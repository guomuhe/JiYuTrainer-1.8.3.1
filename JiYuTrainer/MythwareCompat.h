// MythwareCompat.h - MythwareHacker -> JiYuTrainer 兼容层
// 提供 logger shim + WSTR 宏 + 最小 common.h 等价
#pragma once
#include <windows.h>
#include <string>
#include "Logger.h"

// MythwareHacker 的 logger::Info/Warn/Error → JiYuTrainer Logger
namespace logger {
    inline void Info(const std::wstring& msg)  { static LoggerInternal g; g.LogInfo(msg.c_str()); }
    inline void Warn(const std::wstring& msg)  { static LoggerInternal g; g.LogWarn(msg.c_str()); }
    inline void Error(const std::wstring& msg) { static LoggerInternal g; g.LogError(msg.c_str()); }
}

// WSTR: 数字/常量 → std::wstring
#define WSTR(x) [&]{ std::wstring s; auto v=(x); s=std::to_wstring(v); return s; }()

// APP_NAME for self_protect
#define APP_NAME L"JiYuTrainer"

// common::IsWdaExcludeFromCaptureSupported (Win8+)
namespace common {
    inline bool IsWdaExcludeFromCaptureSupported() {
        DWORD v = GetVersion();
        return (LOBYTE(LOWORD(v)) > 6) ||
               (LOBYTE(LOWORD(v)) == 6 && HIBYTE(LOWORD(v)) >= 3);
    }
}
