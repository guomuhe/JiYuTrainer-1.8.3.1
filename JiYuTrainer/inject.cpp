// inject.cpp - 跨进程 DLL 注入 + 远程调用 (简化版, 同架构 only)
#include "inject.h"
#include "MythwareCompat.h"
#include <tlhelp32.h>
#include <cstring>

namespace inject {

static std::wstring AsciiToWide(const char* s) {
    std::wstring r;
    if (!s) return r;
    while (*s) r += (wchar_t)(unsigned char)*s++;
    return r;
}

Result InjectAndCall(HWND hwnd,
                     const std::wstring& dllPath,
                     const char* funcName,
                     const void* params,
                     size_t paramsSize) {
    Result result = {};
    result.success = false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        result.error = L"目标窗口是本进程或无效 PID";
        return result;
    }
    HANDLE hProcess = OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProcess) {
        result.error = L"OpenProcess 失败 (需要管理员权限): " + WSTR(GetLastError());
        return result;
    }

    // Step 1: 在目标进程分配内存 → 写 DLL 路径
    size_t pathBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemoteMem = VirtualAllocEx(hProcess, nullptr, pathBytes,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemoteMem) { result.error = L"VirtualAllocEx(DLL路径) 失败: " + WSTR(GetLastError()); CloseHandle(hProcess); return result; }
    SIZE_T bytesWritten = 0;
    if (!WriteProcessMemory(hProcess, pRemoteMem, dllPath.c_str(), pathBytes, &bytesWritten)) {
        result.error = L"WriteProcessMemory(DLL路径) 失败: " + WSTR(GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE); CloseHandle(hProcess); return result;
    }

    // Step 2: CreateRemoteThread(LoadLibraryW) 加载 DLL
    auto pfnLoadLibrary = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, pfnLoadLibrary, pRemoteMem, 0, nullptr);
    if (!hThread) {
        result.error = L"CreateRemoteThread(LoadLibrary) 失败: " + WSTR(GetLastError());
        VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE); CloseHandle(hProcess); return result;
    }
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, pRemoteMem, 0, MEM_RELEASE);

    // Step 3: 本地 LoadLibrary 获取导出函数偏移
    HMODULE hLocalDll = LoadLibraryW(dllPath.c_str());
    if (!hLocalDll) { result.error = L"本地加载 DLL 失败: " + dllPath + L" err=" + WSTR(GetLastError()); CloseHandle(hProcess); return result; }
    FARPROC pfnLocal = GetProcAddress(hLocalDll, funcName);
    if (!pfnLocal) { result.error = L"找不到导出函数 " + AsciiToWide(funcName); FreeLibrary(hLocalDll); CloseHandle(hProcess); return result; }
    DWORD_PTR offset = (DWORD_PTR)pfnLocal - (DWORD_PTR)hLocalDll;

    // Step 4: 在目标进程里找 DLL 基址
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (hSnap == INVALID_HANDLE_VALUE) { result.error = L"Snapshot 失败: " + WSTR(GetLastError()); FreeLibrary(hLocalDll); CloseHandle(hProcess); return result; }
    std::wstring matchName = dllPath;
    size_t sl = matchName.find_last_of(L"\\/");
    if (sl != std::wstring::npos) matchName = matchName.substr(sl + 1);
    size_t dl = matchName.find_last_of(L'.');
    if (dl != std::wstring::npos) matchName = matchName.substr(0, dl);
    MODULEENTRY32W me = {}; me.dwSize = sizeof(me);
    DWORD_PTR remoteBase = 0;
    if (Module32FirstW(hSnap, &me)) {
        do {
            std::wstring modName = me.szModule;
            size_t mp = modName.find_last_of(L'.');
            if (mp != std::wstring::npos) modName = modName.substr(0, mp);
            if (_wcsicmp(modName.c_str(), matchName.c_str()) == 0) {
                remoteBase = (DWORD_PTR)me.modBaseAddr; break;
            }
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);
    if (remoteBase == 0) {
        result.error = L"目标进程中找不到 DLL 模块 " + matchName;
        FreeLibrary(hLocalDll); CloseHandle(hProcess); return result;
    }
    DWORD_PTR remoteProc = remoteBase + offset;

    // Step 5: 远程调用导出函数 (可选参数)
    LPVOID pRemoteParams = nullptr;
    if (params && paramsSize > 0) {
        pRemoteParams = VirtualAllocEx(hProcess, nullptr, paramsSize,
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemoteParams) { result.error = L"VirtualAllocEx(参数) 失败: " + WSTR(GetLastError()); FreeLibrary(hLocalDll); CloseHandle(hProcess); return result; }
        if (!WriteProcessMemory(hProcess, pRemoteParams, params, paramsSize, &bytesWritten)) {
            result.error = L"WriteProcessMemory(参数) 失败: " + WSTR(GetLastError());
            VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE); FreeLibrary(hLocalDll); CloseHandle(hProcess); return result;
        }
    }
    hThread = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)remoteProc, pRemoteParams, 0, nullptr);
    if (!hThread) {
        result.error = L"CreateRemoteThread(函数调用) 失败: " + WSTR(GetLastError());
        if (pRemoteParams) VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
        FreeLibrary(hLocalDll); CloseHandle(hProcess); return result;
    }
    WaitForSingleObject(hThread, 3000);
    DWORD exitCode = 0; GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    if (pRemoteParams) VirtualFreeEx(hProcess, pRemoteParams, 0, MEM_RELEASE);
    FreeLibrary(hLocalDll); CloseHandle(hProcess);

    result.success = true; result.exitCode = exitCode;
    if (exitCode != 0) result.error = L"远程调用返回错误码: " + WSTR(exitCode);
    logger::Info(L"注入调用 " + AsciiToWide(funcName) + L" PID=" + WSTR(pid) + L" exitCode=" + WSTR(exitCode));
    return result;
}

Result InjectDllOnly(DWORD pid, const std::wstring& dllPath) {
    Result result = {};
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, pid);
    if (!hProcess) { result.error = L"OpenProcess 失败: " + WSTR(GetLastError()); return result; }
    size_t pathBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID pRemote = VirtualAllocEx(hProcess, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemote) { result.error = L"VirtualAllocEx 失败: " + WSTR(GetLastError()); CloseHandle(hProcess); return result; }
    SIZE_T w = 0;
    if (!WriteProcessMemory(hProcess, pRemote, dllPath.c_str(), pathBytes, &w)) {
        result.error = L"WriteProcessMemory 失败: " + WSTR(GetLastError());
        VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE); CloseHandle(hProcess); return result;
    }
    auto pfnLL = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE t = CreateRemoteThread(hProcess, nullptr, 0, pfnLL, pRemote, 0, nullptr);
    if (!t) { result.error = L"CreateRemoteThread 失败: " + WSTR(GetLastError()); VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE); CloseHandle(hProcess); return result; }
    WaitForSingleObject(t, 5000);
    DWORD rc = 0; GetExitCodeThread(t, &rc); CloseHandle(t);
    VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE); CloseHandle(hProcess);
    if (rc == 0) { result.error = L"LoadLibraryW 返回 NULL, DLL 未成功加载"; return result; }
    result.success = true; result.exitCode = rc;
    logger::Info(L"DLL 注入成功 PID=" + WSTR(pid));
    return result;
}

Result UnInjectDll(DWORD pid, const std::wstring& moduleName) {
    Result result = {};
    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE, FALSE, pid);
    if (!hProcess) { result.error = L"OpenProcess 失败: " + WSTR(GetLastError()); return result; }
    DWORD nameBytes = (DWORD)((moduleName.length() + 1) * sizeof(wchar_t));
    LPVOID pRemoteName = VirtualAllocEx(hProcess, nullptr, nameBytes, MEM_COMMIT, PAGE_READWRITE);
    if (!pRemoteName) { result.error = L"VirtualAllocEx 失败: " + WSTR(GetLastError()); CloseHandle(hProcess); return result; }
    WriteProcessMemory(hProcess, pRemoteName, moduleName.c_str(), nameBytes, nullptr);
    auto pGMH = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetModuleHandleW");
    HANDLE t = CreateRemoteThread(hProcess, nullptr, 0, pGMH, pRemoteName, 0, nullptr);
    if (!t) { result.error = L"GetModuleHandleW 远程调用失败: " + WSTR(GetLastError()); VirtualFreeEx(hProcess, pRemoteName, 0, MEM_DECOMMIT); CloseHandle(hProcess); return result; }
    WaitForSingleObject(t, INFINITE);
    DWORD hMod = 0; GetExitCodeThread(t, &hMod); CloseHandle(t);
    VirtualFreeEx(hProcess, pRemoteName, 0, MEM_DECOMMIT);
    if (hMod == 0) { result.error = L"目标进程找不到模块 " + moduleName; CloseHandle(hProcess); return result; }
    auto pFL = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "FreeLibrary");
    t = CreateRemoteThread(hProcess, nullptr, 0, pFL, (LPVOID)(DWORD_PTR)hMod, 0, nullptr);
    if (!t) { result.error = L"FreeLibrary 远程调用失败: " + WSTR(GetLastError()); CloseHandle(hProcess); return result; }
    WaitForSingleObject(t, INFINITE);
    GetExitCodeThread(t, &result.exitCode); CloseHandle(t); CloseHandle(hProcess);
    result.success = true;
    logger::Info(L"已卸载 DLL PID=" + WSTR(pid));
    return result;
}

// ===== Injector-cap 风格: 加载 DLL + 远程调用 SetFlags 配置 =====
struct Flags { BOOL topmost; BOOL anticapture; };

Result InjectAndConfigure(DWORD pid, const std::wstring& dllPath,
                          bool topmost, bool anticapture,
                          bool importToolkitReg = true) {
    Result result = {};
    if (pid == 0 || pid == GetCurrentProcessId()) {
        result.error = L"无效 PID";
        return result;
    }

    // 可选: 先导入 Toolkit_fix.reg (极域置顶需要根证书修复)
    if (topmost && importToolkitReg) {
        static const wchar_t* kToolkitReg =
            L"Windows Registry Editor Version 5.00\r\n"
            L"\r\n"
            L"[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\SystemCertificates\\ROOT\\Certificates\\B759697225BF14DA7F67596D9A687058C4EBAC60]\r\n"
            L"\"Blob\"=hex:04,00,00,00,01,00,00,00,10,00,00,00,0d,1d,2a,93,62,b0,c1,d9,55,78,d7,20,09,b5,11,6b,0f,00,00,00,01,00,00,00,14,00,00,00,80,f4,32,d5,51,7d,e7,1e,9b,51,4f,d7,44,e1,94,db,20,2d,5d,38,14,00,00,00,01,00,00,00,14,00,00,00,d2,04,2a,6f,04,9f,40,9c,f5,3d,61,2c,53,d8,6b,03,a0,c7,d5,8a,19,00,00,00,01,00,00,00,10,00,00,00,3c,9b,6e,bb,32,e5,fc,7d,3b,47,2c,9c,43,48,ba,43,03,00,00,00,01,00,00,00,14,00,00,00,b7,59,69,72,25,bf,14,da,7f,67,59,6d,9a,68,70,58,c4,eb,ac,60,5c,00,00,00,01,00,00,00,04,00,00,00,00,04,00,00,20,00,00,00,01,00,00,00\r\n";
        wchar_t tempFile[MAX_PATH] = {};
        GetTempFileNameW(L"C:\\Windows\\Temp", L"tkf", 0, tempFile);
        wchar_t regPath[MAX_PATH] = {};
        wcscpy_s(regPath, wcslen(tempFile) + 1, tempFile);
        wchar_t* dot = wcsrchr(regPath, L'.');
        if (dot) *dot = L'\0';
        wcscat_s(regPath, MAX_PATH, L".reg");
        HANDLE hf = CreateFileW(regPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(hf, kToolkitReg, (DWORD)(wcslen(kToolkitReg) * sizeof(wchar_t)), &w, nullptr);
            CloseHandle(hf);
            std::wstring cmd = std::wstring(L"import \"") + regPath + L"\"";
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
            sei.lpVerb = L"runas"; sei.lpFile = L"reg.exe";
            sei.lpParameters = cmd.c_str(); sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 5000); CloseHandle(sei.hProcess);
            }
            DeleteFileW(regPath);
        }
    }

    // OpenProcess
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) { result.error = L"OpenProcess(ALL_ACCESS) 失败: " + WSTR(GetLastError()); return result; }

    // Step 1: VirtualAllocEx → WriteProcessMemory DLL path
    size_t pathBytes = (dllPath.length() + 1) * sizeof(wchar_t);
    LPVOID remoteStr = VirtualAllocEx(hProcess, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteStr) { result.error = L"VirtualAllocEx 失败"; CloseHandle(hProcess); return result; }
    if (!WriteProcessMemory(hProcess, remoteStr, dllPath.c_str(), pathBytes, nullptr)) {
        result.error = L"WriteProcessMemory(DLL路径) 失败"; VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE); CloseHandle(hProcess); return result;
    }

    // Step 2: CreateRemoteThread(LoadLibraryW) → 拿到远程 DLL 基址
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
                                         (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"),
                                         remoteStr, 0, nullptr);
    if (!hThread) { result.error = L"CreateRemoteThread(LoadLibrary) 失败"; VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE); CloseHandle(hProcess); return result; }
    WaitForSingleObject(hThread, 5000);
    DWORD remoteRet = 0; GetExitCodeThread(hThread, &remoteRet);
    CloseHandle(hThread);

    if (remoteRet == 0) {
        result.error = L"LoadLibraryW 在远程进程返回 NULL (DLL 加载失败)";
        VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE); CloseHandle(hProcess); return result;
    }

    // Step 3: 本地 LoadLibraryEx → GetProcAddress("SetFlags") → 计算偏移
    HMODULE localMod = LoadLibraryExW(dllPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!localMod) { result.error = L"本地 LoadLibraryEx 失败"; VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE); CloseHandle(hProcess); return result; }
    FARPROC localFunc = GetProcAddress(localMod, "SetFlags");
    if (!localFunc) { result.error = L"DLL 没有导出 SetFlags"; FreeLibrary(localMod); VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE); CloseHandle(hProcess); return result; }
    uintptr_t offset = (uintptr_t)localFunc - (uintptr_t)localMod;
    LPTHREAD_START_ROUTINE remoteFunc = (LPTHREAD_START_ROUTINE)((uintptr_t)remoteRet + offset);
    FreeLibrary(localMod);

    // Step 4: 远程分配 Flags struct → 写 → CreateRemoteThread(SetFlags)
    Flags f = { (BOOL)topmost, (BOOL)anticapture };
    LPVOID remoteFlags = VirtualAllocEx(hProcess, nullptr, sizeof(Flags), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteFlags) { result.error = L"VirtualAllocEx(Flags) 失败"; VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE); CloseHandle(hProcess); return result; }
    WriteProcessMemory(hProcess, remoteFlags, &f, sizeof(f), nullptr);
    HANDLE hCfgThread = CreateRemoteThread(hProcess, nullptr, 0, remoteFunc, remoteFlags, 0, nullptr);
    if (!hCfgThread) { result.error = L"CreateRemoteThread(SetFlags) 失败"; VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE); VirtualFreeEx(hProcess, remoteFlags, 0, MEM_RELEASE); CloseHandle(hProcess); return result; }
    WaitForSingleObject(hCfgThread, 3000);
    DWORD flagsRet = 0; GetExitCodeThread(hCfgThread, &flagsRet); CloseHandle(hCfgThread);

    VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, remoteFlags, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    result.success = true; result.exitCode = flagsRet;
    logger::Info(L"InjectAndConfigure PID=" + WSTR(pid) +
                 L" topmost=" + (topmost ? L"ON" : L"OFF") +
                 L" anticapture=" + (anticapture ? L"ON" : L"OFF"));
    return result;
}

} // namespace inject
