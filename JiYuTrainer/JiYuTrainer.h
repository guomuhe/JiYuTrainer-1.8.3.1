#pragma once

// 64 位 JyuHideHook64.dll 的内嵌资源 ID（JiYuTrainer.rc 中以数字 206 引用）
#ifndef IDR_DLL_HIDE_HOOK64
#define IDR_DLL_HIDE_HOOK64 206
#endif

EXPORT_CFUNC(LONG) WINAPI AppUnhandledExceptionFilter(PEXCEPTION_POINTERS pExInfo);
EXPORT_CFUNC(VOID) ShowAppStartupFail(int rs);
EXPORT_CFUNC(void*) GetApp();
EXPORT_CFUNC(void) ForceExit();