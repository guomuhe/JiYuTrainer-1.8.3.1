// stdafx.h: 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 项目特定的包含文件
//

#pragma once

#include "targetver.h"

// Override SDKDDKVer.h defaults - we need Vista+ APIs (InetPton etc.)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#undef _NTDDI_VERSION
#define _NTDDI_VERSION 0x06010000
#undef _WIN32_IE
#define _WIN32_IE 0x0800

#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>

#include "../../cmake/msvc_compat.h"  // GCC/MSVC compat

#ifdef JTEXPORT
#define EXPORT
#else
#define EXPORT
#endif

#define EXPORT_CFUNC(x) EXTERN_C EXPORT x 
#define FAST_STR_BINDER(str, fstr, size, ...) WCHAR str[size]; swprintf_s(str, fstr, __VA_ARGS__)

// NOTE: sal_compat.h is NOT included here because it must come AFTER
// any C++ STL headers (like <string> which pulls in <tuple>) to avoid
// polluting MinGW's libstdc++ internals.
// It is included at the right place in Logger.h (after <list>).

// 在此处引用程序需要的其他标头

