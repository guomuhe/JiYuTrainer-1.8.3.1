// FloatingWindow.h - 圆形悬浮窗 + 拾取覆盖模式
// 正常模式：圆形图标，拖拽、左键切换主窗口、右键菜单
// 拾取模式：全屏透明覆盖，用户点击目标窗口 → SetWindowDisplayAffinity
#pragma once
#include <windows.h>

class FloatingWindow {
public:
	// hMainWnd: 主窗口句柄（悬浮窗左键点击时显示/隐藏它）
	// hInst: 实例句柄（用于加载图标）
	FloatingWindow(HWND hMainWnd, HINSTANCE hInst);
	~FloatingWindow();

	bool Create();      // 创建并显示悬浮窗
	void Destroy();     // 销毁悬浮窗
	void Toggle();      // 显示/隐藏切换
	void ResetPos();    // 复位到屏幕顶部中间
	HWND GetHwnd() const { return m_hWnd; }
	HWND GetOwnerHidden() const { return m_hOwnerHidden; }
	bool IsVisible() const { return m_hWnd && IsWindowVisible(m_hWnd); }

	// ★ 拾取模式
	void EnterPickMode();   // 进入全屏透明覆盖模式
	void ExitPickMode();    // 退出拾取模式
	bool IsPickMode() const { return m_pickMode; }

	// ★ 窗口列表选择对话框
	void ShowWindowListDialog();  // 列出所有窗口让用户选择

private:
	HWND m_hWnd = nullptr;
	HWND m_hMainWnd;
	HWND m_hOwnerHidden = nullptr;
	HINSTANCE m_hInst;
	HICON m_hIcon = nullptr;

	// 拾取模式
	HWND m_hPickOverlay = nullptr;   // 全屏透明覆盖窗口
	bool m_pickMode = false;

	// 拖拽状态
	POINT m_dragBase = {};
	RECT  m_dragStartRect = {};
	bool  m_dragging = false;
	bool  m_hovered = false;
	HWND  m_hLastExternal = nullptr;

	// 窗口过程
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT OnMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	// 覆盖窗口过程
	static LRESULT CALLBACK PickOverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT OnPickOverlayMessage(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	// 绘制
	void DrawContent(HDC hdc, int sz);

	// 防屏录
	void ApplyAntiCapture(HWND hWnd);

	// 右键菜单
	void ShowContextMenu(HWND hWnd);

	// 最小化焦点窗口
	void MinimizeFocusWindow();

	// 跟踪外部前台窗口
	void TrackExternalForeground();
	bool IsOwnWindow(HWND h);
};
