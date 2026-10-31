// window_utils.cpp - 窗口工具 (MythwareHacker 移植)
#include "window_utils.h"
#include <psapi.h>

namespace wutil {

std::wstring GetWindowTitle(HWND hwnd) {
    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 512);
    if (buf[0] == L'\0') return L"(无标题)";
    return std::wstring(buf);
}

std::wstring GetProcessName(HWND hwnd) {
    DWORD pid = GetProcessIdFromHwnd(hwnd);
    if (pid == 0) return L"";
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return L"";
    wchar_t name[MAX_PATH] = {};
    GetModuleBaseNameW(hProcess, nullptr, name, MAX_PATH);
    CloseHandle(hProcess);
    std::wstring result(name);
    size_t pos = result.find(L".exe");
    if (pos != std::wstring::npos) result = result.substr(0, pos);
    return result;
}

DWORD GetProcessIdFromHwnd(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid;
}

BOOL IsWindowEligible(HWND hwnd) {
    if (!IsWindow(hwnd)) return FALSE;
    if (!IsWindowVisible(hwnd)) return FALSE;
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (style & WS_CHILD) return FALSE;
    if (exStyle & WS_EX_TOOLWINDOW) return FALSE;
    if (exStyle & WS_EX_NOACTIVATE) return FALSE;
    RECT rc; GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w < 5 || h < 5) return FALSE;
    wchar_t className[256] = {};
    GetClassNameW(hwnd, className, 256);
    if (wcscmp(className, L"Progman") == 0 ||
        wcscmp(className, L"WorkerW") == 0 ||
        wcscmp(className, L"Shell_TrayWnd") == 0 ||
        wcscmp(className, L"Shell_SecondaryTrayWnd") == 0) return FALSE;
    return TRUE;
}

bool IsPointOnTitleBar(POINT pt, HWND* outHwnd) {
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd) return false;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    if (!root || !IsWindowVisible(root)) return false;
    DWORD_PTR result = 0;
    LRESULT hit = 0;
    if (SendMessageTimeoutW(root, WM_NCHITTEST, 0,
                           MAKELPARAM(pt.x, pt.y),
                           SMTO_BLOCK | SMTO_ABORTIFHUNG, 200, &result)) {
        hit = (LRESULT)result;
    }
    if (hit == HTCAPTION) { if (outHwnd) *outHwnd = root; return true; }
    RECT rc;
    if (!GetWindowRect(root, &rc)) return false;
    int tbh = GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME) +
              GetSystemMetrics(SM_CXPADDEDBORDER) * 2;
    if (tbh < 30) tbh = 30;
    if (pt.x >= rc.left && pt.x <= rc.right && pt.y >= rc.top && pt.y <= rc.top + tbh) {
        if (outHwnd) *outHwnd = root;
        return true;
    }
    return false;
}

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
    auto* windows = reinterpret_cast<std::vector<WindowInfo>*>(lParam);
    if (!IsWindowEligible(hwnd)) return TRUE;
    WindowInfo wi = {};
    wi.hwnd = hwnd;
    wi.title = GetWindowTitle(hwnd);
    wi.processName = GetProcessName(hwnd);
    wi.hidden = false;
    windows->push_back(wi);
    return TRUE;
}

std::vector<WindowInfo> EnumAllTopLevelWindows() {
    std::vector<WindowInfo> windows;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&windows));
    return windows;
}

std::vector<HWND> FindWindowsByProcessName(const std::wstring& processName) {
    std::vector<HWND> result;
    auto all = EnumAllTopLevelWindows();
    for (const auto& w : all)
        if (_wcsicmp(w.processName.c_str(), processName.c_str()) == 0)
            result.push_back(w.hwnd);
    return result;
}

HWND FindWindowByTitle(const std::wstring& title) { return FindWindowW(nullptr, title.c_str()); }

} // namespace wutil
