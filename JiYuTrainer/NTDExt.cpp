// NTDExt.cpp - NoTopDomain 功能移植模块实现
// 移植自 NoTopDomain (Python+PyQt5) → JiYuTrainer (C++)

#include "stdafx.h"
#include "NTDExt.h"
#include "inject.h"
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <io.h>

// NT_SUCCESS 宏（如果未定义）
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((LONG)(Status)) >= 0)
#endif

// NTSTATUS 类型（如果未定义）
#ifndef NTSTATUS
typedef long NTSTATUS;
#endif

// ntdll 未文档化 API 声明（在全局作用域）
typedef LONG (NTAPI* NtSuspendProcess_t)(HANDLE ProcessHandle);
typedef LONG (NTAPI* NtResumeProcess_t)(HANDLE ProcessHandle);

// 引入嵌入的空 EXE（用于脱离远程关机）
#include "emptyexe.inc"

namespace NTDExt
{

// ==================== 辅助函数 ====================

// 记录日志
void LogMessage(const wchar_t* msg)
{
#ifdef _DEBUG
    OutputDebugStringW(msg);
#endif
}

// 运行一个命令并等待完成
static bool RunCmdWait(const wchar_t* cmdLine, DWORD waitMs = 5000)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    std::wstring cmd(cmdLine);
    
    if (!CreateProcessW(NULL, const_cast<LPWSTR>(cmd.c_str()), NULL, NULL, FALSE, 
                         CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return false;
    
    WaitForSingleObject(pi.hProcess, waitMs);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// 枚举所有进程，找到指定名称的 PID
static std::vector<DWORD> FindProcessPids(const wchar_t* processName)
{
    std::vector<DWORD> pids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return pids;
    
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(hSnap, &pe))
    {
        do {
            if (_wcsicmp(pe.szExeFile, processName) == 0)
                pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pids;
}

// ==================== 进程操作 ====================

bool SuspendProcess(DWORD pid)
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll)
    {
        hNtdll = LoadLibraryW(L"ntdll.dll");
        if (!hNtdll) return false;
    }
    
    NtSuspendProcess_t pNtSuspend = (NtSuspendProcess_t)GetProcAddress(hNtdll, "NtSuspendProcess");
    if (!pNtSuspend) return false;
    
    HANDLE hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!hProcess) return false;
    
    NTSTATUS status = pNtSuspend(hProcess);
    CloseHandle(hProcess);
    return NT_SUCCESS(status);
}

bool ResumeProcess(DWORD pid)
{
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll)
    {
        hNtdll = LoadLibraryW(L"ntdll.dll");
        if (!hNtdll) return false;
    }
    
    NtResumeProcess_t pNtResume = (NtResumeProcess_t)GetProcAddress(hNtdll, "NtResumeProcess");
    if (!pNtResume) return false;
    
    HANDLE hProcess = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, pid);
    if (!hProcess) return false;
    
    NTSTATUS status = pNtResume(hProcess);
    CloseHandle(hProcess);
    return NT_SUCCESS(status);
}

bool KillProcessByThread(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (!hProcess) return false;
    
    bool success = false;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 te = { sizeof(te) };
        if (Thread32First(hSnap, &te))
        {
            do {
                if (te.th32OwnerProcessID == pid)
                {
                    HANDLE hThread = OpenThread(THREAD_TERMINATE, FALSE, te.th32ThreadID);
                    if (hThread)
                    {
                        TerminateThread(hThread, 0);
                        CloseHandle(hThread);
                    }
                }
            } while (Thread32Next(hSnap, &te));
            success = true;
        }
        CloseHandle(hSnap);
    }
    
    // 最后兜底 TerminateProcess
    if (!success)
        success = TerminateProcess(hProcess, 0);
    
    CloseHandle(hProcess);
    return success;
}

bool KillProcessNTSD(const wchar_t* processName)
{
    wchar_t cmd[MAX_PATH + 100];
    swprintf_s(cmd, L"ntsd -c q -pn %s", processName);
    return RunCmdWait(cmd, 10000);
}

bool KillProcessByName(const wchar_t* processName)
{
    auto pids = FindProcessPids(processName);
    bool success = false;
    for (DWORD pid : pids)
    {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess)
        {
            if (TerminateProcess(hProcess, 0))
                success = true;
            CloseHandle(hProcess);
        }
    }
    return success;
}

// ==================== 系统服务 ====================

bool StopService(const wchar_t* serviceName)
{
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;
    
    SC_HANDLE hService = OpenServiceW(hSCM, serviceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
    bool result = false;
    
    if (hService)
    {
        SERVICE_STATUS ss;
        if (ControlService(hService, SERVICE_CONTROL_STOP, &ss))
            result = true;
        CloseServiceHandle(hService);
    }
    CloseServiceHandle(hSCM);
    return result;
}

// 枚举并停止/删除所有极域厂商（Mythware/TDNetFilter/Skylark/TD*）的内核驱动
// 驱动 unload 时内核会自动清除该驱动注册的 PsSetCreateProcessNotifyRoutine /
// PsSetCreateThreadNotifyRoutine / PsSetLoadImageNotifyRoutine 以及 ObRegisterCallbacks
// 回调，从而解除极域对 StudentMain.exe 的内核层进程保护
// 返回成功 stop 的驱动数量
int ClearJyuKernelDriver()
{
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (!hSCM) {
        LogMessage(L"[ClearJyuKernelDriver] OpenSCManager failed");
        return 0;
    }

    DWORD bufSize = 16384;
    LPBYTE buf = (LPBYTE)LocalAlloc(LPTR, bufSize);
    if (!buf) { CloseServiceHandle(hSCM); return 0; }

    DWORD returned  = 0;
    DWORD resumeHandle = 0;
    int   stoppedCount = 0;

    // EnumServicesStatusW 简单可靠，MinGW 兼容好
    BOOL enumOk = EnumServicesStatusW(
        hSCM,
        SERVICE_KERNEL_DRIVER,   // 只枚举内核驱动
        SERVICE_STATE_ALL,       // 运行中 + 已停止
        (LPENUM_SERVICE_STATUS)buf,
        bufSize,
        &bufSize,
        &returned,
        &resumeHandle);

    if (!enumOk) {
        if (GetLastError() == ERROR_MORE_DATA) {
            buf = (LPBYTE)LocalReAlloc(buf, bufSize, 0);
            if (buf) enumOk = EnumServicesStatusW(
                hSCM, SERVICE_KERNEL_DRIVER, SERVICE_STATE_ALL,
                (LPENUM_SERVICE_STATUS)buf, bufSize,
                &bufSize, &returned, &resumeHandle);
        }
    }

    if (!enumOk) {
        LogMessage(L"[ClearJyuKernelDriver] EnumServicesStatusW failed");
        LocalFree(buf);
        CloseServiceHandle(hSCM);
        return 0;
    }

    LPENUM_SERVICE_STATUS services = (LPENUM_SERVICE_STATUS)buf;
    {
        wchar_t msg[128];
        swprintf_s(msg, L"[ClearJyuKernelDriver] Scanning kernel drivers (%d found)...", returned);
        LogMessage(msg);
    }

    for (DWORD i = 0; i < returned; i++) {
        const wchar_t* svcName = services[i].lpServiceName;
        const wchar_t* dispName = services[i].lpDisplayName;

        bool isJyu = false;

        // --- 1. 硬编码的极域已知驱动名（最可靠） ---
        struct { const wchar_t* name; } knownJyuDrivers[] = {
            { L"TDNetFilter" },
            { L"TDFileFilter" },
            { L"SkylarkTD" },
            { L"MythwareTD" },
            { L"TDProtect" },
            { L"TDMon" },
            { NULL }
        };
        for (int k = 0; knownJyuDrivers[k].name; k++) {
            if (_wcsicmp(svcName, knownJyuDrivers[k].name) == 0) {
                isJyu = true;
                break;
            }
        }

        // --- 2. 软匹配：服务 DisplayName 或路径里含极域厂商关键词 ---
        if (!isJyu && dispName) {
            if (wcsstr(dispName, L"Mythware") ||
                wcsstr(dispName, L"e-Learning") ||
                wcsstr(dispName, L"TDNetFilter") ||
                wcsstr(dispName, L"Skylark"))
                isJyu = true;
        }

        if (!isJyu) {
            // 再看 BinaryPathName
            SC_HANDLE hSvc = OpenServiceW(hSCM, svcName, SERVICE_QUERY_CONFIG);
            if (hSvc) {
                DWORD cfgBufSize = 0;
                QueryServiceConfigW(hSvc, NULL, 0, &cfgBufSize);
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    LPBYTE cfgBuf = (LPBYTE)LocalAlloc(LPTR, cfgBufSize);
                    if (cfgBuf && QueryServiceConfigW(hSvc, (LPQUERY_SERVICE_CONFIGW)cfgBuf, cfgBufSize, &cfgBufSize)) {
                        LPQUERY_SERVICE_CONFIGW cfg = (LPQUERY_SERVICE_CONFIGW)cfgBuf;
                        const wchar_t* path = cfg->lpBinaryPathName;
                        if (path) {
                            if (wcsstr(path, L"Mythware") ||
                                wcsstr(path, L"e-Learning Class") ||
                                wcsstr(path, L"\\TD") ||
                                wcsstr(path, L"\\skylark") ||
                                wcsstr(path, L"\\TDNetFilter"))
                                isJyu = true;
                        }
                        LocalFree(cfgBuf);
                    }
                }
                CloseServiceHandle(hSvc);
            }
        }

        if (!isJyu) continue;

        wchar_t msg[256];
        swprintf_s(msg, L"[ClearJyuKernelDriver] Found Jiyu driver: %s (%s)", svcName, dispName ? dispName : L"");
        LogMessage(msg);

        // --- Stop ---
        SC_HANDLE hSvc = OpenServiceW(hSCM, svcName,
            SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
        if (!hSvc) {
            LogMessage(L"[ClearJyuKernelDriver] OpenServiceW failed, skip");
            continue;
        }

        SERVICE_STATUS ss = { 0 };
        if (ControlService(hSvc, SERVICE_CONTROL_STOP, &ss)) {
            LogMessage(L"[ClearJyuKernelDriver] ControlService(STOP) OK, waiting unload...");
            for (int w = 0; w < 20; w++) {
                Sleep(250);
                SERVICE_STATUS ss2 = { 0 };
                if (QueryServiceStatus(hSvc, &ss2)) {
                    if (ss2.dwCurrentState == SERVICE_STOPPED) {
                        stoppedCount++;
                        break;
                    }
                }
            }
        } else {
            LogMessage(L"[ClearJyuKernelDriver] ControlService(STOP) failed (may already be stopped)");
        }

        // --- Delete，防止极域重启 ---
        DeleteService(hSvc);
        CloseServiceHandle(hSvc);
    }

    LocalFree(buf);
    CloseServiceHandle(hSCM);

    wchar_t msg[128];
    swprintf_s(msg, L"[ClearJyuKernelDriver] Done. %d driver(s) stopped/unloaded.", stoppedCount);
    LogMessage(msg);
    Sleep(1000);   // 给内核回调清理一点时间
    return stoppedCount;
}

// 把嵌入的 KernelCallbackCleaner.exe (x64 BYOVD 硬清内核回调工具)
// 从 BIN 资源解压到 %TEMP%\JyuCleaner\ 目录, 然后 CreateProcess 管理员运行
// 返回值: KernelCallbackCleaner.exe 的退出码 (0=成功清除, 1=没找到, 2=驱动加载失败)
//        -1 = 解压/启动失败
int RunKernelCallbackCleaner()
{
    // 1. Create temp dir %TEMP%\JyuCleaner
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    wcscat_s(tempDir, MAX_PATH, L"JyuCleaner");
    CreateDirectoryW(tempDir, NULL);

    wchar_t cleanerPath[MAX_PATH];
    swprintf_s(cleanerPath, L"%s\\KernelCallbackCleaner.exe", tempDir);

    // 2. 解压 BIN 资源
    HINSTANCE hInst = GetModuleHandleW(NULL);
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(139), L"BIN");  // IDR_EXE_KERNEL_CLEANER = 139
    if (!hRes) {
        LogMessage(L"[RunKernelCallbackCleaner] FindResourceW(137) failed - BIN 资源未找到");
        return -1;
    }
    HGLOBAL hG = LoadResource(hInst, hRes);
    if (!hG) { LogMessage(L"[RunKernelCallbackCleaner] LoadResource failed"); return -1; }
    LPVOID pData = LockResource(hG);
    DWORD dwSize = SizeofResource(hInst, hRes);

    HANDLE hFile = CreateFileW(cleanerPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        wchar_t msg[256];
        swprintf_s(msg, L"[RunKernelCallbackCleaner] CreateFileW(%s) failed (err=%lu)",
                   cleanerPath, GetLastError());
        LogMessage(msg);
        return -1;
    }
    DWORD written = 0;
    WriteFile(hFile, pData, dwSize, &written, NULL);
    CloseHandle(hFile);

    wchar_t msg[256];
    swprintf_s(msg, L"[RunKernelCallbackCleaner] Extracted %lu bytes -> %s", dwSize, cleanerPath);
    LogMessage(msg);

    // 2.5 Extract RTCore64.sys (BYOVD driver) to same dir
    wchar_t sysPath[MAX_PATH];
    swprintf_s(sysPath, L"%s\\RTCore64.sys", tempDir);

    // First try embedded BIN resource (IDR_DLL_RTCORE = 140)
    HRSRC hRes2 = FindResourceW(hInst, MAKEINTRESOURCEW(140), L"BIN");
    if (hRes2) {
        HGLOBAL hG2 = LoadResource(hInst, hRes2);
        if (hG2) {
            LPVOID pData2 = LockResource(hG2);
            DWORD dwSize2 = SizeofResource(hInst, hRes2);
            HANDLE hFile2 = CreateFileW(sysPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, NULL);
            if (hFile2 != INVALID_HANDLE_VALUE) {
                DWORD written2 = 0;
                WriteFile(hFile2, pData2, dwSize2, &written2, NULL);
                CloseHandle(hFile2);
                swprintf_s(msg, L"[RunKernelCallbackCleaner] Extracted RTCore64.sys %lu bytes", dwSize2);
                LogMessage(msg);
            }
        }
    } else {
        // Fallback: copy from JiYuTrainer.exe same directory
        wchar_t selfDir[MAX_PATH];
        GetModuleFileNameW(NULL, selfDir, MAX_PATH);
        wchar_t* lastSlash = wcsrchr(selfDir, L'\\');
        if (lastSlash) { *lastSlash = 0; }
        wchar_t srcSys[MAX_PATH];
        swprintf_s(srcSys, L"%s\\RTCore64.sys", selfDir);
        if (CopyFileW(srcSys, sysPath, FALSE)) {
            LogMessage(L"[RunKernelCallbackCleaner] Copied RTCore64.sys from exe directory");
        } else {
            LogMessage(L"[RunKernelCallbackCleaner] WARNING: RTCore64.sys not found (need it for BYOVD)");
        }
    }

    // 3. Launch x64 Cleaner.exe (WoW64 32-bit can CreateProcess x64 exe)
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFOW si = { sizeof(si) };
    if (!CreateProcessW(cleanerPath, NULL, NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, tempDir, &si, &pi)) {
        DWORD err = GetLastError();
        swprintf_s(msg, L"[RunKernelCallbackCleaner] CreateProcessW failed (err=%lu)\n"
                   L"      - Need Administrator + RTCore64.sys + HVCI/VBS off", err);
        LogMessage(msg);
        return -1;
    }

    // 4. 等它跑完 (最多 30 秒)
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD exitCode = -1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    swprintf_s(msg, L"[RunKernelCallbackCleaner] KernelCallbackCleaner.exe 退出码=%lu", exitCode);
    LogMessage(msg);

    return (int)exitCode;
}

bool StartService(const wchar_t* serviceName)
{
    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCM) return false;
    
    SC_HANDLE hService = OpenServiceW(hSCM, serviceName, SERVICE_START);
    bool result = false;
    
    if (hService)
    {
        result = StartServiceW(hService, 0, NULL) == TRUE;
        CloseServiceHandle(hService);
    }
    CloseServiceHandle(hSCM);
    return result;
}

// ==================== 注册表操作 ====================

static const wchar_t* kIfeoRoot =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";

// Wow6432Node 分支（64 位系统下 32 位进程的注册表重定向路径）
static const wchar_t* kIfeoRootWow6432 =
    L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";

// 检查当前进程是否 32 位在 64 位系统上运行（WOW64）
static bool IsCurrentProcessWow64() {
    BOOL wow = FALSE;
    if (!IsWow64Process(GetCurrentProcess(), &wow)) return false;
    return wow == TRUE;
}

// 返回该注册表根下当前进程视角能看到的 IFEO 根路径
static const wchar_t* EffectiveIfeoRoot(HKEY hRoot) {
    // 32 位进程在 64 位系统下 HKLM 是重定向到 WOW6432Node 的
    // 但 HKCU 不重定向，HKCU 直接看到原生位置
    if (hRoot == HKEY_LOCAL_MACHINE && IsCurrentProcessWow64()) return kIfeoRootWow6432;
    return kIfeoRoot;
}

// 极域/恶意软件常用的 IFEO 目标（清理清单）
static const wchar_t* kCommonToolTargets[] = {
    L"cmd.exe", L"cmd", L"command.exe",
    L"regedit.exe", L"regedit", L"regedt32.exe",
    L"taskmgr.exe", L"taskmgr",
    L"powershell.exe", L"powershell",
    L"pwsh.exe", L"pwsh",
    L"mmc.exe",
    L"mspaint.exe",
    L"notepad.exe",
    L"calc.exe",
    L"procexp.exe", L"procexp64.exe", L"ProcessExplorer.exe",
    L"processhacker.exe", L"ProcessHacker.exe",
    L"PCHunter.exe", L"pchunter64.exe",
    L"WuKong.exe",
    L"Xuetr.exe",
    L"ATool.exe",
    L"IceSword.exe",
    L"IceSword64.exe",
    L"ProcDump.exe",
    L"Autoruns.exe", L"Autoruns64.exe",
    L"Sigcheck.exe",
    L"HxD.exe",
    L"UltraEdit.exe",
    L"WinHex.exe",
    L"OllyDbg.exe",
    L"x64dbg.exe", L"x32dbg.exe",
    L"WPE-Pro.exe",
    L"Fiddler.exe", L"Fiddler64.exe",
    L"IDA.exe", L"ida64.exe",
    L"IDA64.exe",
    L"Hacker.Decompile.exe",
    L"Hacker.Decompile.64.exe",
    L"Petya.exe",
    L"cmdow.exe",
    L"JiYuTrainer.exe", L"JiYuTrainer.exe.exe",
    nullptr
};

// 系统合法调试器（不清理）
static const wchar_t* kLegitimateDebuggers[] = {
    L"GBD.exe", L"GBD64.exe",           // Visual Studio 调试器
    L"vsjitdebugger.exe",                // VS 即时调试器
    L"windbg.exe", L"windbgx.exe",       // WinDbg
    L"ntsd.exe",                        // 符号化调试器
    L"cdb.exe",                         // Console Debugger
    L"kd.exe", L"kdx.exe",              // Kernel Debugger
    L"difxapi.exe",
    nullptr
};

static bool IsCommonTool(const wchar_t* name) {
    for (int i = 0; kCommonToolTargets[i]; i++)
        if (_wcsicmp(kCommonToolTargets[i], name) == 0) return true;
    return false;
}

static bool IsLegitimateDebugger(const wchar_t* dbgPath) {
    // 检查 debugger 值里是否包含合法调试器
    std::wstring s = dbgPath ? dbgPath : L"";
    for (int i = 0; kLegitimateDebuggers[i]; i++)
        if (_wcsicmp(s.c_str(), kLegitimateDebuggers[i]) == 0) return true;
    // 路径里包含这些关键字也算
    const wchar_t* keywords[] = { L"\\VS\\", L"\\Microsoft Visual Studio", L"\\Debuggers\\", L"\\Windbg\\", nullptr };
    for (int i = 0; keywords[i]; i++)
        if (wcsstr(s.c_str(), keywords[i]) != nullptr) return true;
    return false;
}

// 读指定 IFEO 子键的 debugger 值；双侧视角都查（默认 + KEY_WOW64_64KEY），HKLM 优先
static bool QueryIfeoDebugger(const wchar_t* exeName, std::wstring* dbg) {
    // 先查 HKLM 双侧，再查 HKCU 双侧
    const HKEY hives[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    REGSAM views[] = { 0, KEY_WOW64_64KEY };
    for (int h = 0; h < 2; h++) {
        for (auto v : views) {
            const wchar_t* root = EffectiveIfeoRoot(hives[h]);
            if (v & KEY_WOW64_64KEY) {
                // KEY_WOW64_64KEY 强制看 64 位原生注册表
                root = (hives[h] == HKEY_LOCAL_MACHINE) ? kIfeoRoot : kIfeoRoot;
            }
            HKEY hSub;
            if (RegOpenKeyExW(hives[h], (std::wstring(root) + L"\\" + exeName).c_str(),
                              0, KEY_QUERY_VALUE | v, &hSub) != ERROR_SUCCESS) continue;
            wchar_t data[2048] = {};
            DWORD dataSize = sizeof(data);
            DWORD type = 0;
            LONG q = RegQueryValueExW(hSub, L"debugger", NULL, &type, (LPBYTE)data, &dataSize);
            RegCloseKey(hSub);
            if (q == ERROR_SUCCESS) {
                if (dbg) *dbg = data;
                return true;
            }
        }
    }
    return false;
}

// 在两侧（32 位重定向 + 64 位原生）都删除 IFEO 子键；HKLM 和 HKCU 都删
static bool DeleteIfeoKeyBothViews(const wchar_t* exeName) {
    const wchar_t* roots[] = { kIfeoRoot, kIfeoRootWow6432 };
    const HKEY hives[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };
    bool didDelete = false;
    for (int h = 0; h < 2; h++) {
        for (int r = 0; r < 2; r++) {
            std::wstring full = std::wstring(roots[r]) + L"\\" + exeName;
            if (RegDeleteKeyW(hives[h], full.c_str()) == ERROR_SUCCESS) didDelete = true;
            else { DWORD err = GetLastError(); if (err != ERROR_FILE_NOT_FOUND && err != ERROR_ACCESS_DENIED) {/* 忽略 */} }
        }
    }
    return didDelete;
}

// 从 HKLM 和 HKCU 默认视角都删除（兼容旧接口）
static bool DeleteIfeoKey(const wchar_t* exeName) {
    return DeleteIfeoKeyBothViews(exeName);
}

// 在 hRoot 指定的根下枚举 IFEO；samFlags 控制 WOW64 视角（KEY_WOW64_32KEY / KEY_WOW64_64KEY / 0 默认）
static void EnumerateIfeoFrom(HKEY hRoot, REGSAM samFlags,
                              std::vector<IfeoEntry>* out,
                              const wchar_t* selfPath, const wchar_t* selfName) {
    HKEY hParentKey;
    const wchar_t* root = EffectiveIfeoRoot(hRoot);
    if (RegOpenKeyExW(hRoot, root, 0,
                      KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | samFlags, &hParentKey) != ERROR_SUCCESS)
        return;

    wchar_t subkeyName[256];
    DWORD nameLen;
    DWORD index = 0;
    while (true) {
        nameLen = 256;
        LONG ret = RegEnumKeyExW(hParentKey, index++, subkeyName, &nameLen, NULL, NULL, NULL, NULL);
        if (ret == ERROR_NO_MORE_ITEMS) break;
        if (ret != ERROR_SUCCESS) break;

        std::wstring dbg;
        // 读同一 WOW64 视角下的 debugger
        HKEY hSub;
        if (RegOpenKeyExW(hRoot, (std::wstring(root) + L"\\" + subkeyName).c_str(),
                          0, KEY_QUERY_VALUE | samFlags, &hSub) == ERROR_SUCCESS) {
            wchar_t data[2048] = {};
            DWORD dataSize = sizeof(data);
            DWORD type = 0;
            LONG q = RegQueryValueExW(hSub, L"debugger", NULL, &type, (LPBYTE)data, &dataSize);
            RegCloseKey(hSub);
            if (q == ERROR_SUCCESS) {
                dbg = data;
                IfeoEntry e;
                e.exeName = subkeyName;
                e.debugger = dbg;
                e.ourProtect = (_wcsicmp(subkeyName, selfName) == 0 &&
                                _wcsicmp(dbg.c_str(), selfPath) == 0);
                out->push_back(e);
            }
        }
    }
    RegCloseKey(hParentKey);
}

// 注册表备份：reg export 到 %TEMP%，返回文件路径（空串表示失败）
static std::wstring RegExportKey(const wchar_t* regPath, const wchar_t* tag) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    SYSTEMTIME st = {}; GetLocalTime(&st);
    wchar_t out[MAX_PATH] = {};
    swprintf_s(out, L"%s\\JiYuTrainer-%02d%02d%02d-%02d%02d%02d-%s.reg",
               tmp, st.wYear % 100, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, tag);

    wchar_t sysroot[MAX_PATH] = {};
    ExpandEnvironmentStringsW(L"%SystemRoot%", sysroot, MAX_PATH);
    wchar_t cmd[MAX_PATH * 2] = {};
    swprintf_s(cmd, L"\"%s\\System32\\reg.exe\" export \"%s\" \"%s\" /y", sysroot, regPath, out);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return L"";
    WaitForSingleObject(pi.hProcess, 15000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode == 0 ? std::wstring(out) : L"";
}

// 自 exe 完整路径提取基名（不含路径）
static std::wstring SelfBaseName() {
    wchar_t full[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, full, MAX_PATH);
    const wchar_t* p = wcsrchr(full, L'\\');
    return std::wstring(p ? p + 1 : full);
}

std::vector<IfeoEntry> ScanIfeoHijacks() {
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    std::wstring selfName = SelfBaseName();

    std::vector<IfeoEntry> result;
    // 双侧扫描：默认视角（0）+ 强制 64 位原生视角（KEY_WOW64_64KEY）
    REGSAM views[] = { 0, KEY_WOW64_64KEY };
    for (auto v : views) {
        EnumerateIfeoFrom(HKEY_LOCAL_MACHINE, v, &result, selfPath, selfName.c_str());
        EnumerateIfeoFrom(HKEY_CURRENT_USER,  v, &result, selfPath, selfName.c_str());
    }
    // 去重（同一 exe 两边都有的话只保留一份）
    std::vector<IfeoEntry> unique;
    for (auto& e : result) {
        bool dup = false;
        for (auto& u : unique) if (_wcsicmp(u.exeName.c_str(), e.exeName.c_str()) == 0) { dup = true; break; }
        if (!dup) unique.push_back(e);
    }
    return unique;
}

bool ClearSpecificIfeo(const wchar_t* exeName) {
    if (!exeName || !*exeName) return false;
    return DeleteIfeoKey(exeName);
}

bool ClearCommonToolsIfeos(std::wstring* report) {
    auto entries = ScanIfeoHijacks();
    std::wstring log;

    // 清理前备份：原生 + WOW64 双侧
    if (report) {
        std::wstring bk1 = RegExportKey(L"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options", L"ifeo-hklm");
        std::wstring bk2 = RegExportKey(L"HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options", L"ifeo-hklm-wow6432");
        std::wstring bk3 = RegExportKey(L"HKCU\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options", L"ifeo-hkcu");
        if (!bk1.empty()) log += L"[备份  ] " + bk1 + L"\r\n";
        if (!bk2.empty()) log += L"[备份  ] " + bk2 + L"\r\n";
        if (!bk3.empty()) log += L"[备份  ] " + bk3 + L"\r\n";
    }

    int cleared = 0;
    bool allOk = true;
    for (auto& e : entries) {
        if (IsLegitimateDebugger(e.debugger.c_str())) {
            if (report) log += L"[保留合法] " + e.exeName + L" -> " + e.debugger + L"\r\n";
            continue;
        }
        if (e.ourProtect) {
            if (report) log += L"[保留自护 ] " + e.exeName + L" -> self\r\n";
            continue;
        }
        if (IsCommonTool(e.exeName.c_str())) {
            bool ok = DeleteIfeoKeyBothViews(e.exeName.c_str());
            if (ok) cleared++;
            else allOk = false;
            if (report) log += std::wstring(ok ? L"[清除  ] " : L"[失败  ] ") + e.exeName + L" -> " + e.debugger + L"\r\n";
        } else {
            if (report) log += L"[未处理 ] " + e.exeName + L" -> " + e.debugger + L" (非常规目标)\r\n";
        }
    }
    if (report) *report = log;
    return allOk;
}

bool ProtectSelfIfeo() {
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    std::wstring selfName = SelfBaseName();

    // 双侧写入：HKLM + HKCU，原生 + WOW6432
    const wchar_t* roots[] = { kIfeoRoot, kIfeoRootWow6432 };
    const HKEY hives[] = { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER };

    bool wroteAny = false;
    for (int h = 0; h < 2; h++) {
        for (int r = 0; r < 2; r++) {
            std::wstring fullPath = std::wstring(roots[r]) + L"\\" + selfName;
            HKEY hKey;
            LONG ret = RegCreateKeyExW(hives[h], fullPath.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
            if (ret != ERROR_SUCCESS) continue;

            // 已受保护则跳过
            wchar_t cur[2048] = {};
            DWORD cs = sizeof(cur);
            DWORD ct = 0;
            if (RegQueryValueExW(hKey, L"debugger", NULL, &ct, (LPBYTE)cur, &cs) == ERROR_SUCCESS) {
                if (_wcsicmp(cur, selfPath) == 0) { RegCloseKey(hKey); continue; }
            }

            ret = RegSetValueExW(hKey, L"debugger", 0, REG_SZ, (const BYTE*)selfPath,
                                 (DWORD)((wcslen(selfPath) + 1) * sizeof(wchar_t)));
            RegCloseKey(hKey);
            if (ret == ERROR_SUCCESS) wroteAny = true;
        }
    }
    return wroteAny;
}

bool IsSelfIfeoHijacked(std::wstring* outDebugger) {
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    std::wstring selfName = SelfBaseName();

    std::wstring dbg;
    if (!QueryIfeoDebugger(selfName.c_str(), &dbg)) return false;  // 没有 IFEO 条目
    if (_wcsicmp(dbg.c_str(), selfPath) == 0) return false;  // 是我们自己的保护
    if (outDebugger) *outDebugger = dbg;
    return true;
}

bool ClearImageFileExecutionOptions()
{
    // 等价于 Python: 遍历 HKLM\...\Image File Execution Options，删除含 debugger 值的子键
    HKEY hRoot = HKEY_LOCAL_MACHINE;
    const wchar_t* subkeyPath = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options";
    
    HKEY hParentKey;
    if (RegOpenKeyExW(hRoot, subkeyPath, 0, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &hParentKey) != ERROR_SUCCESS)
        return false;
    
    bool result = true;
    wchar_t subkeyName[256];
    DWORD nameLen;
    DWORD index = 0;
    
    while (true)
    {
        nameLen = 256;
        LONG ret = RegEnumKeyExW(hParentKey, index++, subkeyName, &nameLen, NULL, NULL, NULL, NULL);
        if (ret == ERROR_NO_MORE_ITEMS) break;
        if (ret != ERROR_SUCCESS) { result = false; break; }
        
        // 打开子键，检查是否有 debugger 值
        HKEY hSubKey;
        if (RegOpenKeyExW(hParentKey, subkeyName, 0, KEY_QUERY_VALUE, &hSubKey) == ERROR_SUCCESS)
        {
            wchar_t data[2048];
            DWORD dataSize = sizeof(data);
            DWORD type = 0;
            LONG qret = RegQueryValueExW(hSubKey, L"debugger", NULL, &type, (LPBYTE)data, &dataSize);
            RegCloseKey(hSubKey);
            
            // 如果有 debugger 值，删除这个子键
            if (qret == ERROR_SUCCESS)
            {
                if (RegDeleteKeyW(hParentKey, subkeyName) != ERROR_SUCCESS)
                    result = false;
            }
        }
    }
    
    RegCloseKey(hParentKey);
    return result;
}

static bool SetRegDWord(HKEY hRoot, const wchar_t* subkey, const wchar_t* valueName, DWORD val)
{
    HKEY hKey;
    LONG ret = RegCreateKeyExW(hRoot, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL);
    if (ret != ERROR_SUCCESS) return false;
    
    ret = RegSetValueExW(hKey, valueName, 0, REG_DWORD, (BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
    return ret == ERROR_SUCCESS;
}

// ==================== 组策略扫描/清理 ====================

struct PolicyTarget {
    HKEY hRoot;
    const wchar_t* subkey;
    const wchar_t* valueName;
    DWORD expectedBadValue;   // 极域设置的限制值（通常 = 1）
};

// 极域/恶意软件常用的组策略限制清单（末尾必须有哨兵 subkey=nullptr）
static const PolicyTarget kPolicyTargets[] = {
    // === 用户级（HKCU ===
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",   L"DisableTaskMgr",        1 },
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",   L"DisableRegistryTools",  1 },
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",   L"DisableChangePassword", 1 },
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"NoRun",                 1 },
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"NoControlPanel",        1 },
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"HideDrives",            1 },
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"DisallowRun",           0 /* 数组型，特殊处理 */ },
    { HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\WinOldApp", L"Disabled",            1 },
    // === 系统级（HKLM ===
    { HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\Windows\\System", L"DisableCMD",   1 },
    { HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\Windows\\System", L"DisableRegedit", 1 },
    { HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\Windows\\Explorer", L"NoRun",       1 },
    { HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\Windows\\Explorer", L"NoControlPanel", 1 },
    { HKEY_LOCAL_MACHINE, L"Software\\Policies\\Microsoft\\Windows\\Explorer", L"DisallowRun", 0 /* 数组型 */ },
    // === 哨兵 ===
    { HKEY_CURRENT_USER, nullptr, nullptr, 0 },
};

static const wchar_t* kPolicyTargetSubkeys[] = {
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer",
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\WinOldApp",
    L"Software\\Policies\\Microsoft\\Windows\\System",
    L"Software\\Policies\\Microsoft\\Windows\\Explorer",
    nullptr
};

// 组策略扫描结果
struct PolicyEntry {
    HKEY hRoot;
    std::wstring subkey;
    std::wstring valueName;
    bool hasValue = false;
    DWORD value = 0;
    bool isArray = false;  // DisallowRun 这类 REG_DWORD 数组型
};

// 扫描所有极域常用策略，返回被设置过的条目
// 判定规则：DWORD 值 != 0 才算"被限制"；数组/字符串型键存在就算
static std::vector<PolicyEntry> ScanPolicyEntries() {
    std::vector<PolicyEntry> result;
    for (int i = 0; kPolicyTargets[i].subkey; i++) {
        const PolicyTarget& t = kPolicyTargets[i];
        HKEY hKey;
        if (RegOpenKeyExW(t.hRoot, t.subkey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) continue;

        DWORD dataSize = 0;
        DWORD type = 0;
        LONG q = RegQueryValueExW(hKey, t.valueName, NULL, &type, NULL, &dataSize);
        if (q != ERROR_SUCCESS || dataSize == 0) { RegCloseKey(hKey); continue; }

        if (type == REG_DWORD && dataSize >= sizeof(DWORD)) {
            DWORD buf = 0;
            RegQueryValueExW(hKey, t.valueName, NULL, &type, (LPBYTE)&buf, &dataSize);
            // 值为 0 等于没限制，不算"被极域改过"
            if (buf == 0) { RegCloseKey(hKey); continue; }
            PolicyEntry pe;
            pe.hRoot = t.hRoot;
            pe.subkey = t.subkey;
            pe.valueName = t.valueName;
            pe.hasValue = true;
            pe.value = buf;
            pe.isArray = false;
            result.push_back(pe);
        } else {
            // 数组型或字符串型：存在就算被设置过（系统默认不会有 DisallowRun 这种键）
            PolicyEntry pe;
            pe.hRoot = t.hRoot;
            pe.subkey = t.subkey;
            pe.valueName = t.valueName;
            pe.hasValue = true;
            pe.isArray = true;
            result.push_back(pe);
        }
        RegCloseKey(hKey);
    }
    return result;
}

// 强制刷新组策略 (gpupdate /force)
static bool RunGpUpdate() {
    wchar_t sysroot[MAX_PATH] = {};
    ExpandEnvironmentStringsW(L"%SystemRoot%", sysroot, MAX_PATH);
    wchar_t cmd[MAX_PATH * 2] = {};
    swprintf_s(cmd, L"\"%s\\System32\\gpupdate.exe\" /force", sysroot);
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    // 等 20 秒，gpupdate 比较慢
    WaitForSingleObject(pi.hProcess, 20000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return exitCode == 0;
}

// 组策略扫描（公开接口）：返回被极域等程序设置过的策略条目
std::vector<IfeoEntry> ScanPolicyHijacks() {
    // 复用 IfeoEntry 结构不太合适，这里单独用 PolicyEntry
    // 为了让外部调用（BuildCapabilityReport）能拿到，先复用现有数据
    std::vector<PolicyEntry> pols = ScanPolicyEntries();
    std::vector<IfeoEntry> out;
    for (auto& p : pols) {
        if (!p.hasValue) continue;
        IfeoEntry e;
        wchar_t rootTag[32] = {};
        wcscpy_s(rootTag, (p.hRoot == HKEY_LOCAL_MACHINE) ? L"HKLM" : L"HKCU");
        e.exeName = std::wstring(rootTag) + L"\\" + p.subkey + L"\\" + p.valueName;
        if (p.isArray) e.debugger = L"(数组/列表类型)";
        else { wchar_t v[32] = {}; swprintf_s(v, L"= 0x%08X", p.value); e.debugger = v; }
        e.ourProtect = false;
        out.push_back(e);
    }
    return out;
}

// 旧兼容接口（ClearGroupPolicyRestrictions），新实现会：
//   1. reg export 备份所有 Policy 相关注册表路径
//   2. 批量置 0（对 DWORD 型）或删除 DisallowRun 数组/Explorer\DisallowRun 键
//   3. gpupdate /force 使清理生效
// 返回清理成功的条目数（0 表示都已经是安全状态）
int ClearGroupPolicyRestrictionsEx(std::wstring* report) {
    std::wstring log;

    // === 备份 ===
    if (report) {
        const wchar_t* backupPaths[] = {
            L"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies",
            L"HKLM\\Software\\Policies\\Microsoft\\Windows",
            nullptr
        };
        for (int i = 0; backupPaths[i]; i++) {
            std::wstring bk = RegExportKey(backupPaths[i], (i == 0) ? L"pols-hkcu" : L"pols-hklm");
            if (!bk.empty()) log += L"[备份  ] " + bk + L"\r\n";
        }
    }

    // === 处理 DisallowRun 数组键 (极域常用的软件黑名单) ===
    // HKCU\...\Explorer\DisallowRun 键下有 1,2,3,... 命名的字符串值
    const wchar_t* disallowRunPaths[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer\\DisallowRun",
        L"Software\\Policies\\Microsoft\\Windows\\Explorer\\DisallowRun",
        nullptr
    };
    int cleared = 0;
    for (int i = 0; disallowRunPaths[i]; i++) {
        HKEY h;
        HKEY roots2[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
        for (auto r : roots2) {
            if (RegOpenKeyExW(r, disallowRunPaths[i], 0, KEY_ALL_ACCESS, &h) == ERROR_SUCCESS) {
                if (RegDeleteKeyW(r, disallowRunPaths[i]) == ERROR_SUCCESS) {
                    cleared++;
                    if (report) log += L"[清除  ] " + std::wstring((r == HKEY_LOCAL_MACHINE) ? L"HKLM" : L"HKCU") +
                                      L"\\" + disallowRunPaths[i] + L"\r\n";
                }
                RegCloseKey(h);
            }
        }
    }

    // === 批量置 0 ===
    for (int i = 0; kPolicyTargets[i].subkey; i++) {
        const PolicyTarget& t = kPolicyTargets[i];
        if (_wcsicmp(t.valueName, L"DisallowRun") == 0) continue;  // 数组型已单独处理
        if (SetRegDWord(t.hRoot, t.subkey, t.valueName, 0)) {
            cleared++;
            if (report) log += L"[置0   ] " + std::wstring((t.hRoot == HKEY_LOCAL_MACHINE) ? L"HKLM" : L"HKCU") +
                              L"\\" + t.subkey + L"\\" + t.valueName + L" = 0\r\n";
        } else {
            // 已经是 0 或没有值 —— 跳过（不是"被极域改过"的状态）
        }
    }

    // === 刷新组策略 ===
    if (report) log += L"[刷新  ] gpupdate /force ...\r\n";
    bool gpok = RunGpUpdate();
    if (report) log += std::wstring(gpok ? L"[刷新  ] gpupdate 完成\r\n" : L"[刷新  ] gpupdate 返回非零（可能需要手动注销/重启）\r\n");

    if (report) *report = log;
    return cleared;
}

// 旧接口保持兼容：返回 bool，true = 至少清了一个或都已经安全
bool ClearGroupPolicyRestrictions() {
    int cleared = ClearGroupPolicyRestrictionsEx(nullptr);
    return cleared >= 0;   // 0=都已安全也算成功
}

// ==================== 窗口操作 ====================

bool SetWindowTop(HWND hwnd, bool topmost)
{
    if (!IsWindow(hwnd)) return false;
    
    BOOL success;
    if (topmost)
        success = SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    else
        success = SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    
    return success == TRUE;
}

// 给任意窗口应用 WDA
// 注意: SetWindowDisplayAffinity 只允许操作本进程的窗口，跨进程调用会失败(ERROR_ACCESS_DENIED)，
//       所以对其它进程的窗口必须注入 JyuHideHook.dll，在其进程内调用 RemoteSetAffinity
static bool ApplyWindowDisplayAffinity(HWND hwnd, DWORD affinity)
{
    if (!IsWindow(hwnd)) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId())
        return SetWindowDisplayAffinity(hwnd, affinity) == TRUE;

    // JyuHideHook.dll 与本程序同目录
    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;
    std::wstring dllPath(exePath);
    size_t sep = dllPath.find_last_of(L'\\');
    if (sep == std::wstring::npos) return false;
    dllPath = dllPath.substr(0, sep + 1) + L"JyuHideHook.dll";
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        LogMessage(L"[hide] 缺少 JyuHideHook.dll，无法跨进程设置防截屏");
        return false;
    }

    struct AffinityParam { HWND hwnd; DWORD affinity; } param = { hwnd, affinity };
    auto r = inject::InjectAndCall(hwnd, dllPath, "RemoteSetAffinity", &param, sizeof(param));
    if (!r.success)
    {
        LogMessage((L"[hide] 注入失败: " + r.error).c_str());
        return false;
    }
    return r.exitCode == 0;  // RemoteSetAffinity 返回 0 表示成功
}

// 已由本程序隐藏的窗口（跨进程查询 WDA 不可靠，用本地记录兜底）
static std::vector<HWND> g_hiddenWindows;

static bool IsMarkedHidden(HWND hwnd)
{
    for (HWND h : g_hiddenWindows)
        if (h == hwnd && IsWindow(h)) return true;
    return false;
}

static void MarkHidden(HWND hwnd, bool hidden)
{
    for (size_t i = 0; i < g_hiddenWindows.size(); ++i)
    {
        if (g_hiddenWindows[i] == hwnd)
        {
            if (!hidden) g_hiddenWindows.erase(g_hiddenWindows.begin() + i);
            return;
        }
    }
    if (hidden) g_hiddenWindows.push_back(hwnd);
}

bool ToggleForegroundWindowHide()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;

    // 检查当前的 WDA 设置
    DWORD currentAffinity = WDA_NONE;
    bool covered = GetWindowDisplayAffinity(hwnd, &currentAffinity) &&
                   currentAffinity != WDA_NONE;
    bool hidden = covered || IsMarkedHidden(hwnd);

    if (hidden)
    {
        bool ok = ApplyWindowDisplayAffinity(hwnd, WDA_NONE);
        if (ok) MarkHidden(hwnd, false);
        return ok;
    }

    // 实测结论（见 mock_teacher.py selftest）：
    //   极域这类 GDI BitBlt 抓屏只尊重 WDA_MONITOR（截屏变黑），
    //   WDA_EXCLUDEFROMCAPTURE 只对 WGC/DWM 抓屏有效，GDI 仍能截到画面。
    //   因此优先 WDA_MONITOR，失败（极少数场景）再回退 EXCLUDEFROMCAPTURE。
    bool ok = ApplyWindowDisplayAffinity(hwnd, WDA_MONITOR) ||
              ApplyWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    if (ok) MarkHidden(hwnd, true);
    return ok;
}

bool ToggleForegroundWindowTop()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    BOOL isTopmost = (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    return SetWindowTop(hwnd, !isTopmost);
}

bool KillForegroundProcess()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    // 不要杀自己
    DWORD myPid = GetCurrentProcessId();
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == myPid) return false;
    
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProcess) return false;
    
    bool result = TerminateProcess(hProcess, 0) == TRUE;
    CloseHandle(hProcess);
    return result;
}

// ==================== 资源管理器 ====================

bool RestartExplorer()
{
    // 先杀掉 explorer.exe
    auto pids = FindProcessPids(L"explorer.exe");
    bool killed = false;
    for (DWORD pid : pids)
    {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hProcess)
        {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
            killed = true;
        }
    }
    
    // 系统会自动重启 explorer，但可以等一下手动启动
    Sleep(500);
    
    if (!killed)
    {
        // 如果没找到，直接启动一个
        RunCmdWait(L"C:\\Windows\\explorer.exe");
    }
    
    return true;
}

// ==================== 极域特定功能 ====================

bool ToggleNoShutdown(const wchar_t* jiyuDir, bool enable)
{
    if (!jiyuDir || !*jiyuDir) return false;
    
    std::wstring shutdownExe = std::wstring(jiyuDir) + L"\\Shutdown.exe";
    std::wstring shutdownBack = std::wstring(jiyuDir) + L"\\Shutdown_back.exe";
    
    if (enable)
    {
        // 备份原文件 → 写入空 EXE
        if (!CopyFileW(shutdownExe.c_str(), shutdownBack.c_str(), FALSE))
            return false;
        
        // 写入嵌入的空 EXE（用 C 风格 fopen 避免宽字符路径问题）
        FILE* fp = _wfopen(shutdownExe.c_str(), L"wb");
        if (!fp) return false;
        fwrite(g_emptyExeData, 1, g_emptyExeSize, fp);
        fclose(fp);
        return true;
    }
    else
    {
        // 恢复备份
        if (!CopyFileW(shutdownBack.c_str(), shutdownExe.c_str(), FALSE))
            return false;
        DeleteFileW(shutdownBack.c_str());
        return true;
    }
}

bool ToggleWebsiteUnblock(bool enable)
{
    if (enable)
    {
        // 结束进程
        KillProcessByName(L"gatesrv.exe");
        KillProcessByName(L"masterhelper.exe");
        
        // 停止驱动
        StopService(L"TDNetFilter");
        try { StopService(L"TDNetworkFilter"); } catch (...) {}
        
        return true;
    }
    else
    {
        // 恢复（这里简化处理：启动驱动即可）
        StartService(L"TDNetFilter");
        return true;
    }
}

bool ToggleUSBDeviceLimit(bool enable)
{
    if (enable)
        return StopService(L"TDFileFilter");
    else
        return StartService(L"TDFileFilter");
}

bool UnlockJiyuAntiKill()
{
    // 杀 prochelper64.exe 和 prochelper32.exe
    KillProcessByName(L"prochelper64.exe");
    KillProcessByName(L"prochelper32.exe");
    
    // 尝试卸载极域的 Hook DLL
    HMODULE hHook = GetModuleHandleW(L"LibTDProcHook64.dll");
    if (hHook) FreeLibrary(hHook);
    hHook = GetModuleHandleW(L"LibTDProcHook32.dll");
    if (hHook) FreeLibrary(hHook);
    
    return true;
}

bool ClearKeyboardAndMouseLock()
{
    // 极域的键盘/鼠标锁通过注入 DLL 实现，
    // JiYuTrainer 的 DLL 注入模块 (JiYuTrainerHooks) 已经处理了反锁功能，
    // 这里我们额外尝试：杀掉负责锁的进程
    KillProcessByName(L"TDInputBlock.exe");
    KillProcessByName(L"TDMouseHook.exe");
    
    // 同时解除所有极域防杀钩子
    UnlockJiyuAntiKill();
    
    return true;
}

// ==================== 实用工具 ====================

bool RunCommand(const wchar_t* cmd)
{
    return RunCmdWait(cmd);
}

} // namespace NTDExt
