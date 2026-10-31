// FloatingWindow.cpp - 圆形悬浮窗 + 拾取覆盖模式
// 正常模式：圆形图标，拖拽、左键切换主窗口、右键菜单
// 拾取模式：全屏透明覆盖，用户点击目标窗口 → SetWindowDisplayAffinity
#include "FloatingWindow.h"
#include "MythwareExt.h"
#include "Resource.h"
#include <tlhelp32.h>
#include <cstdio>

#define FLOAT_SIZE 33

// 主窗口菜单命令 ID（在 JiYuTrainerUI/resource.h 定义为 32776）
#ifndef IDM_SHOWMAIN
#define IDM_SHOWMAIN 32776
#endif

// ── 从图标掩码生成窗口形状区域 ────────────────────────
static HRGN CreateRgnFromIconMask(HBITMAP hbmMask, int w, int h) {
	if (!hbmMask) return nullptr;
	HDC hdc = ::GetDC(NULL);
	HDC memDC = ::CreateCompatibleDC(hdc);
	::ReleaseDC(NULL, hdc);
	if (!memDC) return nullptr;
	::SelectObject(memDC, hbmMask);

	BITMAPINFOHEADER bih = {};
	bih.biSize = sizeof(BITMAPINFOHEADER);
	bih.biWidth = w;
	bih.biHeight = h;
	bih.biPlanes = 1;
	bih.biBitCount = 1;
	bih.biCompression = BI_RGB;

	BYTE* bits = new BYTE[w * h];
	if (!bits) { ::DeleteDC(memDC); return nullptr; }
	::GetDIBits(memDC, hbmMask, 0, h, bits, (BITMAPINFO*)&bih, DIB_RGB_COLORS);

	HRGN hRgn = ::CreateRectRgn(0, 0, 0, 0);
	for (int y = 0; y < h; y++) {
		int row = h - 1 - y;
		BYTE* rowBits = bits + row * ((w + 7) / 8);
		int xStart = -1;
		for (int x = 0; x <= w; x++) {
			bool opaque = (x < w) && !(rowBits[x >> 3] & (0x80 >> (x & 7)));
			if (opaque && xStart < 0) xStart = x;
			if (!opaque && xStart >= 0) {
				HRGN hLine = ::CreateRectRgn(xStart, y, x, y + 1);
				::CombineRgn(hRgn, hRgn, hLine, RGN_OR);
				::DeleteObject(hLine);
				xStart = -1;
			}
		}
	}
	delete[] bits;
	::DeleteDC(memDC);
	return hRgn;
}

// ── 前置声明（文件后面定义） ──────────────────────────
static void DrawGradientBg(HDC hdc, const RECT& rc, COLORREF topColor, COLORREF bottomColor);

// ── 构造 / 析构 ──────────────────────────────────────
FloatingWindow::FloatingWindow(HWND hMainWnd, HINSTANCE hInst)
	: m_hMainWnd(hMainWnd), m_hInst(hInst) {
}

FloatingWindow::~FloatingWindow() {
	Destroy();
}

// ── 防屏录 ───────────────────────────────────────────
void FloatingWindow::ApplyAntiCapture(HWND hWnd) {
	typedef BOOL(WINAPI* SDA_t)(HWND, DWORD);
	static SDA_t fnSDA = nullptr;
	if (!fnSDA) {
		fnSDA = (SDA_t)::GetProcAddress(::GetModuleHandleW(L"user32.dll"),
			"SetWindowDisplayAffinity");
	}
	if (fnSDA && hWnd) {
		fnSDA(hWnd, 1);  // WDA_EXCLUDEFROMCAPTURE
	}
}

// ── 是否本进程窗口 ───────────────────────────────────
bool FloatingWindow::IsOwnWindow(HWND h) {
	if (!h) return true;
	DWORD pid = 0;
	::GetWindowThreadProcessId(h, &pid);
	return pid == ::GetCurrentProcessId();
}

// ── 跟踪外部前台窗口 ─────────────────────────────────
void FloatingWindow::TrackExternalForeground() {
	HWND fg = ::GetForegroundWindow();
	if (fg && !IsOwnWindow(fg)) m_hLastExternal = fg;
}

// ── 最小化焦点窗口 ───────────────────────────────────
void FloatingWindow::MinimizeFocusWindow() {
	HWND hFg = ::GetForegroundWindow();
	if (hFg && !IsOwnWindow(hFg)) {
		::ShowWindow(hFg, SW_MINIMIZE);
		return;
	}
	if (m_hLastExternal && ::IsWindow(m_hLastExternal) &&
		::IsWindowVisible(m_hLastExternal) && !::IsIconic(m_hLastExternal) &&
		!IsOwnWindow(m_hLastExternal)) {
		::ShowWindow(m_hLastExternal, SW_MINIMIZE);
	}
}

// ── 绘制悬浮窗内容 ──────────────────────────────────
void FloatingWindow::DrawContent(HDC hdc, int sz) {
	if (m_hIcon) {
		::DrawIconEx(hdc, 0, 0, m_hIcon, sz, sz, 0, NULL, DI_NORMAL);
	} else {
		HBRUSH hBg = ::CreateSolidBrush(RGB(60, 60, 70));
		HBRUSH hOld = (HBRUSH)::SelectObject(hdc, hBg);
		::Ellipse(hdc, 0, 0, sz + 1, sz + 1);
		::SelectObject(hdc, hOld);
		::DeleteObject(hBg);
	}

	if (m_hovered) {
		HBRUSH hOldBr = (HBRUSH)::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
		HPEN hPen, hOldPen;
		hPen = ::CreatePen(PS_SOLID, 1, RGB(60, 120, 200));
		hOldPen = (HPEN)::SelectObject(hdc, hPen);
		::Ellipse(hdc, 0, 0, sz, sz);
		::SelectObject(hdc, hOldPen); ::DeleteObject(hPen);
		hPen = ::CreatePen(PS_SOLID, 2, RGB(100, 160, 230));
		hOldPen = (HPEN)::SelectObject(hdc, hPen);
		::Ellipse(hdc, 1, 1, sz - 1, sz - 1);
		::SelectObject(hdc, hOldPen); ::DeleteObject(hPen);
		hPen = ::CreatePen(PS_SOLID, 2, RGB(160, 200, 255));
		hOldPen = (HPEN)::SelectObject(hdc, hPen);
		::Ellipse(hdc, 2, 2, sz - 2, sz - 2);
		::SelectObject(hdc, hOldPen); ::DeleteObject(hPen);
		::SelectObject(hdc, hOldBr);
	}
}

// ── 创建悬浮窗 ───────────────────────────────────────
bool FloatingWindow::Create() {
	// 0. 创建隐形 owner 窗口
	static bool s_ownerClass = false;
	if (!s_ownerClass) {
		WNDCLASSEXW oc = {};
		oc.cbSize = sizeof(WNDCLASSEX);
		oc.lpfnWndProc = ::DefWindowProcW;
		oc.hInstance = m_hInst;
		oc.lpszClassName = L"JyuHiddenOwner";
		::RegisterClassExW(&oc);
		s_ownerClass = true;
	}
	// ★ hidden owner owned 于主窗口 → Windows owned 规则自动保证 z-order 在主窗口之上
	m_hOwnerHidden = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
		L"JyuHiddenOwner", L"",
		WS_POPUP, -10000, -10000, 1, 1,
		m_hMainWnd, NULL, m_hInst, NULL);
	if (m_hOwnerHidden) {
		::SetWindowPos(m_hOwnerHidden, HWND_TOPMOST, -10000, -10000, 1, 1,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
	}

	// 1. 加载图标
	m_hIcon = ::LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_APP_MAIN));
	if (!m_hIcon) m_hIcon = ::LoadIcon(NULL, IDI_APPLICATION);
	if (!m_hIcon) return false;

	// 2. 获取图标尺寸 + 掩码
	int iconW = FLOAT_SIZE, iconH = FLOAT_SIZE;
	HBITMAP hbmMask = nullptr;
	ICONINFO ii = {};
	if (::GetIconInfo(m_hIcon, &ii)) {
		iconW = ii.xHotspot * 2;
		iconH = ii.yHotspot * 2;
		hbmMask = ii.hbmMask;
		if (ii.hbmColor) ::DeleteObject(ii.hbmColor);
	}

	// 3. 注册悬浮窗类
	static bool s_classRegistered = false;
	if (!s_classRegistered) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.lpfnWndProc = WndProc;
		wc.hInstance = m_hInst;
		wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = (HBRUSH)::GetStockObject(BLACK_BRUSH);
		wc.lpszClassName = L"JyuFloatingWnd";
		ATOM atom = ::RegisterClassExW(&wc);
		DWORD err1 = ::GetLastError();
		if (!atom && err1 != ERROR_CLASS_ALREADY_EXISTS) {
			if (hbmMask) ::DeleteObject(hbmMask);
			return false;
		}
		s_classRegistered = true;
	}

	// ★ 4. 注册拾取覆盖窗口类
	static bool s_pickClassRegistered = false;
	if (!s_pickClassRegistered) {
		WNDCLASSEXW pc = {};
		pc.cbSize = sizeof(WNDCLASSEX);
		pc.lpfnWndProc = PickOverlayWndProc;
		pc.hInstance = m_hInst;
		pc.hCursor = ::LoadCursor(NULL, IDC_CROSS);  // 十字光标
		pc.hbrBackground = (HBRUSH)::GetStockObject(BLACK_BRUSH);
		pc.lpszClassName = L"JyuPickOverlay";
		ATOM atom = ::RegisterClassExW(&pc);
		DWORD err = ::GetLastError();
		if (!atom && err != ERROR_CLASS_ALREADY_EXISTS) {
			if (hbmMask) ::DeleteObject(hbmMask);
			return false;
		}
		s_pickClassRegistered = true;
	}

	// 5. 创建悬浮窗
	m_hWnd = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
		L"JyuFloatingWnd", L"",
		WS_POPUP, 0, 0, iconW, iconH,
		m_hOwnerHidden, NULL, m_hInst, this);

	if (!m_hWnd) {
		if (hbmMask) ::DeleteObject(hbmMask);
		return false;
	}

	// 6. 从图标掩码生成贴合形状的窗口区域
	HRGN hRgn = nullptr;
	if (hbmMask) {
		hRgn = CreateRgnFromIconMask(hbmMask, iconW, iconH);
		::DeleteObject(hbmMask);
	}
	if (hRgn) {
		::SetWindowRgn(m_hWnd, hRgn, TRUE);
	} else {
		hRgn = ::CreateEllipticRgn(0, 0, iconW + 1, iconH + 1);
		::SetWindowRgn(m_hWnd, hRgn, TRUE);
	}

	// 7. 初始位置 + 显示
	int sw = ::GetSystemMetrics(SM_CXSCREEN);
	::SetWindowPos(m_hWnd, HWND_TOPMOST, (sw - iconW) / 2, 2,
		iconW, iconH, SWP_NOACTIVATE);

	::ShowWindow(m_hWnd, SW_SHOW);
	ApplyAntiCapture(m_hWnd);
	::SetTimer(m_hWnd, 1, 400, NULL);

	return true;
}

// ── 销毁悬浮窗 ───────────────────────────────────────
void FloatingWindow::Destroy() {
	ExitPickMode();

	if (m_hWnd && ::IsWindow(m_hWnd)) {
		::KillTimer(m_hWnd, 1);
		::DestroyWindow(m_hWnd);
	}
	if (m_hOwnerHidden && ::IsWindow(m_hOwnerHidden)) {
		::DestroyWindow(m_hOwnerHidden);
	}
	if (m_hIcon) { ::DestroyIcon(m_hIcon); m_hIcon = nullptr; }
	m_hWnd = nullptr;
	m_hOwnerHidden = nullptr;
}

// ── 显示/隐藏切换 ─────────────────────────────────────
void FloatingWindow::Toggle() {
	if (!m_hWnd) return;
	if (::IsWindowVisible(m_hWnd))
		::ShowWindow(m_hWnd, SW_HIDE);
	else
		::ShowWindow(m_hWnd, SW_SHOW);
}

// ── 复位位置 ─────────────────────────────────────────
void FloatingWindow::ResetPos() {
	if (!m_hWnd) return;
	RECT rc; ::GetWindowRect(m_hWnd, &rc);
	int w = rc.right - rc.left, h = rc.bottom - rc.top;
	int sw = ::GetSystemMetrics(SM_CXSCREEN);
	::SetWindowPos(m_hWnd, HWND_TOPMOST, (sw - w) / 2, 2, w, h, SWP_NOACTIVATE);
}

// ═════════════════════════════════════════════════════
// ★ 拾取模式：全屏透明覆盖窗口
// ═════════════════════════════════════════════════════

void FloatingWindow::EnterPickMode() {
	if (m_pickMode) return;
	m_pickMode = true;

	// 隐藏圆形悬浮窗（让目标窗口暴露出来）
	if (m_hWnd) ::ShowWindow(m_hWnd, SW_HIDE);

	int sw = ::GetSystemMetrics(SM_CXSCREEN);
	int sh = ::GetSystemMetrics(SM_CYSCREEN);

	// 创建全屏透明覆盖窗口
	m_hPickOverlay = ::CreateWindowExW(
		WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
		L"JyuPickOverlay", L"",
		WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		0, 0, sw, sh,
		m_hOwnerHidden, NULL, m_hInst, this);

	if (!m_hPickOverlay) {
		m_pickMode = false;
		if (m_hWnd) ::ShowWindow(m_hWnd, SW_SHOW);
		return;
	}

	// 设为几乎全透明（alpha=1），完全不透明时鼠标穿透，必须 >= 1
	::SetLayeredWindowAttributes(m_hPickOverlay, 0, 1, LWA_ALPHA);

	::SetWindowPos(m_hPickOverlay, HWND_TOPMOST, 0, 0, sw, sh,
		SWP_NOACTIVATE | SWP_SHOWWINDOW);

	// 提示：让主窗口显示一个气泡
	if (m_hMainWnd && ::IsWindow(m_hMainWnd) && !::IsWindowVisible(m_hMainWnd)) {
		::ShowWindow(m_hMainWnd, SW_SHOWNOACTIVATE);
	}
}

void FloatingWindow::ExitPickMode() {
	if (!m_pickMode) return;
	m_pickMode = false;

	if (m_hPickOverlay && ::IsWindow(m_hPickOverlay)) {
		::DestroyWindow(m_hPickOverlay);
	}
	m_hPickOverlay = nullptr;

	// 恢复圆形悬浮窗
	if (m_hWnd && ::IsWindow(m_hWnd)) {
		::ShowWindow(m_hWnd, SW_SHOW);
	}
}

// ── 覆盖窗口过程（静态转发） ──────────────────────────
LRESULT CALLBACK FloatingWindow::PickOverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	FloatingWindow* self = nullptr;
	if (msg == WM_NCCREATE) {
		CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
		self = (FloatingWindow*)cs->lpCreateParams;
		if (self) ::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
	} else {
		self = (FloatingWindow*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	}
	if (self) return self->OnPickOverlayMessage(hWnd, msg, wParam, lParam);
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT FloatingWindow::OnPickOverlayMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {

	case WM_PAINT: {
		// ★ 全屏渐变遮罩（加深版 + 艺术渐变）
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);
		if (hdc) {
			int sw = ::GetSystemMetrics(SM_CXSCREEN);
			int sh = ::GetSystemMetrics(SM_CYSCREEN);
			RECT rc = { 0, 0, sw, sh };

			// ★ 垂直渐变：顶部深夜蓝 → 底部暗紫蓝
			DrawGradientBg(hdc, rc, RGB(18, 32, 72), RGB(8, 16, 42));

			// ★ 中心光晕（径向渐变感：中心区域稍亮）
			HBRUSH hGlow = ::CreateSolidBrush(RGB(30, 55, 130));
			// 用多圈半透明椭圆模拟中心光效
			for (int i = 0; i < 5; i++) {
				int size = 300 - i * 40;
				RECT rcG = { sw / 2 - size, sh / 2 - size - 20, sw / 2 + size, sh / 2 + size - 20 };
				::FillRect(hdc, &rcG, hGlow);
			}
			::DeleteObject(hGlow);

			// ★ 画中心提示文字（加大字号 + 阴影）
			::SetBkMode(hdc, TRANSPARENT);
			HFONT hFont = ::CreateFontW(42, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
			HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
			RECT rcText = { sw / 2 - 400, sh / 2 - 60, sw / 2 + 400, sh / 2 - 10 };

			// 文字阴影
			::SetTextColor(hdc, RGB(0, 0, 0));
			::SetTextCharacterExtra(hdc, 0);
			RECT rcShadow = { rcText.left + 2, rcText.top + 2, rcText.right + 2, rcText.bottom + 2 };
			::DrawTextW(hdc, L"点击要防屏录的窗口", -1, &rcShadow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			// 主文字
			::SetTextColor(hdc, RGB(255, 255, 255));
			::DrawTextW(hdc, L"点击要防屏录的窗口", -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

			// 副文字提示
			HFONT hFont2 = ::CreateFontW(22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
				DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
			HFONT hOld2 = (HFONT)::SelectObject(hdc, hFont2);
			RECT rcSub = { sw / 2 - 400, sh / 2 + 10, sw / 2 + 400, sh / 2 + 50 };
			::SetTextColor(hdc, RGB(200, 210, 240));
			::DrawTextW(hdc, L"右键取消  |  ESC 退出", -1, &rcSub, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			::SelectObject(hdc, hOld2);
			::DeleteObject(hFont2);

			::SelectObject(hdc, hOld);
			::DeleteObject(hFont);
		}
		::EndPaint(hWnd, &ps);
		break;
	}

	case WM_LBUTTONDOWN: {
		::SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA);  // 先变透明让 WindowFromPoint 穿透
		POINT pt; ::GetCursorPos(&pt);
		::SetLayeredWindowAttributes(hWnd, 0, 1, LWA_ALPHA);  // 恢复

		// 多窗口穿透：一直找到最底层的非自己窗口
		HWND hwnd = ::WindowFromPoint(pt);
		HWND prev = nullptr;
		while (hwnd && hwnd != prev) {
			// 如果是自己的窗口或子窗口，跳过
			if (hwnd == hWnd || ::IsChild(hWnd, hwnd)) {
				prev = hwnd;
				hwnd = ::WindowFromPoint(pt);
				continue;
			}
			// 如果是 JiYuTrainer 自己的窗口，也跳过
			if (IsOwnWindow(hwnd)) {
				prev = hwnd;
				hwnd = ::GetWindow(hwnd, GW_HWNDPREV);
				continue;
			}
			break;
		}

		ExitPickMode();

		if (hwnd && !IsOwnWindow(hwnd)) {
			ApplyAntiCapture(hwnd);
			// 恢复主窗口（如果之前被最小化了）
			if (m_hMainWnd && ::IsWindow(m_hMainWnd)) {
				if (::IsIconic(m_hMainWnd)) {
					::ShowWindow(m_hMainWnd, SW_RESTORE);
				}
			}
		}
		return 0;
	}

	case WM_RBUTTONDOWN: {
		ExitPickMode();
		return 0;
	}

	case WM_KEYDOWN: {
		if (wParam == VK_ESCAPE) {
			ExitPickMode();
			return 0;
		}
		break;
	}

	case WM_DESTROY:
		m_hPickOverlay = nullptr;
		break;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ═════════════════════════════════════════════════════
// 正常圆形悬浮窗消息处理
// ═════════════════════════════════════════════════════

LRESULT CALLBACK FloatingWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	FloatingWindow* self = nullptr;
	if (msg == WM_NCCREATE) {
		CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
		self = (FloatingWindow*)cs->lpCreateParams;
		if (self) self->m_hWnd = hWnd;
		::SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)self);
	} else {
		self = (FloatingWindow*)::GetWindowLongPtr(hWnd, GWLP_USERDATA);
	}
	if (self) return self->OnMessage(hWnd, msg, wParam, lParam);
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT FloatingWindow::OnMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {

	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED) ::ShowWindow(hWnd, SW_RESTORE);
		break;

	case WM_TIMER:
		ApplyAntiCapture(hWnd);
		// ★ hidden owner 已 owned 于主窗口，MainWindow 的 EnumOwnedTopMost 自动处理层级
		//    这里只处理主窗口不可见的场景（主窗口 SW_HIDE 后不再刷 TIMER_AOP）
		if (m_hOwnerHidden && ::IsWindow(m_hOwnerHidden)) {
			bool mainVisible = m_hMainWnd && ::IsWindow(m_hMainWnd) &&
				::IsWindowVisible(m_hMainWnd) && !::IsIconic(m_hMainWnd);
			if (!mainVisible) {
				::SetWindowPos(m_hOwnerHidden, HWND_TOPMOST, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}
		}
		TrackExternalForeground();
		break;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(hWnd, &ps);
		if (hdc) {
			RECT rc; ::GetClientRect(hWnd, &rc);
			int sz = rc.right - rc.left;
			HDC memDC = ::CreateCompatibleDC(hdc);
			if (memDC) {
				HBITMAP memBmp = ::CreateCompatibleBitmap(hdc, sz, sz);
				if (memBmp) {
					HBITMAP hOldBmp = (HBITMAP)::SelectObject(memDC, memBmp);
					DrawContent(memDC, sz);
					::BitBlt(hdc, 0, 0, sz, sz, memDC, 0, 0, SRCCOPY);
					::SelectObject(memDC, hOldBmp);
					::DeleteObject(memBmp);
				}
				::DeleteDC(memDC);
			}
		}
		::EndPaint(hWnd, &ps);
		break;
	}

	case WM_MOUSEMOVE: {
		if (::GetCapture() == hWnd) {
			POINT ptNow; ::GetCursorPos(&ptNow);
			int dx = ptNow.x - m_dragBase.x;
			int dy = ptNow.y - m_dragBase.y;
			if (abs(dx) > 3 || abs(dy) > 3) m_dragging = true;
			if (m_dragging) {
				::SetWindowPos(hWnd, HWND_TOPMOST,
					m_dragStartRect.left + dx, m_dragStartRect.top + dy,
					0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
			}
		}
		if (!m_hovered) {
			m_hovered = true;
			::InvalidateRect(hWnd, NULL, FALSE);
			TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
			tme.dwFlags = TME_LEAVE; tme.hwndTrack = hWnd;
			::TrackMouseEvent(&tme);
		}
		break;
	}

	case WM_MOUSELEAVE:
		m_hovered = false;
		::InvalidateRect(hWnd, NULL, FALSE);
		break;

	case WM_LBUTTONDOWN:
		m_dragging = false;
		::GetCursorPos(&m_dragBase);
		::GetWindowRect(hWnd, &m_dragStartRect);
		::SetCapture(hWnd);
		break;

	case WM_LBUTTONUP:
		::ReleaseCapture();
		if (!m_dragging && m_hMainWnd && ::IsWindow(m_hMainWnd)) {
			// ★ 发 WM_COMMAND / IDM_SHOWMAIN 走 MainWindow 统一联动逻辑
			// （隐藏主窗口的同时 → 自动隐藏所有附属窗口；恢复时 → 自动恢复）
			::PostMessage(m_hMainWnd, WM_COMMAND, IDM_SHOWMAIN, 0);
		}
		break;

	case WM_MBUTTONDOWN:
		mythware_ext::ToggleBroadcastWindow();
		break;

	case WM_RBUTTONDOWN:
		ShowContextMenu(hWnd);
		break;

	case WM_DESTROY:
		::KillTimer(hWnd, 1);
		if (m_hIcon) { ::DestroyIcon(m_hIcon); m_hIcon = nullptr; }
		break;

	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ── 右键菜单 ─────────────────────────────────────────
void FloatingWindow::ShowContextMenu(HWND hWnd) {
	TrackExternalForeground();

	POINT pt; ::GetCursorPos(&pt);
	HMENU hMenu = ::CreatePopupMenu();

	::AppendMenuW(hMenu, MF_STRING, 1,
		(m_hMainWnd && ::IsWindowVisible(m_hMainWnd)) ? L"隐藏面板" : L"打开面板");
	::AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
	::AppendMenuW(hMenu, MF_STRING, 8, L"防屏录拾取...");
	::AppendMenuW(hMenu, MF_STRING, 7, L"退出黑屏");
	::AppendMenuW(hMenu, MF_STRING, 6, L"最小化焦点窗口");
	::AppendMenuW(hMenu, MF_STRING, 2, L"杀掉极域");
	::AppendMenuW(hMenu, MF_STRING, 3, L"解除USB限制");
	::AppendMenuW(hMenu, MF_STRING, 4, L"解除网络限制");
	::AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
	::AppendMenuW(hMenu, MF_STRING, 5, L"退出");

	int cmd = ::TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
		pt.x, pt.y, 0, hWnd, NULL);
	::DestroyMenu(hMenu);

	switch (cmd) {
	case 1:
		if (m_hMainWnd && ::IsWindow(m_hMainWnd)) {
			// ★ 统一走 IDM_SHOWMAIN（隐藏→联动隐藏附属；恢复→联动恢复）
			::PostMessage(m_hMainWnd, WM_COMMAND, IDM_SHOWMAIN, 0);
		}
		break;
	case 8:  ShowWindowListDialog(); break;
	case 7:  mythware_ext::ExitBlackScreen(); break;
	case 6:  MinimizeFocusWindow(); break;
	case 2:  mythware_ext::KillMythwareProcess(); break;
	case 3:  mythware_ext::RemoveUSBRestrictions(); break;
	case 4:  mythware_ext::RemoveNetworkRestrictions(); break;
	case 5:  ::PostQuitMessage(0); break;
	}
}

// ═════════════════════════════════════════════════════
// ★ 窗口列表对话框（艺术增强版）
// ═════════════════════════════════════════════════════

// ★ 渐变背景绘制（GDI GradientFill 线性渐变）
static void DrawGradientBg(HDC hdc, const RECT& rc,
	COLORREF topColor, COLORREF bottomColor) {
	TRIVERTEX tv[2] = {
		{ rc.left,  rc.top,
			(USHORT)(GetRValue(topColor) * 256),
			(USHORT)(GetGValue(topColor) * 256),
			(USHORT)(GetBValue(topColor) * 256),
			0xFF00 },
		{ rc.right, rc.bottom,
			(USHORT)(GetRValue(bottomColor) * 256),
			(USHORT)(GetGValue(bottomColor) * 256),
			(USHORT)(GetBValue(bottomColor) * 256),
			0xFF00 },
	};
	GRADIENT_RECT gr = { 0, 1 };
	::GradientFill(hdc, tv, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

struct WndListDlg {
	HWND hDlg = nullptr;
	HWND hList = nullptr;
	bool selected = false;
	HWND selectedHwnd = nullptr;
	int W = 520, H = 520;  // ★ 调高：从 480×400 → 520×520
};

static LRESULT CALLBACK WndListProc(HWND h, UINT m, WPARAM w, LPARAM l);  // 前置声明

static BOOL CALLBACK EnumVisibleWnds(HWND hwnd, LPARAM lParam) {
	auto* dlg = (WndListDlg*)lParam;
	if (!::IsWindowVisible(hwnd) || ::IsIconic(hwnd)) return TRUE;
	DWORD pid = 0; ::GetWindowThreadProcessId(hwnd, &pid);
	if (pid == ::GetCurrentProcessId()) return TRUE;

	wchar_t title[256] = {};
	::GetWindowTextW(hwnd, title, 256);
	if (!title[0]) return TRUE;

	wchar_t procName[MAX_PATH] = L"";
	HANDLE hSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnap != INVALID_HANDLE_VALUE) {
		PROCESSENTRY32W pe = { sizeof(pe) };
		if (::Process32FirstW(hSnap, &pe)) {
			do {
				if (pe.th32ProcessID == pid) {
					wcscpy_s(procName, MAX_PATH, pe.szExeFile); break;
				}
			} while (::Process32NextW(hSnap, &pe));
		}
		::CloseHandle(hSnap);
	}

	wchar_t display[512];
	swprintf_s(display, L"%s  (%s)", title, procName);
	int idx = (int)::SendMessageW(dlg->hList, LB_ADDSTRING, 0, (LPARAM)display);
	if (idx != LB_ERR) ::SendMessageW(dlg->hList, LB_SETITEMDATA, idx, (LPARAM)hwnd);
	return TRUE;
}

void FloatingWindow::ShowWindowListDialog() {
	static bool s_reg = false;
	if (!s_reg) {
		WNDCLASSEXW wc = {};
		wc.cbSize = sizeof(wc);
		wc.lpfnWndProc = ::DefWindowProcW;
		wc.hInstance = m_hInst;
		wc.hCursor = ::LoadCursorW(NULL, IDC_ARROW);
		wc.hbrBackground = nullptr;  // ★ 自绘背景
		wc.lpszClassName = L"JyuWndListPopup";
		::RegisterClassExW(&wc);
		s_reg = true;
	}

	const int WW = 520, WH = 520;  // ★ 调高：从 480×400
	int sw = ::GetSystemMetrics(SM_CXSCREEN);
	int sh = ::GetSystemMetrics(SM_CYSCREEN);

	// ★ owner 设为 hidden owner：Windows owned 规则保证对话框始终在 owner 之上
	WndListDlg dlg = {};
	HWND hDlg = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"JyuWndListPopup",
		L"",  // ★ 标题栏自绘，去掉系统标题
		WS_POPUP,  // ★ 去掉 WS_CAPTION/WS_SYSMENU，完全自绘
		(sw - WW) / 2, (sh - WH) / 2, WW, WH,
		m_hOwnerHidden, NULL, m_hInst, NULL);
	if (!hDlg) return;

	dlg.hDlg = hDlg;
	::SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)&dlg);
	::SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)WndListProc);

	// ★ 圆角窗口区域（R=20）
	const int RADIUS = 20;
	HRGN hRgn = ::CreateRoundRectRgn(0, 0, WW + 1, WH + 1, RADIUS, RADIUS);
	::SetWindowRgn(hDlg, hRgn, TRUE);

	// ListBox（位置适配新尺寸）
	const int MARGIN = 16;
	const int TITLE_H = 44;
	const int BTN_H = 34;
	dlg.hList = ::CreateWindowExW(0, L"LISTBOX", NULL,
		WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
		MARGIN, TITLE_H + MARGIN, WW - MARGIN * 2, WH - TITLE_H - BTN_H - MARGIN * 3,
		hDlg, (HMENU)1001, m_hInst, NULL);
	::EnumWindows(EnumVisibleWnds, (LPARAM)&dlg);
	if (::SendMessageW(dlg.hList, LB_GETCOUNT, 0, 0) > 0)
		::SendMessageW(dlg.hList, LB_SETCURSEL, 0, 0);

	// Buttons（适配新位置）
	const int BTN_W = 80;
	const int BTN_Y = WH - BTN_H - MARGIN;
	::CreateWindowExW(0, L"BUTTON", L"\x786E\x5B9A",
		WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
		WW - MARGIN - BTN_W * 2 - 10, BTN_Y, BTN_W, BTN_H, hDlg, (HMENU)1002, m_hInst, NULL);
	::CreateWindowExW(0, L"BUTTON", L"\x53D6\x6D88",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		WW - MARGIN - BTN_W, BTN_Y, BTN_W, BTN_H, hDlg, (HMENU)1003, m_hInst, NULL);

	::ShowWindow(hDlg, SW_SHOW);
	::SetForegroundWindow(hDlg);
	::SetFocus(dlg.hList);

	// 模态消息循环
	MSG msg;
	while (::IsWindow(hDlg) && ::GetMessageW(&msg, NULL, 0, 0)) {
		if (!::IsDialogMessageW(hDlg, &msg)) {
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
	}
}

// 对话框子类化过程（静态函数）
static LRESULT CALLBACK WndListProc(HWND h, UINT m, WPARAM w, LPARAM l) {
	auto* pDlg = (WndListDlg*)::GetWindowLongPtrW(h, GWLP_USERDATA);

	switch (m) {

	case WM_ERASEBKGND:  // ★ 阻止系统擦除背景，用自绘
		return 1;

	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = ::BeginPaint(h, &ps);
		if (!hdc) break;
		RECT rc; ::GetClientRect(h, &rc);
		int W = rc.right - rc.left, H = rc.bottom - rc.top;
		const int R = 20;  // ★ 圆角半径（改大更明显）

		// ★ 1. 建立圆角裁剪区（保证所有绘制都在圆角内）
		HRGN hClip = ::CreateRoundRectRgn(0, 0, W + 1, H + 1, R, R);
		::SelectClipRgn(hdc, hClip);

		// ★ 2. 渐变背景：顶部淡蓝 → 底部深蓝
		DrawGradientBg(hdc, rc, RGB(45, 85, 170), RGB(15, 35, 85));

		// ★ 3. 标题栏渐变（更深的顶条）
		const int TITLE_H = 44;
		RECT rcTitle = { 0, 0, W, TITLE_H };
		DrawGradientBg(hdc, rcTitle, RGB(30, 65, 140), RGB(18, 40, 100));

		// ★ 4. 标题文字
		::SetBkMode(hdc, TRANSPARENT);
		::SetTextColor(hdc, RGB(255, 255, 255));
		HFONT hFont = ::CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei");
		HFONT hOld = (HFONT)::SelectObject(hdc, hFont);
		RECT rcTxt = { 20, 0, W - 60, TITLE_H };
		::DrawTextW(hdc, L"选择要防屏录的窗口", -1, &rcTxt,
			DT_LEFT | DT_VCENTER | DT_SINGLELINE);
		::SelectObject(hdc, hOld);
		::DeleteObject(hFont);

		// ★ 5. 关闭按钮 (X)
		::SetTextColor(hdc, RGB(255, 180, 180));
		HFONT hFont2 = ::CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
			DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
		HFONT hOld2 = (HFONT)::SelectObject(hdc, hFont2);
		RECT rcX = { W - 32, 0, W - 4, TITLE_H };
		::DrawTextW(hdc, L"✕", -1, &rcX, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		::SelectObject(hdc, hOld2);
		::DeleteObject(hFont2);

		// ★ 6. 恢复裁剪区后画圆角边框（不然圆角外没东西）
		::SelectClipRgn(hdc, NULL);

		// ★ 7. 明确的圆角边框（外发光 + 内高光）
		HPEN hPenGlow = ::CreatePen(PS_SOLID, 3, RGB(80, 130, 230));
		HPEN hPenHighlight = ::CreatePen(PS_SOLID, 1, RGB(200, 220, 255));
		HBRUSH hBrushNull = (HBRUSH)::GetStockObject(NULL_BRUSH);
		HBRUSH hOldBrush = (HBRUSH)::SelectObject(hdc, hBrushNull);

		::SelectObject(hdc, hPenGlow);
		::RoundRect(hdc, 1, 1, W - 1, H - 1, R, R);
		::SelectObject(hdc, hPenHighlight);
		::RoundRect(hdc, 3, 3, W - 3, H - 3, R - 1, R - 1);

		::SelectObject(hdc, hOldBrush);
		::DeleteObject(hPenGlow);
		::DeleteObject(hPenHighlight);

		::DeleteObject(hClip);
		::EndPaint(h, &ps);
		break;
	}

	case WM_NCHITTEST: {  // ★ 标题栏可拖动 + 关闭按钮
		POINT pt = { (short)LOWORD(l), (short)HIWORD(l) };
		::ScreenToClient(h, &pt);
		int W = pDlg ? pDlg->W : 520;
		if (pt.y < 44) {
			if (pt.x >= W - 32) return HTCLOSE;
			return HTCAPTION;
		}
		return HTCLIENT;
	}

	case WM_COMMAND: {
		switch (LOWORD(w)) {
		case 1001:
			if (HIWORD(w) == LBN_DBLCLK) goto do_select;
			break;
		case 1002:
		do_select: {
			int sel = (int)::SendMessageW(pDlg->hList, LB_GETCURSEL, 0, 0);
			if (sel != LB_ERR) {
				pDlg->selectedHwnd = (HWND)::SendMessageW(pDlg->hList, LB_GETITEMDATA, sel, 0);
				pDlg->selected = true;
			}
			::DestroyWindow(h);
			return 0;
		}
		case 1003:
			::DestroyWindow(h); return 0;
		}
		break;
	}
	case WM_CLOSE:
		::DestroyWindow(h); return 0;
	case WM_DESTROY:
		if (pDlg && pDlg->selected && pDlg->selectedHwnd) {
			typedef BOOL(WINAPI* SDA_t)(HWND, DWORD);
			static SDA_t fnSDA = nullptr;
			if (!fnSDA) fnSDA = (SDA_t)::GetProcAddress(::GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity");
			if (fnSDA) fnSDA(pDlg->selectedHwnd, 1);
		}
		break;
	}
	return ::DefWindowProcW(h, m, w, l);
}
