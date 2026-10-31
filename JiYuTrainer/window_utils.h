// window_utils.h - 窗口工具 (MythwareHacker 移植)
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace wutil {

struct WindowInfo {
    HWND hwnd;
    std::wstring title;
    std::wstring processName;
    bool hidden;
};

std::wstring GetWindowTitle(HWND hwnd);
std::wstring GetProcessName(HWND hwnd);
DWORD GetProcessIdFromHwnd(HWND hwnd);
BOOL IsWindowEligible(HWND hwnd);
bool IsPointOnTitleBar(POINT pt, HWND* outHwnd);
std::vector<WindowInfo> EnumAllTopLevelWindows();
std::vector<HWND> FindWindowsByProcessName(const std::wstring& processName);
HWND FindWindowByTitle(const std::wstring& title);

} // namespace wutil
