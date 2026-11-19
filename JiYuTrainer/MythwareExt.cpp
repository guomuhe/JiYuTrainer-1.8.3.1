// MythwareExt.cpp - 从 MythwareToolkit 移植的高级功能
// ACL 防杀 / CBT hook 对话框防截屏 / 键鼠解锁 / 极域控制 / 网络限制解除
#include "MythwareExt.h"
#include "NTDExt.h"
#include <aclapi.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>

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
//  进程白名单（防止误杀系统关键进程）
// ============================================================
static const wchar_t* g_protectedNames[] = {
	L"System", L"Idle", L"smss.exe", L"csrss.exe", L"wininit.exe",
	L"winlogon.exe", L"services.exe", L"lsass.exe", L"lsaiso.exe",
	L"svchost.exe", L"dwm.exe", L"explorer.exe", L"fontdrvhost.exe",
	L"spoolsv.exe", L"audiodg.exe", L"taskhostw.exe", L"sihost.exe",
	L"RuntimeBroker.exe", L"SearchHost.exe", L"StartMenuExperienceHost.exe",
	L"TextInputHost.exe", L"ctfmon.exe", L"SecurityHealthService.exe",
	L"MsMpEng.exe", L"JiYuTrainer.exe", nullptr
};

bool IsProtectedProcessName(const wchar_t* name) {
	if (!name || !*name) return false;
	for (int i = 0; g_protectedNames[i]; i++)
		if (_wcsicmp(g_protectedNames[i], name) == 0) return true;
	return false;
}

static std::wstring ProcessImageName(DWORD pid) {
	wchar_t buf[MAX_PATH] = {};
	DWORD size = MAX_PATH;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h) return L"";
	QueryFullProcessImageNameW(h, 0, buf, &size);
	CloseHandle(h);
	std::wstring s(buf);
	size_t p = s.find_last_of(L"\\/");
	return p == std::wstring::npos ? s : s.substr(p + 1);
}

static bool IsProtectedPid(DWORD pid) {
	return IsProtectedProcessName(ProcessImageName(pid).c_str());
}

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
	if (IsProtectedPid(pid)) return false;   // 白名单：系统关键进程/自身不杀
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
	if (IsProtectedProcessName(name)) return false;   // 白名单
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

bool ApplyBroadcastPolicy(bool windowed, bool* found, bool* changed) {
	HWND hBd = FindBroadcastWindow();
	if (found) *found = (hBd != nullptr);
	if (changed) *changed = false;
	if (!hBd) return false;
	RECT r = {};
	GetWindowRect(hBd, &r);
	int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
	bool isFull = (r.left <= 0 && r.top <= 0 && r.right >= sw && r.bottom >= sh);
	if (isFull == windowed) {
		// 当前全屏却要求窗口化，或当前窗口化却要求全屏 → 点击切换按钮
		PostMessageW(hBd, WM_COMMAND, MAKEWPARAM(1004, BM_CLICK), 0);
		if (changed) *changed = true;
	}
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

// ============================================================
//  教师端通信阻断（仅本机端口规则）
// ============================================================
static std::wstring System32Path(const wchar_t* fileName) {
	wchar_t dir[MAX_PATH] = {};
	GetSystemDirectoryW(dir, MAX_PATH);
	std::wstring p = dir;
	if (!p.empty() && p.back() != L'\\') p += L'\\';
	p += fileName;
	return p;
}

static bool RunHiddenWait(const std::wstring& exe, const std::wstring& args, DWORD* exitCode = nullptr) {
	std::vector<wchar_t> cmd(args.begin(), args.end());
	cmd.push_back(0);
	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE,
	                    CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		return false;
	WaitForSingleObject(pi.hProcess, 20000);
	DWORD rc = 1;
	GetExitCodeProcess(pi.hProcess, &rc);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	if (exitCode) *exitCode = rc;
	return rc == 0;
}

bool SetTeacherCommBlock(bool enable) {
	const wchar_t* kRule = L"JiYuTrainer_BlockTeacherUDP";
	std::wstring args;
	if (enable) {
		args = std::wstring(L"advfirewall firewall add rule name=\"") + kRule +
		       L"\" dir=out protocol=UDP localport=4700-4720 action=block profile=any";
	} else {
		args = std::wstring(L"advfirewall firewall delete rule name=\"") + kRule + L"\"";
	}
	return RunHiddenWait(System32Path(L"netsh.exe"), args);
}

// ============================================================
//  能力自检 / 诊断包
// ============================================================
static bool TokenIsElevated() {
	HANDLE hToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) return false;
	TOKEN_ELEVATION te = {};
	DWORD sz = sizeof(te);
	BOOL ok = GetTokenInformation(hToken, TokenElevation, &te, sz, &sz);
	CloseHandle(hToken);
	return ok && te.TokenIsElevated;
}

static bool OsIs64Bit() {
	SYSTEM_INFO si = {};
	GetNativeSystemInfo(&si);
	return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64;
}

static bool PidIs64Bit(DWORD pid) {
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h) return false;
	BOOL wow = FALSE;
	BOOL ok = IsWow64Process(h, &wow);
	CloseHandle(h);
	return ok && OsIs64Bit() && !wow;
}

static std::wstring ServiceStateText(const wchar_t* name) {
	SC_HANDLE sc = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (!sc) return L"无法访问服务管理器";
	SC_HANDLE svc = OpenServiceW(sc, name, SERVICE_QUERY_STATUS);
	std::wstring r;
	if (!svc) {
		r = (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) ? L"未安装" : L"打开失败";
	} else {
		SERVICE_STATUS ss = {};
		if (QueryServiceStatus(svc, &ss)) {
			switch (ss.dwCurrentState) {
			case SERVICE_RUNNING: r = L"运行中"; break;
			case SERVICE_STOPPED: r = L"已停止"; break;
			case SERVICE_START_PENDING: r = L"正在启动"; break;
			case SERVICE_STOP_PENDING: r = L"正在停止"; break;
			default: r = L"其他状态"; break;
			}
		} else r = L"查询失败";
		CloseServiceHandle(svc);
	}
	CloseServiceHandle(sc);
	return r;
}

static std::wstring SelfDir() {
	wchar_t p[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, p, MAX_PATH);
	std::wstring s(p);
	size_t sep = s.find_last_of(L'\\');
	return sep == std::wstring::npos ? std::wstring() : s.substr(0, sep + 1);
}

static std::wstring FilePresence(const std::wstring& dir, const wchar_t* name) {
	if (GetFileAttributesW((dir + name).c_str()) == INVALID_FILE_ATTRIBUTES) return L"缺失";
	return L"存在";
}

static void Rep(std::wstring& s, const wchar_t* fmt, ...) {
	wchar_t buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vswprintf_s(buf, 2048, fmt, ap);
	va_end(ap);
	s += buf;
}

// 以 UTF-8(含 BOM) 写文本文件，避免依赖 _wfopen_s 的 ccs 扩展模式
static bool WriteTextUtf8(const std::wstring& path, const std::wstring& text) {
	HANDLE hf = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
	                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hf == INVALID_HANDLE_VALUE) return false;
	const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
	DWORD written = 0;
	WriteFile(hf, bom, 3, &written, nullptr);
	int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
	                               nullptr, 0, nullptr, nullptr);
	if (need > 0) {
		std::string utf8(need, '\0');
		WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
		                    &utf8[0], need, nullptr, nullptr);
		WriteFile(hf, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
	}
	CloseHandle(hf);
	return true;
}

std::wstring BuildCapabilityReport() {
	std::wstring r;
	SYSTEMTIME st = {};
	GetLocalTime(&st);
	Rep(r, L"JiYuTrainer 能力自检报告\r\n");
	Rep(r, L"时间: %04d-%02d-%02d %02d:%02d:%02d\r\n\r\n",
	    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	Rep(r, L"[运行环境]\r\n");
	Rep(r, L"  运行身份: %s\r\n", TokenIsElevated() ? L"管理员（已提权）" : L"普通用户（未提权，注入/驱动会失败）");
	Rep(r, L"  系统位数: %s；本程序位数: %s\r\n",
	    OsIs64Bit() ? L"64 位" : L"32 位", sizeof(void*) == 8 ? L"64 位" : L"32 位");

	Rep(r, L"\r\n[极域学生端]\r\n");
	DWORD pid = FindProcessByName(L"StudentMain.exe");
	if (!pid) pid = FindProcessByName(L"StudentM.exe");
	if (!pid) pid = FindProcessByName(L"StudentMain64.exe");
	if (pid) {
		Rep(r, L"  进程: 运行中  PID=%lu  镜像=%s  位数=%s\r\n", pid,
		    ProcessImageName(pid).c_str(), PidIs64Bit(pid) ? L"64 位" : L"32 位");
		if (PidIs64Bit(pid) && sizeof(void*) == 4)
			Rep(r, L"  注入: 需要 JyuInject64.exe（32 位主程序无法直接注入 64 位目标）\r\n");
	} else {
		Rep(r, L"  进程: 未运行\r\n");
	}

	Rep(r, L"\r\n[注入依赖]\r\n");
	std::wstring dir = SelfDir();
	Rep(r, L"  JyuHideHook.dll      : %s\r\n", FilePresence(dir, L"JyuHideHook.dll").c_str());
	Rep(r, L"  JyuHideHook64.dll    : %s\r\n", FilePresence(dir, L"JyuHideHook64.dll").c_str());
	Rep(r, L"  JyuInject64.exe      : %s\r\n", FilePresence(dir, L"JyuInject64.exe").c_str());
	Rep(r, L"  KernelCallbackCleaner: %s\r\n", FilePresence(dir, L"KernelCallbackCleaner.exe").c_str());
	Rep(r, L"  RTCore64.sys         : %s\r\n", FilePresence(dir, L"RTCore64.sys").c_str());
	Rep(r, L"  JiYuTrainerDriver.sys: %s\r\n", FilePresence(dir, L"JiYuTrainerDriver.sys").c_str());

	Rep(r, L"\r\n[内核与限制]\r\n");
	Rep(r, L"  服务 TDNetFilter : %s\r\n", ServiceStateText(L"TDNetFilter").c_str());
	Rep(r, L"  服务 TDFileFilter: %s\r\n", ServiceStateText(L"TDFileFilter").c_str());
	HANDLE hDev = CreateFileW(L"\\\\.\\TDNetFilter", GENERIC_READ | GENERIC_WRITE,
	                          FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDev != INVALID_HANDLE_VALUE) { Rep(r, L"  TDNetFilter 设备: 可访问\r\n"); CloseHandle(hDev); }
	else Rep(r, L"  TDNetFilter 设备: 不可访问 (err=%lu)\r\n", GetLastError());

	Rep(r, L"\r\n[本程序状态]\r\n");
	Rep(r, L"  键鼠解锁线程: %s\r\n", IsInputUnlockRunning() ? L"运行中" : L"未启动");
	Rep(r, L"  进程防杀(ACL): %s\r\n", g_bProcessProtected ? L"已开启" : L"未开启");
	Rep(r, L"  教师端通信阻断: %s\r\n", L"可在扩展区手动开关（netsh 本机端口规则）");
	return r;
}

bool WriteSelfCheckReport(std::wstring* outPath) {
	wchar_t tmp[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tmp);
	std::wstring p = std::wstring(tmp) + L"JiYuTrainer-selfcheck.txt";
	if (!WriteTextUtf8(p, BuildCapabilityReport())) return false;
	if (outPath) *outPath = p;
	return true;
}

bool ExportDiagnosticBundle(std::wstring& outDirPath) {
	SYSTEMTIME st = {};
	GetLocalTime(&st);
	wchar_t tmp[MAX_PATH] = {};
	GetTempPathW(MAX_PATH, tmp);
	wchar_t dir[MAX_PATH] = {};
	swprintf_s(dir, L"%sJiYuTrainer-diag-%04d%02d%02d-%02d%02d%02d",
	           tmp, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
	if (!CreateDirectoryW(dir, nullptr)) return false;
	outDirPath = dir;

	// 1) 能力报告
	WriteTextUtf8(outDirPath + L"\\capability.txt", BuildCapabilityReport());

	// 2) 应用日志（程序目录 + 临时目录中的 *.log）
	auto copyLogs = [&](const std::wstring& srcDir) {
		WIN32_FIND_DATAW fd = {};
		HANDLE hFind = FindFirstFileW((srcDir + L"*.log").c_str(), &fd);
		if (hFind == INVALID_HANDLE_VALUE) return;
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
			CopyFileW((srcDir + fd.cFileName).c_str(),
			          (outDirPath + L"\\" + fd.cFileName).c_str(), FALSE);
		} while (FindNextFileW(hFind, &fd));
		FindClose(hFind);
	};
	copyLogs(SelfDir());
	copyLogs(std::wstring(tmp));

	// 3) 注册表备份（IFEO 与策略分支）
	RunHiddenWait(System32Path(L"reg.exe"),
		L"export \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\" \"" +
		outDirPath + L"\\ifeo.reg\" /y");
	RunHiddenWait(System32Path(L"reg.exe"),
		L"export \"HKLM\\SOFTWARE\\Policies\" \"" + outDirPath + L"\\policies.reg\" /y");

	// 4) 服务状态快照
	std::wstring svc = L"TDNetFilter=" + ServiceStateText(L"TDNetFilter") +
	                   L"\r\nTDFileFilter=" + ServiceStateText(L"TDFileFilter") + L"\r\n";
	WriteTextUtf8(outDirPath + L"\\services.txt", svc);
	return true;
}

// ============================================================
//  一键脱控 / 一键恢复
// ============================================================
static std::wstring JiyuStudentDir() {
	DWORD pid = FindMythwarePid();
	if (!pid) return L"";
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!h) return L"";
	wchar_t buf[MAX_PATH] = {};
	DWORD size = MAX_PATH;
	QueryFullProcessImageNameW(h, 0, buf, &size);
	CloseHandle(h);
	std::wstring s(buf);
	size_t sep = s.find_last_of(L'\\');
	return sep == std::wstring::npos ? std::wstring() : s.substr(0, sep);
}

static void AddStep(std::wstring& log, const wchar_t* label, bool ok) {
	log += ok ? L"[成功] " : L"[失败] ";
	log += label;
	log += L"\r\n";
}

bool DetachAll(std::wstring* report) {
	std::wstring log;
	std::wstring jiyuDir = JiyuStudentDir();
	bool allOk = true;

	bool r = NTDExt::ClearKeyboardAndMouseLock();
	AddStep(log, L"解除键盘鼠标锁定", r); allOk = allOk && r;

	StartInputUnlock();
	r = IsInputUnlockRunning();
	AddStep(log, L"启动键鼠解锁线程", r); allOk = allOk && r;

	if (jiyuDir.empty()) {
		AddStep(log, L"脱离远程关机（未找到极域目录，跳过）", false); allOk = false;
	} else {
		r = NTDExt::ToggleNoShutdown(jiyuDir.c_str(), true);
		AddStep(log, L"脱离远程关机", r); allOk = allOk && r;
	}

	r = NTDExt::ToggleUSBDeviceLimit(true);
	AddStep(log, L"解除 U 盘/软件限制", r); allOk = allOk && r;

	r = NTDExt::ToggleWebsiteUnblock(true);
	AddStep(log, L"解除网站限制", r); allOk = allOk && r;

	r = NTDExt::ClearImageFileExecutionOptions();
	AddStep(log, L"解除映像劫持", r); allOk = allOk && r;

	r = NTDExt::ClearGroupPolicyRestrictions();
	AddStep(log, L"解除组策略限制", r); allOk = allOk && r;

	r = NTDExt::UnlockJiyuAntiKill();
	AddStep(log, L"解除极域防杀钩子", r); allOk = allOk && r;

	if (!IsProcessProtected()) ToggleProcessProtection();
	r = IsProcessProtected();
	AddStep(log, L"开启进程防杀(ACL)", r); allOk = allOk && r;

	InstallDialogProtection();
	AddStep(log, L"开启对话框防截屏", true);

	if (report) *report = log;
	return allOk;
}

bool RestoreAll(std::wstring* report) {
	std::wstring log;
	std::wstring jiyuDir = JiyuStudentDir();
	bool allOk = true;

	bool r = true;
	if (!jiyuDir.empty())
		r = NTDExt::ToggleNoShutdown(jiyuDir.c_str(), false);
	else
		r = false;
	AddStep(log, L"恢复远程关机", r); allOk = allOk && r;

	r = NTDExt::ToggleUSBDeviceLimit(false);
	AddStep(log, L"恢复 U 盘/软件限制", r); allOk = allOk && r;

	r = NTDExt::ToggleWebsiteUnblock(false);
	AddStep(log, L"恢复网站限制", r); allOk = allOk && r;

	StopInputUnlock();
	AddStep(log, L"停止键鼠解锁线程", !IsInputUnlockRunning());

	if (IsProcessProtected()) ToggleProcessProtection();
	AddStep(log, L"关闭进程防杀(ACL)", !IsProcessProtected());

	UninstallDialogProtection();
	AddStep(log, L"关闭对话框防截屏", true);

	r = SetTeacherCommBlock(false);
	AddStep(log, L"关闭教师端通信阻断", r); allOk = allOk && r;

	if (report) *report = log;
	return allOk;
}

} // namespace mythware_ext
