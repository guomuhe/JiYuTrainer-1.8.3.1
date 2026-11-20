// NTDExt.h - NoTopDomain 功能移植模块
// 集成所有 NoTopDomain 独有的反极域功能到 JiYuTrainer

#pragma once

#include "stdafx.h"
#include <string>
#include <vector>

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
    
    // IFEO 扫描结果
    struct IfeoEntry {
        std::wstring exeName;     // 被劫持的 exe 名（如 "cmd.exe"）
        std::wstring debugger;    // debugger 值（劫持目标）
        bool ourProtect = false;  // 是否是我们自己加的保护（debugger 指向自身）
    };

    // 扫描所有 IFEO 子键，返回含 debugger 值的条目（区分系统合法调试器配置 vs 用户工具劫持）
    std::vector<IfeoEntry> ScanIfeoHijacks();

    // 清理指定 exe 的 IFEO 劫持子键
    bool ClearSpecificIfeo(const wchar_t* exeName);

    // 清理常见用户工具/安全工具的 IFEO 劫持（极域常用：cmd/taskmgr/regedit/PCHunter/ProcessExplorer/JiYuTrainer 等）
    // 保留系统合法调试器（如 VS GBD.exe、WinDbg、ntsd）的 IFEO 配置
    bool ClearCommonToolsIfeos(std::wstring* report = nullptr);

    // 给 JiYuTrainer.exe 加 IFEO 保护：如果被极域劫持，我们把 debugger 改成指向自身
    // 返回：true=已设置保护，false=无法访问注册表或已受保护
    bool ProtectSelfIfeo();

    // 检查 JiYuTrainer.exe 是否被 IFEO 劫持（非我们自己的保护）
    bool IsSelfIfeoHijacked(std::wstring* outDebugger = nullptr);

    // 解除映像劫持 (清除 Image File Execution Options 所有含 debugger 值的子键 —— 激进，会清除合法调试器配置)
    // 建议用 ClearCommonToolsIfeos 替代
    bool ClearImageFileExecutionOptions();
    
    // 解除组策略限制 (任务管理器/运行/注册表/CMD 等) —— 旧接口保持兼容
    bool ClearGroupPolicyRestrictions();

    // 组策略扫描（公开接口）：返回被极域等程序设置过的策略条目
    // 复用 IfeoEntry 结构：exeName = "HKLM\\subkey\\valueName"，debugger = 实际值或 "(数组类型)"
    std::vector<IfeoEntry> ScanPolicyHijacks();

    // 新接口：带扫描前备份 + DisallowRun 数组删除 + gpupdate /force 刷新
    // 返回清理成功的条目数，report 写入操作明细
    int ClearGroupPolicyRestrictionsEx(std::wstring* report = nullptr);
    
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
