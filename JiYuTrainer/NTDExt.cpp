// NTDExt.cpp - NoTopDomain 功能移植模块实现
// 移植自 NoTopDomain (Python+PyQt5) → JiYuTrainer (C++)

#include "stdafx.h"
#include "NTDExt.h"
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

bool ClearGroupPolicyRestrictions()
{
    bool result = true;
    
    // 用户级
    result &= SetRegDWord(HKEY_CURRENT_USER, 
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 
        L"DisableTaskMgr", 0);
    result &= SetRegDWord(HKEY_CURRENT_USER, 
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 
        L"DisableRegistryTools", 0);
    result &= SetRegDWord(HKEY_CURRENT_USER, 
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 
        L"NoRun", 0);
    result &= SetRegDWord(HKEY_CURRENT_USER, 
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", 
        L"NoControlPanel", 0);
    result &= SetRegDWord(HKEY_CURRENT_USER, 
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\WinOldApp", 
        L"Disabled", 0);
    
    // 系统级
    result &= SetRegDWord(HKEY_LOCAL_MACHINE,
        L"Software\\Policies\\Microsoft\\Windows\\System",
        L"DisableCMD", 0);
    result &= SetRegDWord(HKEY_LOCAL_MACHINE,
        L"Software\\Policies\\Microsoft\\Windows\\System",
        L"DisableRegedit", 0);
    result &= SetRegDWord(HKEY_LOCAL_MACHINE,
        L"Software\\Policies\\Microsoft\\Windows\\Explorer",
        L"NoRun", 0);
    
    return result;
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

bool ToggleForegroundWindowHide()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    
    // 检查当前的 WDA 设置
    DWORD currentAffinity = 0;
    if (GetWindowDisplayAffinity(hwnd, &currentAffinity))
    {
        if (currentAffinity & WDA_EXCLUDEFROMCAPTURE)
            SetWindowDisplayAffinity(hwnd, WDA_NONE);
        else
        {
            // Win10 2004+ 用 EXCLUDEFROMCAPTURE，老版本回退到 MONITOR
            SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
        }
    }
    return true;
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
