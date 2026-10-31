// MythwareExt.cpp - 从 MythwareToolkit 移植的高级功能
// ACL 防杀 / CBT hook 对话框防截屏 / 键鼠解锁 / 极域控制 / 网络限制解除
#include "MythwareExt.h"
#include <aclapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

namespace mythware_ext {

// ============================================================
//  ACL 防杀进程
// ============================================================
static bool g_bProcessProtected = false;

bool ToggleProcessProtection() {
	HANDLE hProcess = GetCurrentProcess();
	PACL pOldDACL = nullptr;
	PSECURITY_DESCRIPTOR pSD = nullptr;

	if (!g_bProcessProtected) {
		DWORD err = GetSecurityInfo(hProcess, SE_KERNEL_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr,
			&pOldDACL, nullptr, &pSD);
		if (err != ERROR_SUCCESS) return false;

		EXPLICIT_ACCESS_W ea = {};
		ea.grfAccessPermissions = PROCESS_TERMINATE;
		ea.grfAccessMode = DENY_ACCESS;
		ea.grfInheritance = NO_INHERITANCE;
		ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;

		SID_IDENTIFIER_AUTHORITY sia = SECURITY_WORLD_SID_AUTHORITY;
		PSID pEveryone = nullptr;
		AllocateAndInitializeSid(&sia, 1, SECURITY_WORLD_RID,
			0, 0, 0, 0, 0, 0, 0, &pEveryone);
		ea.Trustee.ptstrName = (LPWSTR)pEveryone;

		PACL pNewDACL = nullptr;
		DWORD err2 = SetEntriesInAclW(1, &ea, pOldDACL, &pNewDACL);
		FreeSid(pEveryone);
		LocalFree(pSD);
		if (err2 != ERROR_SUCCESS) return false;

		DWORD err3 = SetSecurityInfo(hProcess, SE_KERNEL_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr, pNewDACL, nullptr);
		LocalFree(pNewDACL);
		if (err3 != ERROR_SUCCESS) return false;

		g_bProcessProtected = true;
		return true;
	} else {
		SID_IDENTIFIER_AUTHORITY sia = SECURITY_WORLD_SID_AUTHORITY;
		PSID pEveryone = nullptr;
		AllocateAndInitializeSid(&sia, 1, SECURITY_WORLD_RID,
			0, 0, 0, 0, 0, 0, 0, &pEveryone);

		EXPLICIT_ACCESS_W ea2 = {};
		ea2.grfAccessPermissions = PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION |
			SYNCHRONIZE | READ_CONTROL;
		ea2.grfAccessMode = SET_ACCESS;
		ea2.grfInheritance = NO_INHERITANCE;
		ea2.Trustee.TrusteeForm = TRUSTEE_IS_SID;
		ea2.Trustee.ptstrName = (LPWSTR)pEveryone;

		PACL pNewDACL = nullptr;
		DWORD err2 = SetEntriesInAclW(1, &ea2, nullptr, &pNewDACL);
		FreeSid(pEveryone);
		if (err2 != ERROR_SUCCESS) return false;

		DWORD err3 = SetSecurityInfo(hProcess, SE_KERNEL_OBJECT,
			DACL_SECURITY_INFORMATION, nullptr, nullptr, pNewDACL, nullptr);
		LocalFree(pNewDACL);
		if (err3 != ERROR_SUCCESS) return false;

		g_bProcessProtected = false;
		return true;
	}
}

bool IsProcessProtected() { return g_bProcessProtected; }

// ============================================================
//  CBT Hook — 全局对话框防截屏
// ============================================================
static HHOOK g_cbtHook = nullptr;
static HMODULE g_cbtModule = nullptr;

// 获取 user32.dll 里 SetWindowDisplayAffinity 函数指针
static BOOL(WINAPI* g_fnSDA)(HWND, DWORD) = nullptr;
static void EnsureSDA() {
	if (!g_fnSDA) {
		g_fnSDA = (BOOL(WINAPI*)(HWND, DWORD))
			GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowDisplayAffinity");
	}
}

// SDA 应用（带版本检查）
static bool ApplyWDA(HWND hwnd) {
	EnsureSDA();
	if (!g_fnSDA) return false;
	return g_fnSDA(hwnd, 1) == TRUE;  // WDA_EXCLUDEFROMCAPTURE = 1
}

static LRESULT CALLBACK CBTHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
	if (nCode == HCBT_ACTIVATE) {
		HWND hwnd = (HWND)wParam;
		wchar_t cls[32] = {};
		GetClassNameW(hwnd, cls, 32);
		// 对话框类名 #32770
		if (wcscmp(cls, L"#32770") == 0) {
			ApplyWDA(hwnd);
		}
	}
	else if (nCode == HCBT_CREATEWND) {
		HWND hwnd = (HWND)((CBT_CREATEWNDW*)lParam)->lpcs->hwndParent;
		// 父窗口如果是我们的，给新窗口也加上
		// 这里简化处理：延迟到 HCBT_ACTIVATE 再应用
	}
	return CallNextHookEx(g_cbtHook, nCode, wParam, lParam);
}

void InstallDialogProtection() {
	if (g_cbtHook) return;
	g_cbtModule = GetModuleHandleW(nullptr);
	g_cbtHook = SetWindowsHookExW(WH_CBT, CBTHookProc,
		g_cbtModule, GetCurrentThreadId());
}

void UninstallDialogProtection() {
	if (g_cbtHook) {
		UnhookWindowsHookEx(g_cbtHook);
		g_cbtHook = nullptr;
	}
}

// ============================================================
//  键盘/鼠标解锁
// ============================================================
static HANDLE g_hKeyThread = nullptr;
static HANDLE g_hMouseThread = nullptr;
static volatile bool g_inputUnlockRunning = false;
static HHOOK g_kbdHook = nullptr;
static HHOOK g_mseHook = nullptr;

// 空钩子回调：只转发，不拦截
static LRESULT CALLBACK NopHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
	return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static DWORD WINAPI KeyUnlockThread(LPVOID) {
	HMODULE hMod = GetModuleHandleW(nullptr);
	while (g_inputUnlockRunning) {
		g_kbdHook = SetWindowsHookExW(WH_KEYBOARD_LL, NopHookProc, hMod, 0);
		Sleep(25);
		if (g_kbdHook) { UnhookWindowsHookEx(g_kbdHook); g_kbdHook = nullptr; }
	}
	return 0;
}

static DWORD WINAPI MouseUnlockThread(LPVOID) {
	HMODULE hMod = GetModuleHandleW(nullptr);
	while (g_inputUnlockRunning) {
		g_mseHook = SetWindowsHookExW(WH_MOUSE_LL, NopHookProc, hMod, 0);
		ClipCursor(nullptr);  // 解除鼠标区域锁定
		Sleep(25);
		if (g_mseHook) { UnhookWindowsHookEx(g_mseHook); g_mseHook = nullptr; }
	}
	return 0;
}

void StartInputUnlock() {
	if (g_inputUnlockRunning) return;
	g_inputUnlockRunning = true;
	g_hKeyThread = CreateThread(nullptr, 0, KeyUnlockThread, nullptr, 0, nullptr);
	g_hMouseThread = CreateThread(nullptr, 0, MouseUnlockThread, nullptr, 0, nullptr);
}

void StopInputUnlock() {
	g_inputUnlockRunning = false;
	if (g_hKeyThread) {
		WaitForSingleObject(g_hKeyThread, 1000);
		CloseHandle(g_hKeyThread); g_hKeyThread = nullptr;
	}
	if (g_hMouseThread) {
		WaitForSingleObject(g_hMouseThread, 1000);
		CloseHandle(g_hMouseThread); g_hMouseThread = nullptr;
	}
}

bool IsInputUnlockRunning() { return g_inputUnlockRunning; }

// ============================================================
//  进程查找/查杀
// ============================================================
DWORD FindProcessByName(const wchar_t* name) {
	DWORD id = 0;
	PROCESSENTRY32W pe = {};
	pe.dwSize = sizeof(PROCESSENTRY32W);
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (Process32FirstW(snap, &pe)) {
		do {
			if (_wcsicmp(pe.szExeFile, name) == 0) { id = pe.th32ProcessID; break; }
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return id;
}

bool KillProcessById(DWORD pid, bool force) {
	if (force) {
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid);
		if (hSnap == INVALID_HANDLE_VALUE) return false;
		bool ok = true;
		THREADENTRY32 te = {}; te.dwSize = sizeof(te);
		if (Thread32First(hSnap, &te)) {
			do {
				if (te.th32OwnerProcessID == pid) {
					HANDLE hT = OpenThread(THREAD_TERMINATE, FALSE, te.th32ThreadID);
					if (hT) { TerminateThread(hT, 0); CloseHandle(hT); }
				}
			} while (Thread32Next(hSnap, &te));
		}
		CloseHandle(hSnap); return ok;
	} else {
		HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
		if (!h) return false;
		BOOL r = TerminateProcess(h, 0);
		CloseHandle(h); return r == TRUE;
	}
}

bool KillAllWithName(const wchar_t* name, bool force) {
	PROCESSENTRY32W pe = {}; pe.dwSize = sizeof(pe);
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	bool r = false;
	if (Process32FirstW(snap, &pe)) {
		do {
			if (_wcsicmp(pe.szExeFile, name) == 0)
				r = KillProcessById(pe.th32ProcessID, force) || r;
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap); return r;
}

// ============================================================
//  极域进程控制
// ============================================================
static const wchar_t* g_mythwareNames[] = {
	L"StudentMain.exe", L"StudentM.exe", L"StudentMain64.exe", L"Student.exe", nullptr
};

static DWORD FindMythwarePid() {
	for (int i = 0; g_mythwareNames[i]; i++) {
		DWORD pid = FindProcessByName(g_mythwareNames[i]);
		if (pid) return pid;
	}
	return 0;
}

bool KillMythwareProcess() {
	// 同时杀守护进程防止自启
	KillAllWithName(L"MasterHelper.exe", true);
	KillAllWithName(L"GATESRV.exe", true);
	Sleep(50);
	DWORD pid = FindMythwarePid();
	if (pid) return KillProcessById(pid, true);
	return false;
}

// 查找广播窗口：类名 Afx: 且标题是"屏幕广播"/"窗口化屏幕"
static HWND g_foundBroadcast = nullptr;
static BOOL CALLBACK FindBroadcastProc(HWND hwnd, LPARAM) {
	if (!IsWindowVisible(hwnd)) return TRUE;
	wchar_t cls[16] = {};
	if (!GetClassNameW(hwnd, cls, 16)) return TRUE;
	if (wcsncmp(cls, L"Afx:", 4) != 0) return TRUE;
	wchar_t title[256] = {};
	GetWindowTextW(hwnd, title, 256);
	if (wcsstr(title, L"屏幕广播")) {
		g_foundBroadcast = hwnd;
		return FALSE;
	}
	return TRUE;
}

static HWND FindBroadcastWindow() {
	g_foundBroadcast = nullptr;
	EnumWindows(FindBroadcastProc, 0);
	return g_foundBroadcast;
}

bool ToggleBroadcastWindow() {
	HWND hBd = FindBroadcastWindow();
	if (!hBd) return false;
	// 广播窗口里的"窗口化/全屏化"按钮是 ID 1004 的 BM_CLICK
	PostMessageW(hBd, WM_COMMAND, MAKEWPARAM(1004, BM_CLICK), 0);
	return true;
}

// 黑屏退出：4 级递进
static BOOL CALLBACK FindBlackScreenProc(HWND hWnd, LPARAM lParam) {
	if (!IsWindowVisible(hWnd)) return TRUE;
	LONG ex = GetWindowLongW(hWnd, GWL_EXSTYLE);
	if (!(ex & WS_EX_TOPMOST)) return TRUE;
	RECT r; GetWindowRect(hWnd, &r);
	int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
	if (r.right - r.left < sw || r.bottom - r.top < sh) return TRUE;
	*(HWND*)lParam = hWnd;
	return FALSE;
}

bool ExitBlackScreen() {
	HWND hBlack = nullptr;
	EnumWindows(FindBlackScreenProc, (LPARAM)&hBlack);
	if (!hBlack) return false;

	// 第1级：SW_HIDE
	ShowWindow(hBlack, SW_HIDE);
	Sleep(200);
	if (!IsWindowVisible(hBlack)) return true;

	// 第2级：取消置顶 + 最小化
	SetWindowPos(hBlack, HWND_BOTTOM, 0, 0, 0, 0,
		SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	ShowWindow(hBlack, SW_MINIMIZE);
	Sleep(200);
	if (!IsWindowVisible(hBlack) || IsIconic(hBlack)) return true;

	// 第3级：发送 ESC
	SetForegroundWindow(hBlack); Sleep(50);
	keybd_event(VK_ESCAPE, 0, 0, 0);
	keybd_event(VK_ESCAPE, 0, KEYEVENTF_KEYUP, 0);
	Sleep(300);
	if (!IsWindowVisible(hBlack)) return true;

	// 第4级：杀进程（最后手段）
	KillMythwareProcess();
	return true;
}

// 解除网络限制
bool RemoveNetworkRestrictions() {
	bool ok = false;

	// 步骤1：通过设备驱动发停止指令
	HANDLE hDev = CreateFileW(L"\\\\.\\TDNetFilter",
		GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDev != INVALID_HANDLE_VALUE) {
		DWORD br = 0;
		if (DeviceIoControl(hDev, 0x120014, nullptr, 0, nullptr, 0, &br, nullptr))
			ok = true;
		CloseHandle(hDev);
	}

	// 步骤2：杀相关进程
	KillAllWithName(L"MasterHelper.exe", false);
	KillAllWithName(L"GATESRV.exe", false);

	// 步骤3：停 + 删除服务
	SC_HANDLE sc = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (sc) {
		SC_HANDLE hSvc = OpenServiceW(sc, L"TDNetFilter", SERVICE_STOP | DELETE);
		if (hSvc) {
			SERVICE_STATUS ss = {};
			ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
			DeleteService(hSvc);
			CloseServiceHandle(hSvc);
			ok = true;
		}
		CloseServiceHandle(sc);
	}
	return ok;
}

// 解除USB限制
bool RemoveUSBRestrictions() {
	// 停 + 删除 TDFileFilter 服务
	SC_HANDLE sc = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (!sc) return false;
	SC_HANDLE hSvc = OpenServiceW(sc, L"TDFileFilter", SERVICE_STOP | DELETE);
	if (!hSvc) { CloseServiceHandle(sc); return false; }
	SERVICE_STATUS ss = {};
	ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
	bool ok = (DeleteService(hSvc) == TRUE);
	CloseServiceHandle(hSvc);
	CloseServiceHandle(sc);
	return ok;
}

// ============================================================
//  极域密码读取 (XOR 解密)
// ============================================================
bool GetMythwarePassword(std::wstring& outPassword) {
	HKEY hk;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
		L"SOFTWARE\\TopDomain\\e-Learning Class\\Student",
		0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hk) != ERROR_SUCCESS)
		return false;

	DWORD nSize = 0;
	RegQueryValueExW(hk, L"knock1", nullptr, nullptr, nullptr, &nSize);
	if (nSize == 0) { RegCloseKey(hk); return false; }

	std::vector<BYTE> buf(nSize);
	if (RegQueryValueExW(hk, L"knock1", nullptr, nullptr, buf.data(), &nSize) != ERROR_SUCCESS) {
		RegCloseKey(hk); return false;
	}
	RegCloseKey(hk);

	// XOR 解密
	for (size_t i = 0; i + 3 < buf.size(); i += 4) {
		buf[i + 0] = buf[i + 0] ^ 0x50 ^ 0x45;
		buf[i + 1] = buf[i + 1] ^ 0x43 ^ 0x4c;
		buf[i + 2] = buf[i + 2] ^ 0x4c ^ 0x43;
		buf[i + 3] = buf[i + 3] ^ 0x45 ^ 0x50;
	}

	// 提取：每隔一个字节取一个（UTF-16 → 8bit 交替）
	std::string pass;
	for (size_t i = 0; i + 1 < buf.size(); i += 2) {
		if (buf[i] == 0) break;
		pass += (char)buf[i];
	}
	// 转 wstring
	outPassword.assign(pass.begin(), pass.end());
	return true;
}

} // namespace mythware_ext
