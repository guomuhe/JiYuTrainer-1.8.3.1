# JiYuTrainer v1.8.3.5 功能冗余（降级）方案

> 目标：每个功能都不是“一条路走到黑”，而是 **主方案 → 备选方案 → 兜底方案** 的链式执行，
> 任一层失败都能自动降级并把失败原因上报，避免出现“点了没反应、也不知道为什么”的情况。

## 0. 设计原则

| 原则 | 说明 |
|---|---|
| 降级优先 | 失败不抛异常、不弹崩溃框，按链继续尝试下一方案 |
| 可诊断 | 每次尝试记录 `方案名 / 结果 / GetLastError`，UI 状态栏显示“已降级(2/3)” |
| 幂等 | 同一功能重复执行不产生副作用（隐藏/置顶/停服务都要可重复调用） |
| 可回滚 | 所有“解除/停止/修改”类操作都要有对应的恢复路径 |
| 权限前置 | 需要管理员的方案先探测 `IsUserAnAdmin()`，不要静默失败 |
| 不碰内核签名 | 驱动方案仅作可选，失败必须能退回用户层（现有 `EnableDriverOnModernWindows` 守卫） |

## 1. 现有功能与冗余方案

### 1.1 注入 Hook DLL（`TrainerWorkerOpBoomInjectHook`）

| 层 | 方案 | 说明 / 限制 |
|---|---|---|
| L0 | `CreateRemoteThread + LoadLibraryW`（现有 `InjectDll`） | 通用；对 PPL/受保护进程无效 |
| L1 | `QueueUserAPC` 到目标 alertable 线程 | 目标无线程时不适用，比 L0 隐蔽 |
| L2 | `SetWindowsHookEx(WH_GETMESSAGE)` 注入 | 仅 GUI 进程 |
| L3 | 挂起线程 + `SetThreadContext` 改 EIP 到 `LoadLibraryW` | 易崩，最后手段 |
| 诊断 | 位数不匹配（32↔64）时直接换对应架构 DLL | 现有 `IsWow64Process` 校验可复用 |

**新增要求**：注入前先按目标位数选择 `JiYuTrainerHooks.dll` / `JiYuTrainerHooks64.dll`，避免“注入成功但模块没加载”。

### 1.2 卸载 / 强制卸载 Hook DLL

| 层 | 方案 |
|---|---|
| L0 | 远程调用 DLL 导出 `UninstallAll`，再由其自行 `FreeLibrary` 延迟 |
| L1 | 远程 `FreeLibrary`（现有 `UnInjectDll`） |
| L2 | 若仍被占用：结束并重启极域学生端（业务可接受） |

### 1.3 挂起 / 恢复极域进程

| 层 | 方案 | 备注 |
|---|---|---|
| L0 | `NtSuspendProcess` / `NtResumeProcess`（ntdll 动态解析） | 最稳，无需遍历线程 |
| L1 | Toolhelp 遍历线程逐个 `SuspendThread` | 需要线程句柄权限 |
| L2 | 驱动 `CTL_SUSPEND_PROCESS / CTL_RESUME_PROCESS` | 仅驱动可用时 |
| L3 | 降级为“停服务 + 结束进程” | 会掉线，作为兜底 |

**副作用提示**：挂起会让教师端画面定格、指令积压，恢复后可能瞬间执行积压指令。

### 1.4 解除 U 盘 / 软件限制（TDFileFilter）

| 层 | 方案 | 备注 |
|---|---|---|
| L0 | `sc stop TDFileFilter`（服务名做候选列表探测） | 需管理员 |
| L1 | `CreateFile(\\\\.\\TDFileFilter) + DeviceIoControl` 卸载设备 | 需驱动支持 |
| L2 | 注册表 `Start=4` 后重启服务 | 重启后失效 |
| L3 | 提示用户手动处理（不 rename/删驱动文件） | 避免蓝屏 |

### 1.5 解除网站限制（TDNetFilter）

同 1.4，服务名候选：`TDNetFilter` / `TDNetFilterService` / `JiYuNetFilter`。
补充 L2：`netsh wfp show filters` 只做诊断输出，不做非法删规则。

### 1.6 解除映像劫持（IFEO）

| 层 | 方案 |
|---|---|
| L0 | 枚举 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\*`，删除指向极域调试器的 `Debugger` 值（先备份到 `%TEMP%`） |
| L1 | 同时清理 `Wow6432Node` 分支 |
| L2 | 无法删除（权限）时提示并给出 .reg 备份路径 |

### 1.7 解除组策略限制

| 层 | 方案 |
|---|---|
| L0 | 定位并删除极域下发策略键（`Software\Policies`、`Explorer\DisallowRun` 等），先导出 |
| L1 | `gpupdate /force` 使清理生效 |
| L2 | 提示需注销/重启 |

### 1.8 脱离远程关机 / 重启

| 层 | 方案 | 备注 |
|---|---|---|
| L0 | Hook `ExitWindowsEx`（现有 L1060） | 只覆盖被注入进程 |
| L1 | 增加 hook `InitiateShutdownW` / `NtShutdownSystem` | 补齐漏网 API |
| L2 | 在广播/控制窗口处理 `WM_QUERYENDSESSION → FALSE` | 仅本进程窗口 |
| L3 | 从源头阻断：停服务或挂起极域 | 会失去广播，需用户确认 |
| L4 | 注册表 `NoClose` | 只影响开始菜单，**不算真正拦截**，仅作心理兜底 |

### 1.9 解除键盘鼠标锁定

| 层 | 方案 |
|---|---|
| L0 | Hook `SendInput` / `mouse_event` / `BlockInput`（现有） |
| L1 | `UnhookWindowsHookEx` 清极域键盘钩子链 |
| L2 | 安装同级低级钩子并返回 0 覆盖 |
| L3 | 结束极域控制进程（掉线兜底） |

### 1.10 隐藏焦点窗口（防截屏 / 让老师看不到特定画面）

| 层 | 方案 | 备注 |
|---|---|---|
| L0 | 本进程窗口：`SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)`，失败回退 `WDA_MONITOR` | Win10 2004+ 才有 EXCLUDE |
| L1 | 跨进程：注入 `JyuHideHook.dll` → 远程 `RemoteSetAffinity`（本次已实现） | 需要与目标同位数 |
| L2 | 32/64 位自动匹配 DLL（新增 `JyuHideHook64.dll`） | 避免注入失败 |
| L3 | 窗口移到屏幕外 + `ShowWindow(SW_MINIMIZE)` | 通用，但用户体验差 |
| L4 | 注入极域进程 hook `CreateDCW` / `GetWindowDC` / `BitBlt` / `PrintWindow` / `EncodeToJPEGBuffer`（已有部分 hook 点） | 能挡住整屏采集 |

**已知边界**：WDA 只对本进程顶层窗口生效；对镜像驱动/内核采集无效（需 L4 或内核方案）。

### 1.11 置顶焦点窗口

L0 `SetWindowPos(HWND_TOPMOST)` → L1 定时重申（`CKInterval`）→ L2 同时清 `WS_EX_NOACTIVATE`。
现有 `ManualTop` 已具备定时器骨架，只需把“失败后重试次数”纳入上报。

### 1.12 杀进程（焦点进程 / 按名称）

| 层 | 方案 | 备注 |
|---|---|---|
| L0 | `TerminateProcess` | 遇 PPL 失败 |
| L1 | 逐个 `TerminateThread` 后 `TerminateProcess`（现有 `TrainerWorkerOpTermByThread`） | |
| L2 | `ntsd -c q -p <pid>`（现有 `TermByNTSD`） | 依赖调试器存在 |
| L3 | 驱动 `CTL_KILL_PROCESS / CTL_TREMINATE_PROCESS` | 需驱动 |
| L4 | 停服务 + 禁用启动项，防 `ProcHelper64` 秒级复活 | **必须先停 `STUDSRV`** |

### 1.13 屏幕广播 / 黑屏窗口处理（FakeFull、防黑屏）

| 层 | 方案 |
|---|---|
| L0 | 枚举窗口按标题/类名识别，去 topmost、改尺寸（现有 `CheckWindowTextIsGb` / `FixWindow`） |
| L1 | 定时重申（`CKInterval`=3100ms） |
| L2 | 把窗口移出屏幕或最小化 |
| L3 | 结束对应进程 |

### 1.14 内核驱动加载与自我保护

| 层 | 方案 |
|---|---|
| L0 | 创建服务 + `StartService`（现有） |
| L1 | 失败码映射为人话并写日志（如 577 签名、1275 被拦截） |
| L2 | 直接降级为用户层模式（F-07 守卫已实现，需补充 UI 提示） |
| L3 | 手动映射加载（不推荐，签名/BSOD 风险，默认不启用） |

### 1.15 自保护 / 看门狗

L0 双进程互拉 → L1 计划任务守护 → L2 被结束后下次启动自动恢复配置与状态。

### 1.16 UDP 远程指令

保持 **默认关闭**（F-08 已实现），仅在配置显式开启且二次确认时可用；不做默认自动重试。

## 2. 统一降级框架（建议实现）

```cpp
enum class OpResult { Ok, Denied, NotSupported, Retryable, Fatal };

struct Attempt {
    const wchar_t* name;   // 方案名，如 L1:QueueUserAPC
    OpResult       result;
    DWORD          error;  // GetLastError()
};

// 每个功能实现为「方案链」，RunOperation 内部统一驱动
OpResult RunChain(TrainerWorkerOp op, std::vector<Attempt>* trace);
```

- 所有 `TrainerWorkerOp` 走同一入口，UI 只需读取 `trace` 即可显示“已降级(方案 2/3)，原因：…”。
- 日志统一格式：`[op] L1 QueueUserAPC -> Retryable err=5`。
- 新增诊断面板：列出每个功能的可用层（驱动可用 / 管理员 / 位数匹配）。

## 3. 新增功能建议（按优先级）

| 优先级 | 功能 | 价值 | 实现要点 |
|---|---|---|---|
| P0 | 能力自检面板 | 用户能看懂“为什么没生效” | 探测管理员、驱动、位数、hook 注入状态，可导出报告 |
| P0 | 32/64 位 DLL 自动匹配 | 消除最常见注入失败 | 复用本次 `JyuHideHook64` 构建方式 |
| P0 | 一键“脱控”/一键恢复 | 把 10 个按钮合成一个开关 | 按依赖顺序批量执行，逐项显示结果 |
| P1 | 功能被回滚后自动重申 | 极域会改回窗口样式/重启服务 | 轻量定时器 + 差异检测，避免高频轮询 |
| P1 | 教师端通信阻断（可选） | 从源头断掉关机/广播指令 | 用户显式开启，先做“仅本机端口”规则 |
| P1 | 进程白名单/防误杀 | 降低“杀焦点进程”误伤 | 内置系统关键进程保护名单 |
| P2 | 诊断包导出 | 便于远程排障 | 打包日志 + 注册表备份 + 环境信息 |
| P2 | 广播窗口自动策略 | 少手动按 Alt+F | 识别到广播窗口后自动应用用户偏好 |

## 4. 风险与约束

- **不做免杀**，不改 `vendor/`，不改跨 DLL ABI。
- 保留协议串 `"JiYu Trainer Virus Window"` 与 INI 键 `"DoNotShowVirusWindow"`。
- 驱动相关方案必须可完全降级到用户层，且默认在 Win10+ 关闭。
- 所有“解除限制”类操作前先做备份（注册表导出 / 服务原状态记录），便于回滚。

## 5. 落地顺序

1. 引入 `RunChain` + `Attempt` 框架，先接入 3 个功能（隐藏窗口、解除键鼠、脱离关机）验证模式。
2. 补齐 32/64 位 DLL 双架构与自动选择。
3. 其余功能按 1.1–1.16 的层逐一接入。
4. 最后做能力自检面板与一键脱控。
