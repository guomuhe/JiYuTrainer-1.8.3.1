// NTDExt.h - NoTopDomain 功能移植模块
// 集成所有 NoTopDomain 独有的反极域功能到 JiYuTrainer

#pragma once

#include "stdafx.h"

namespace NTDExt
{
    // === 进程操作 ===
    
    // 挂起指定 PID 的进程（调用 ntdll.NtSuspendProcess）
    bool SuspendProcess(DWORD pid);
    
    // 恢复指定 PID 的进程（调用 ntdll.NtResumeProcess）
    bool ResumeProcess(DWORD pid);
    
    // 通过遍历线程终止进程（TerminateThread，绕过反杀钩子）
    bool KillProcessByThread(DWORD pid);
    
    // 通过 ntsd.exe 杀掉进程
    bool KillProcessNTSD(const wchar_t* processName);
    
    // 通过进程名杀进程（TerminateProcess）
    bool KillProcessByName(const wchar_t* processName);
    
    // === 系统服务 ===
    
    // 停止 Windows 服务 (sc stop)
    bool StopService(const wchar_t* serviceName);
    
    // 启动 Windows 服务 (sc start)
    bool StartService(const wchar_t* serviceName);
    
    // === 注册表操作 ===
    
    // 解除映像劫持 (清除 Image File Execution Options)
    bool ClearImageFileExecutionOptions();
    
    // 解除组策略限制 (任务管理器/运行/注册表/CMD 等)
    bool ClearGroupPolicyRestrictions();
    
    // === 窗口操作 ===
    
    // 切换窗口置顶状态
    bool SetWindowTop(HWND hwnd, bool topmost);
    
    // 隐藏/取消隐藏焦点窗口（防截图）
    bool ToggleForegroundWindowHide();
    
    // 杀焦点窗口所在进程
    bool KillForegroundProcess();
    
    // 置顶/取消置顶焦点窗口
    bool ToggleForegroundWindowTop();
    
    // === 资源管理器 ===
    
    // 重启资源管理器 (explorer.exe)
    bool RestartExplorer();
    
    // === 极域特定功能 ===
    
    // 脱离远程关机 (替换极域 Shutdown.exe 为空程序)
    bool ToggleNoShutdown(const wchar_t* jiyuDir, bool enable);
    
    // 解除网站限制 (停止 TDNetFilter + 杀 gatesrv.exe/masterhelper.exe)
    bool ToggleWebsiteUnblock(bool enable);
    
    // 解除 U盘/软件限制 (停止 TDFileFilter)
    bool ToggleUSBDeviceLimit(bool enable);
    
    // 枚举并停止/删除所有极域厂商（Mythware/TDNetFilter/Skylark）的内核驱动
    // 驱动 unload 时内核自动清除 PsSet*NotifyRoutine 和 ObRegisterCallbacks 回调
    // 从而解除极域对 StudentMain.exe 的内核层进程保护
    // 返回: 成功 stop 的驱动数量
    int  ClearJyuKernelDriver();

    // 从嵌入的 BIN 资源解压 KernelCallbackCleaner.exe (x64) 并执行
    // 它通过 RTCore64.sys (MSI 漏洞驱动) 直接硬清 ntoskrnl 内的内核回调数组
    // 返回值: KernelCallbackCleaner.exe 的退出码 (0=成功)，-1 表示解压/启动失败
    int  RunKernelCallbackCleaner();

    // 解除极域防杀钩子 (杀 prochelper*.exe + FreeLibrary Hook DLL)
    bool UnlockJiyuAntiKill();
    
    // 解除键盘/鼠标限制 (极域键盘锁/鼠标锁)
    bool ClearKeyboardAndMouseLock();
    
    // === 实用工具 ===
    
    // 运行外部程序（以普通或管理员权限）
    bool RunCommand(const wchar_t* cmd);
    
    // 记录日志到 JiYuTrainer
    void LogMessage(const wchar_t* msg);
}
