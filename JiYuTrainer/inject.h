// inject.h - 跨进程 DLL 注入 + 远程调用 (MythwareHacker 移植, 简化版)
// 只支持同架构 (32→32 或 64→64). JiYuTrainer 32-bit → StudentMain.exe 32-bit 完全覆盖
#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace inject {

struct Result {
    bool success = false;
    DWORD exitCode = 0;
    std::wstring error;
};

// 核心：注入 DLL + 远程调用导出函数
//   hwnd       = 目标窗口 (自动 OpenProcess 该进程)
//   dllPath    = JyuHideHook.dll 的完整路径 (主程序同级目录)
//   funcName   = "RemoteCloakWindow", "RemoteSetAffinity", "RemoteMoveOffscreen" 等
//   params     = 指向参数 struct 的指针
//   paramsSize = 参数 struct 的 sizeof
Result InjectAndCall(HWND hwnd,
                     const std::wstring& dllPath,
                     const char* funcName,
                     const void* params,
                     size_t paramsSize);

// 只注入不调用
Result InjectDllOnly(DWORD pid, const std::wstring& dllPath);

// 卸载
Result UnInjectDll(DWORD pid, const std::wstring& moduleName);

// Injector-cap 风格: 加载 DLL + 远程调用 SetFlags(topmost, anticapture)
//   anticapture = TRUE  → WDA_EXCLUDEFROMCAPTURE (教师截屏看不到)
//   anticapture = FALSE → WDA_MONITOR (允许教师监视)
//   topmost = TRUE      → HWND_TOPMOST (极域广播窗口置顶)
//   importToolkitReg    → 置顶时自动导入 Toolkit_fix.reg 根证书
Result InjectAndConfigure(DWORD pid, const std::wstring& dllPath,
                          bool topmost, bool anticapture,
                          bool importToolkitReg = true);

} // namespace inject
