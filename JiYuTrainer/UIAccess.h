/**
 * UIAccess.h —— 置顶权限工具（从 pin.cpp 精简而来）
 *
 * 用法（在 wWinMain 最开头、创建任何窗口之前）：
 *   #include "UIAccess.h"
 *   int APIENTRY wWinMain(...) {
 *       if (UIAccess::Prepare()) return 0;   // 提权/重启后，本进程在此退出
 *       // ... 原有逻辑
 *   }
 *
 * 依赖库：advapi32.lib, shell32.lib, tlhelp32.lib
 */

#pragma once
#ifndef _JYU_UIAccess_H_
#define _JYU_UIAccess_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>

namespace UIAccess {

// ---------------- 管理员检测 / UAC 提权 ----------------

static inline BOOL IsRunAsAdmin() {
    HANDLE hToken;
    BOOL bAdmin = FALSE;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION te;
        DWORD dwRet;
        if (GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &dwRet))
            bAdmin = te.TokenIsElevated ? TRUE : FALSE;
        CloseHandle(hToken);
    }
    return bAdmin;
}

/** 非管理员 → 请求 UAC 提权，返回 TRUE 表示已发起提权，调用方应退出 */
static inline BOOL ElevateIfNeeded() {
    WCHAR szExe[MAX_PATH];
    DWORD dwLen;
    if (IsRunAsAdmin()) return FALSE;
    dwLen = GetModuleFileNameW(NULL, szExe, MAX_PATH);
    if (dwLen == 0 || dwLen == MAX_PATH) return FALSE;
    if (ShellExecuteW(NULL, L"runas", szExe, NULL, NULL, SW_SHOWNORMAL) > (HINSTANCE)32)
        return TRUE;
    return FALSE;
}

static inline DWORD EnableDebugPrivilege() {
    HANDLE hToken;
    DWORD dwErr = ERROR_SUCCESS;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return GetLastError();
    {
        TOKEN_PRIVILEGES tp;
        LUID luid;
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &luid)) {
            tp.Privileges[0].Luid = luid;
            if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL))
                dwErr = GetLastError();
            else if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
                dwErr = ERROR_NOT_ALL_ASSIGNED;
        } else {
            dwErr = GetLastError();
        }
    }
    CloseHandle(hToken);
    return dwErr;
}

// ---------------- UIAccess 令牌 ----------------

static inline DWORD DuplicateWinloginToken(DWORD dwSessionId, DWORD dwDesiredAccess, PHANDLE phToken) {
    DWORD dwErr = ERROR_NOT_FOUND;
    PRIVILEGE_SET ps;
    ps.PrivilegeCount = 1;
    ps.Control = PRIVILEGE_SET_ALL_NECESSARY;
    if (!LookupPrivilegeValueW(NULL, SE_TCB_NAME, &ps.Privilege[0].Luid))
        return GetLastError();

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return GetLastError();

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    for (BOOL bCont = Process32FirstW(hSnapshot, &pe); bCont; bCont = Process32NextW(hSnapshot, &pe)) {
        if (lstrcmpiW(pe.szExeFile, L"winlogon.exe") != 0) continue;
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!hProcess) continue;
        HANDLE hToken;
        if (OpenProcessToken(hProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &hToken)) {
            BOOL fTcb;
            DWORD dwRetLen, sid;
            if (PrivilegeCheck(hToken, &ps, &fTcb) && fTcb &&
                GetTokenInformation(hToken, TokenSessionId, &sid, sizeof(sid), &dwRetLen) &&
                sid == dwSessionId) {
                if (DuplicateTokenEx(hToken, dwDesiredAccess, NULL, SecurityImpersonation, TokenImpersonation, phToken))
                    dwErr = ERROR_SUCCESS;
                else
                    dwErr = GetLastError();
            }
            CloseHandle(hToken);
        }
        CloseHandle(hProcess);
        if (dwErr == ERROR_SUCCESS) break;
    }
    CloseHandle(hSnapshot);
    return dwErr;
}

static inline DWORD CreateUIAccessToken(PHANDLE phToken) {
    DWORD dwErr;
    HANDLE hTokenSelf;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &hTokenSelf))
        return GetLastError();
    DWORD dwSessionId, dwRetLen;
    if (GetTokenInformation(hTokenSelf, TokenSessionId, &dwSessionId, sizeof(dwSessionId), &dwRetLen)) {
        HANDLE hTokenSystem;
        dwErr = DuplicateWinloginToken(dwSessionId, TOKEN_IMPERSONATE, &hTokenSystem);
        if (dwErr == ERROR_SUCCESS) {
            if (SetThreadToken(NULL, hTokenSystem)) {
                if (DuplicateTokenEx(hTokenSelf,
                    TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT,
                    NULL, SecurityAnonymous, TokenPrimary, phToken)) {
                    BOOL bUIAccess = TRUE;
                    if (!SetTokenInformation(*phToken, TokenUIAccess, &bUIAccess, sizeof(bUIAccess))) {
                        dwErr = GetLastError();
                        CloseHandle(*phToken);
                    }
                } else {
                    dwErr = GetLastError();
                }
                RevertToSelf();
            } else {
                dwErr = GetLastError();
            }
            CloseHandle(hTokenSystem);
        }
    } else {
        dwErr = GetLastError();
    }
    CloseHandle(hTokenSelf);
    return dwErr;
}

static inline BOOL HasUIAccess(BOOL* pfUIAccess) {
    HANDLE hToken;
    BOOL ok = FALSE;
    DWORD fUI = 0;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        DWORD dwRetLen;
        if (GetTokenInformation(hToken, TokenUIAccess, &fUI, sizeof(fUI), &dwRetLen))
            ok = TRUE;
        CloseHandle(hToken);
    }
    if (ok) *pfUIAccess = (fUI != 0);
    return ok;
}

/**
 * 一站式入口：UAC 提权 → SeDebugPrivilege → UIAccess 令牌重启
 * 返回 TRUE 表示本进程应退出（已发起提权或 UIAccess 重启）
 * 返回 FALSE 表示当前进程已具备 UIAccess，可继续执行
 */
static inline BOOL Prepare() {
    // 1) 非管理员 → 发起 UAC 提权，退出
    if (ElevateIfNeeded()) return TRUE;

    // 2) 启用 SeDebugPrivilege（复制 winlogon 令牌需要）
    EnableDebugPrivilege();

    // 3) 已有 UIAccess → 直接继续
    BOOL fUI = FALSE;
    if (HasUIAccess(&fUI) && fUI) return FALSE;

    // 4) 尝试拿到 UIAccess 令牌并重启自身
    HANDLE hTokenUIAccess;
    if (CreateUIAccessToken(&hTokenUIAccess) == ERROR_SUCCESS) {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        GetStartupInfoW(&si);
        if (CreateProcessAsUserW(hTokenUIAccess, NULL, GetCommandLineW(),
            NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            ExitProcess(0);   // 新进程接管，本进程退出
        }
        CloseHandle(hTokenUIAccess);
    }
    // UIAccess 失败不致命，继续运行（只是普通置顶可能被极域压下去）
    return FALSE;
}

} // namespace UIAccess

#endif // _JYU_UIAccess_H_
