// stdafx.h: 标准系统包含文件的包含文件，
// 或是经常使用但不常更改的
// 项目特定的包含文件
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>

#include "../../cmake/msvc_compat.h"  // GCC/MSVC compat

// C 运行时头文件
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>

#ifdef JTUPDATE_EXPORT
#define UPEXPORT __declspec(dllexport)
#else
// Static library or import: no linkage spec needed
#define UPEXPORT
#endif

// Note: EXTERN_C is extern "C" on GCC; we must NOT add another 'extern' inside it.
// So UPEXPORT_CFUNC expands to e.g. 'extern "C" BOOL funcname()' — just EXTERN_C + type + name.
#define UPEXPORT_CFUNC(x) EXTERN_C x 


// 在此处引用程序需要的其他标头

