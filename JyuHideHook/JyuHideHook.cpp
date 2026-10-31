// JyuHideHook.cpp - 跨进程窗口隐蔽 DLL
// ============================================================
// 融合: MythwareHacker (window_hide) + Injector-cap (InjectDLL)
//
// 导出函数 (5 + SetFlags):
//   RemoteCloakWindow      → DWMWA_CLOAK 穿透
//   RemoteSetAffinity      → SetWindowDisplayAffinity WDA_EXCLUDEFROMCAPTURE
//   RemoteMoveOffscreen    → SetWindowPos 屏幕外
//   RemoteSetLayeredAlpha  → WS_EX_LAYERED + SetLayeredWindowAttributes
//   RemoteIsCloaked        → DwmGetWindowAttribute cloaked 状态查询
//   SetFlags               → [Injector-cap] 远程配置 topmost + anticapture
//
// DllMain: WorkerThread 解析命令行参数 (-notopmost -nocapture) 自动应用
//
// 编译: g++ -DBUILD_DLL -shared JyuHideHook.cpp -O2 -s -luser32 -ldwmapi
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <string.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 1
#endif
#ifndef DWMWA_CLOAK
#define DWMWA_CLOAK 9
#endif

typedef BOOL    (WINAPI *SetWindowDisplayAffinity_t)(HWND, DWORD);
typedef HRESULT (WINAPI *DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);
typedef HRESULT (WINAPI *DwmGetWindowAttribute_t)(HWND, DWORD, PVOID, DWORD);
typedef BOOL    (WINAPI *SetLayeredWindowAttributes_t)(HWND, COLORREF, BYTE, DWORD);

// ====== 全局配置 (Injector-cap InjectDLL 风格) ======
struct InjectorConfig {
    BOOL enableTopMost;     // TRUE 置顶
    BOOL enableAntiCapture; // TRUE 排除截屏 (WDA_EXCLUDEFROMCAPTURE), FALSE 可监视 (WDA_MONITOR)
} g_config = { FALSE, TRUE };

// ====== PID → 主窗口 (Injector-cap GetMainWindow) ======
static HWND g_targetMainWnd = nullptr;

static BOOL CALLBACK FindMainWndProc(HWND hWnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == (DWORD)lParam && GetWindow(hWnd, GW_OWNER) == NULL && IsWindowVisible(hWnd)) {
        g_targetMainWnd = hWnd;
        return FALSE;
    }
    return TRUE;
}

static HWND GetMainWindowByPid(DWORD pid) {
    g_targetMainWnd = nullptr;
    EnumWindows(FindMainWndProc, (LPARAM)pid);
    return g_targetMainWnd;
}

// ====== ApplySettings (Injector-cap 核心逻辑) ======
static void ApplySettings() {
    HWND hwnd = GetMainWindowByPid(GetCurrentProcessId());
    if (!hwnd) return;

    // 1. 置顶
    if (g_config.enableTopMost)
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    else
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // 2. 防截屏 (WDA_EXCLUDEFROMCAPTURE) / 允许监视 (WDA_MONITOR)
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (!hUser32) return;
    auto pSWDA = (SetWindowDisplayAffinity_t)GetProcAddress(hUser32, "SetWindowDisplayAffinity");
    if (!pSWDA) return;
    if (g_config.enableAntiCapture)
        pSWDA(hwnd, WDA_EXCLUDEFROMCAPTURE); // 教师截屏时此窗口不显示
    else
        pSWDA(hwnd, WDA_MONITOR);            // 允许教师监视 (默认)
}

// ============================================================
// 5 个 MythwareHacker 风格导出 (被 window_hide.cpp 的 InjectAndCall 使用)
// ============================================================
struct RemoteAffinityParams    { HWND hwnd; DWORD affinity; };
struct RemoteCloakParams      { HWND hwnd; BOOL cloak; };
struct RemoteMoveParams       { HWND hwnd; int x; int y; int w; int h; };
struct RemoteLayeredAlphaParams { HWND hwnd; BYTE alpha; };

extern "C" __declspec(dllexport) DWORD WINAPI RemoteSetAffinity(LPVOID lpParam) {
    auto* p = (RemoteAffinityParams*)lpParam;
    if (!p) return 1;
    HMODULE h = GetModuleHandleW(L"user32.dll");
    if (!h) return 4;
    auto f = (SetWindowDisplayAffinity_t)GetProcAddress(h, "SetWindowDisplayAffinity");
    if (!f) return 3;
    return f(p->hwnd, p->affinity) ? 0 : 2;
}

extern "C" __declspec(dllexport) DWORD WINAPI RemoteCloakWindow(LPVOID lpParam) {
    auto* p = (RemoteCloakParams*)lpParam;
    if (!p) return 1;
    HMODULE h = LoadLibraryW(L"dwmapi.dll");
    if (!h) return 4;
    auto f = (DwmSetWindowAttribute_t)GetProcAddress(h, "DwmSetWindowAttribute");
    if (!f) { FreeLibrary(h); return 3; }
    HRESULT hr = f(p->hwnd, DWMWA_CLOAK, &p->cloak, sizeof(p->cloak));
    FreeLibrary(h);
    return SUCCEEDED(hr) ? 0 : 2;
}

extern "C" __declspec(dllexport) DWORD WINAPI RemoteMoveOffscreen(LPVOID lpParam) {
    auto* p = (RemoteMoveParams*)lpParam;
    if (!p || !p->hwnd || !IsWindow(p->hwnd)) return 3;
    return SetWindowPos(p->hwnd, nullptr, p->x, p->y, p->w, p->h,
                         SWP_NOZORDER | SWP_NOACTIVATE) ? 0 : 2;
}

extern "C" __declspec(dllexport) DWORD WINAPI RemoteSetLayeredAlpha(LPVOID lpParam) {
    auto* p = (RemoteLayeredAlphaParams*)lpParam;
    if (!p) return 1;
    HMODULE h = GetModuleHandleW(L"user32.dll");
    if (!h) return 4;
    LONG ex = GetWindowLongW(p->hwnd, GWL_EXSTYLE);
    SetWindowLongW(p->hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    auto f = (SetLayeredWindowAttributes_t)GetProcAddress(h, "SetLayeredWindowAttributes");
    if (f) f(p->hwnd, 0, p->alpha, LWA_ALPHA);
    return 0;
}

extern "C" __declspec(dllexport) DWORD WINAPI RemoteIsCloaked(LPVOID lpParam) {
    HWND hwnd = nullptr;
    if (lpParam) memcpy(&hwnd, lpParam, sizeof(HWND));
    if (!hwnd || !IsWindow(hwnd)) return 3;
    HMODULE h = LoadLibraryW(L"dwmapi.dll");
    if (!h) return 4;
    auto f = (DwmGetWindowAttribute_t)GetProcAddress(h, "DwmGetWindowAttribute");
    if (!f) { FreeLibrary(h); return 5; }
    BOOL cloaked = FALSE;
    HRESULT hr = f(hwnd, 14, &cloaked, sizeof(cloaked));
    FreeLibrary(h);
    if (FAILED(hr)) return 6;
    return cloaked ? 0 : 1;
}

// ============================================================
// SetFlags (Injector-cap 风格) — 被 Injector.cpp 远程调用
// struct Flags { BOOL topmost; BOOL anticapture; };
//   topmost = TRUE  → HWND_TOPMOST
//   anticapture = TRUE  → WDA_EXCLUDEFROMCAPTURE (教师看不到)
//   anticapture = FALSE → WDA_MONITOR (允许教师监视)
// ============================================================
struct Flags { BOOL topmost; BOOL anticapture; };

extern "C" __declspec(dllexport) DWORD WINAPI SetFlags(LPVOID lpParam) {
    if (!lpParam) return 0;
    Flags f;
    memcpy(&f, lpParam, sizeof(f));
    g_config.enableTopMost     = f.topmost != 0;
    g_config.enableAntiCapture = f.anticapture != 0;
    ApplySettings();
    return 1;
}

// ============================================================
// WorkerThread (Injector-cap DllMain 风格)
// ============================================================
static DWORD WINAPI WorkerThread(LPVOID) {
    // 解析命令行参数 (主程序可能 CreateProcess 时加了参数)
    LPWSTR cmdLine = GetCommandLineW();
    if (cmdLine) {
        if (wcsstr(cmdLine, L"-topmost"))  g_config.enableTopMost = TRUE;
        if (wcsstr(cmdLine, L"-notopmost")) g_config.enableTopMost = FALSE;
        if (wcsstr(cmdLine, L"-anticapture"))  g_config.enableAntiCapture = TRUE;
        if (wcsstr(cmdLine, L"-nocapture"))    g_config.enableAntiCapture = FALSE;
    }
    ApplySettings();
    return 0;
}

// ============================================================
// DllMain — DLL 加载到 StudentMain.exe 时自动跑 WorkerThread
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
