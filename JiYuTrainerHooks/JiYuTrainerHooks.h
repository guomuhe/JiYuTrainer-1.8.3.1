#pragma once
#include "stdafx.h"
#include <unknwn.h>
#include <ShellAPI.h>

enum InjectionMode {
	InjectionModeProtect,
	InjectionModeHook,
	InjectionModeMaster,
	InjectionModeReplaceStub
};

EXTERN_C void __declspec(dllexport) VUnloadAll();
EXTERN_C void __declspec(dllexport) VLoad();
void VRunMain();

void VLoadRealHookDll();
void VLoadMainProtect();
void VParamInit();

void VCreateMsgCenter();
DWORD WINAPI VMsgCenterRunThread(LPVOID lpThreadParameter);
void VCloseMsgCenter();

void VHandleMsg(LPWSTR buff);
void VOutPutStatus(const wchar_t * str, ...);
void VHookFWindow(const wchar_t * hWndStr);

void VHookWindow(const wchar_t * hWndStr);
void VFixGuangBoWindow(HWND hWnd);
bool VIsInIllegalWindows(HWND hWnd);
void VBoom();
void VSendMessageBack(LPCWSTR buff, HWND hDlg);
void VManualQuit();

BOOL VShowOpConfirmDialog(LPCWSTR file, LPCWSTR pararm);

void VLoadOpWhiteList();
void VAddOpToWhiteList(const wchar_t * cmd);
bool VIsOpInWhiteList(const wchar_t * cmd);

BOOL VShowOutOfControlDlg();

void VInstallHooks(InjectionMode mode);
void VUnInstallHooks();

void VOpenFuckDrivers();
void VCloseFuckDrivers();

void VUnHookKeyBoard();

void VGetStudentainVersion();

EXTERN_C BOOL VGetExeInfo(LPWSTR strFilePath, LPCWSTR InfoItem, LPWSTR str, int maxCount);

INT_PTR CALLBACK FakeDesktopWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK MainWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK JiYuWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK JiYuTDDeskWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

typedef HHOOK(*fnDoneHook)();
typedef int(*fnUnHookLocalInput)(DWORD *bytesReturn);
typedef HMODULE(*fnUnLockLocalInput)();
typedef BOOL (WINAPI *fnSetForegroundWindow)(HWND hWnd);
typedef BOOL (WINAPI *fnSetWindowPos)(HWND hWnd, HWND hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags);
typedef BOOL (WINAPI *fnMoveWindow)(HWND hWnd, int X, int Y, int nWidth, int nHeight, BOOL bRepaint);
typedef BOOL (WINAPI *fnBringWindowToTop)(HWND hWnd); 
typedef BOOL (WINAPI *fnDeviceIoControl)(
	HANDLE hDevice,
	DWORD dwIoControlCode,
	LPVOID lpInBuffer,
	DWORD nInBufferSize,
	LPVOID lpOutBuffer,
	DWORD nOutBufferSize,
	LPDWORD lpBytesReturned,
	LPOVERLAPPED lpOverlapped
);
typedef HANDLE (WINAPI *fnCreateFileA)(
	LPCSTR lpFileName,
	DWORD dwDesiredAccess,
	DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes,
	HANDLE hTemplateFile
);
typedef HANDLE(WINAPI *fnCreateFileW)(
	LPCWSTR lpFileName,
	DWORD dwDesiredAccess,
	DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes,
	HANDLE hTemplateFile
);
typedef HHOOK (WINAPI *fnSetWindowsHookExA)(
	int idHook,
	HOOKPROC lpfn,
	HINSTANCE hmod,
	DWORD dwThreadId);
typedef HDWP (WINAPI *fnDeferWindowPos)(
	_In_ HDWP hWinPosInfo,
	_In_ HWND hWnd,
	_In_opt_ HWND hWndInsertAfter,
	_In_ int x,
	_In_ int y,
	_In_ int cx,
	_In_ int cy,
	_In_ UINT uFlags);
typedef VOID(WINAPI *fnmouse_event)(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo);
typedef UINT (WINAPI *fnSendInput)(
	UINT cInputs,                     // number of input in the array
	__in_ecount(cInputs) LPINPUT pInputs,  // array of inputs
	int cbSize);
typedef LONG (WINAPI  *fnSetWindowLongA)(
	HWND hWnd,
	int nIndex,
	LONG dwNewLong);
typedef LONG (WINAPI  *fnSetWindowLongW)(
	HWND hWnd,
	int nIndex,
	LONG dwNewLong);
typedef HRESULT(__cdecl *fnTDAjustCreateInstance)(CLSID *rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext, IID *riid, LPVOID *ppv);
typedef BOOL (WINAPI *fnShowWindow)(
	_In_ HWND hWnd,
	_In_ int nCmdShow);
typedef BOOL (WINAPI *fnExitWindowsEx)(
	UINT uFlags,
	DWORD dwReason);
typedef BOOL (WINAPI* fnCreateProcessW)(
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
);
typedef BOOL(WINAPI* fnCreateProcessA)(
	LPCSTR lpApplicationName,
	LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
);
typedef HRESULT (WINAPI *fnDwmEnableComposition)(UINT uCompositionAction);
typedef UINT (WINAPI*fnWinExec)(LPCSTR lpCmdLine,	UINT uCmdShow);
typedef LRESULT (WINAPI *fnCallNextHookEx)( HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);
typedef HWND (WINAPI* fnGetDesktopWindow)(VOID);
typedef HDC (WINAPI *fnGetWindowDC)(HWND hWnd);
typedef int(__cdecl *fnEncodeToJPEGBuffer)(int a1, int a2, int a3, int a4, int a5, DWORD *a6, int a7, int a8, int a9);
typedef HWND(WINAPI*fnGetForegroundWindow)(VOID);
typedef HDC (WINAPI *fnCreateDCW)(LPCWSTR pwszDriver, LPCWSTR pwszDevice, LPCWSTR pszPort, CONST DEVMODEW * pdm);
typedef BOOL(WINAPI *fnEnableMenuItem)(HMENU hMenu, UINT  uIDEnableItem, UINT  uEnable);
typedef DWORD (WINAPI *fnSetClassLongA)(HWND hWnd, int nIndex, LONG dwNewLong);
typedef DWORD (WINAPI *fnSetClassLongW)(HWND hWnd, int nIndex, LONG dwNewLong);
typedef BOOL (WINAPI *fnUnhookWindowsHookEx)(HHOOK hhk);
typedef BOOL (WINAPI *fnPostMessageW)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
typedef LRESULT (WINAPI *fnSendMessageW)(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
typedef BOOL (WINAPI *fnTerminateProcess)(HANDLE hProcess,	UINT uExitCode);
typedef HRESULT (WINAPI *fnFilterConnectCommunicationPort)(LPCWSTR lpPortName, DWORD dwOptions, LPCVOID lpContext, WORD wSizeOfContext, LPSECURITY_ATTRIBUTES lpSecurityAttributes, HANDLE *hPort);


//Hooks

BOOL WINAPI hkExitWindowsEx(UINT uFlags, DWORD dwReason);
BOOL WINAPI hkSetWindowPos(HWND hWnd, HWND hWndInsertAfter, int x, int y, int cx, int cy, UINT uFlags);
BOOL WINAPI hkMoveWindow(HWND hWnd, int x, int y, int cx, int cy, BOOL bRepaint);
BOOL WINAPI hkSetForegroundWindow(HWND hWnd);
BOOL WINAPI hkBringWindowToTop(_In_ HWND hWnd);
BOOL WINAPI hkDeviceIoControl(
	HANDLE hDevice,
	DWORD dwIoControlCode,
	LPVOID lpInBuffer,
	DWORD nInBufferSize,
	LPVOID lpOutBuffer,
	DWORD nOutBufferSize,
	LPDWORD lpBytesReturned,
	LPOVERLAPPED lpOverlapped
	);
HANDLE WINAPI hkCreateFileA(
	LPCSTR lpFileName,
	DWORD dwDesiredAccess,
	DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes,
	HANDLE hTemplateFile
);
HANDLE WINAPI hkCreateFileW(
	LPCWSTR lpFileName,
	DWORD dwDesiredAccess,
	DWORD dwShareMode,
	LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	DWORD dwCreationDisposition,
	DWORD dwFlagsAndAttributes,
	HANDLE hTemplateFile
);
HHOOK WINAPI hkSetWindowsHookExA(
	int idHook,
	HOOKPROC lpfn,
	HINSTANCE hmod,
	DWORD dwThreadId);
HDWP WINAPI hkDeferWindowPos(
	_In_ HDWP hWinPosInfo,
	_In_ HWND hWnd,
	_In_opt_ HWND hWndInsertAfter,
	_In_ int x,
	_In_ int y,
	_In_ int cx,
	_In_ int cy,
	_In_ UINT uFlags);
UINT WINAPI hkSendInput(
	UINT cInputs,                     // number of input in the array
	__in_ecount(cInputs) LPINPUT pInputs,  // array of inputs
	int cbSize);
VOID WINAPI hkmouse_event(DWORD dwFlags, DWORD dx, DWORD dy, DWORD dwData, ULONG_PTR dwExtraInfo);
LONG WINAPI hkChangeDisplaySettingsW(
	DEVMODEW* lpDevMode,
	DWORD dwFlags);
HDESK WINAPI hkOpenDesktopA(
	LPCSTR lpszDesktop,
	DWORD dwFlags,
	BOOL fInherit,
	ACCESS_MASK dwDesiredAccess);
HDESK WINAPI hkOpenInputDesktop(
	DWORD dwFlags,
	BOOL fInherit,
	ACCESS_MASK dwDesiredAccess);
LONG WINAPI hkSetWindowLongA(
	HWND hWnd,
	int nIndex,
	LONG dwNewLong);
LONG WINAPI hkSetWindowLongW(
	HWND hWnd,
	int nIndex,
	LONG dwNewLong);
BOOL WINAPI hkShowWindow(
	_In_ HWND hWnd,
	_In_ int nCmdShow);
HINSTANCE WINAPI hkShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile, LPCWSTR lpParameters,
	LPCWSTR lpDirectory, INT nShowCmd);
BOOL WINAPI hkShellExecuteExW(SHELLEXECUTEINFOW *pExecInfo);
BOOL WINAPI hkCreateProcessW(
	LPCWSTR lpApplicationName,
	LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
);
BOOL WINAPI hkCreateProcessA(
	LPCSTR lpApplicationName,
	LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation
	);
HRESULT WINAPI hkDwmEnableComposition(UINT uCompositionAction);
UINT WINAPI hkWinExec(LPCSTR lpCmdLine,	 UINT uCmdShow);
LRESULT WINAPI hkCallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam);
HWND WINAPI hkGetDesktopWindow(VOID);
HDC WINAPI hkGetWindowDC(HWND hWnd);
BOOL __cdecl hkEncodeToJPEGBuffer(int a1, int a2, int a3, int a4, int a5, DWORD *a6, int a7, int a8, int a9);
HWND WINAPI hkGetForegroundWindow(VOID);
HDC WINAPI hkCreateDCW(LPCWSTR pwszDriver, LPCWSTR pwszDevice, LPCWSTR pszPort, CONST DEVMODEW * pdm);
BOOL WINAPI hkEnableMenuItem(HMENU hMenu,	UINT  uIDEnableItem,	 UINT  uEnable);
DWORD WINAPI hkSetClassLongA(HWND hWnd, int nIndex,	LONG dwNewLong);
DWORD WINAPI hkSetClassLongW(HWND hWnd, int nIndex,	LONG dwNewLong);
BOOL WINAPI hkUnhookWindowsHookEx(HHOOK hhk);
BOOL WINAPI hkPostMessageW(	HWND hWnd,	UINT Msg,	WPARAM wParam,	LPARAM lParam);
LRESULT WINAPI hkSendMessageW(	HWND hWnd,	UINT Msg,	WPARAM wParam,	LPARAM lParam);
BOOL WINAPI hkTerminateProcess(HANDLE hProcess,	UINT uExitCode);
HRESULT WINAPI hkFilterConnectCommunicationPort(LPCWSTR lpPortName,	DWORD dwOptions,	LPCVOID lpContext,	WORD wSizeOfContext,	LPSECURITY_ATTRIBUTES lpSecurityAttributes,	HANDLE *hPort);



INT_PTR CALLBACK VShowOpConfirmWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

typedef struct tag_WNDCOUNTDOWN {
	int count;
	HWND wnd;
}WNDCOUNTDOWN, *PWNDCOUNTDOWN;
typedef struct tag_WNDIJJ {
	HWND wnd;
	bool canSizeToFull;
}WNDIJJ,*PWNDIJJ;