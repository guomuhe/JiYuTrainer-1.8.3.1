// MythwareExt.h - 从 MythwareToolkit 移植的高级功能
// ACL 防杀 / CBT hook 对话框防截屏 / 键鼠解锁 / 极域控制 / 网络限制解除
#pragma once
#include <windows.h>
#include <string>

namespace mythware_ext {

// ── ACL 防杀进程 ────────────────────────────────────
// 通过修改进程 DACL 添加 Everyone DENY PROCESS_TERMINATE
// 任务管理器"结束任务"会返回"拒绝访问"
bool ToggleProcessProtection();
bool IsProcessProtected();

// ── 全局对话框防截屏 ────────────────────────────────
// WH_CBT 钩子：所有 HCBT_ACTIVATE 的 #32770 对话框
// 自动调用 SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)
void InstallDialogProtection();
void UninstallDialogProtection();

// ── 键盘/鼠标解锁 ──────────────────────────────────
// 开两个后台线程持续重装 WH_KEYBOARD_LL / WH_MOUSE_LL
// 并反复 ClipCursor(NULL) 解除鼠标锁
void StartInputUnlock();
void StopInputUnlock();
bool IsInputUnlockRunning();

// ── 极域进程控制 ────────────────────────────────────
// 杀 StudentMain + MasterHelper + GATESRV
bool KillMythwareProcess();
// 查找极域广播窗口并窗口化/全屏化
bool ToggleBroadcastWindow();
// 退出黑屏（4级递进）
bool ExitBlackScreen();
// 解除网络限制（TDNetFilter 驱动 + 杀进程 + 停服务）
bool RemoveNetworkRestrictions();
// 解除USB限制（停 TDFileFilter 服务）
bool RemoveUSBRestrictions();

// ── 密码读取 ────────────────────────────────────────
// 从注册表 XOR 解密极域学生端密码
bool GetMythwarePassword(std::wstring& outPassword);

// ── 辅助 ────────────────────────────────────────────
// 进程查找/查杀（公开给 UI 层直接调用）
DWORD FindProcessByName(const wchar_t* name);
bool KillProcessById(DWORD pid, bool force = true);
bool KillAllWithName(const wchar_t* name, bool force = true);

} // namespace mythware_ext
