# JiYuTrainer

<p align="center">不再被极域电子教室控制</p>

<p align="center">
  <img alt="language" src="https://img.shields.io/badge/language-C%2B%2B%2017-blue.svg">
  <img alt="build" src="https://img.shields.io/badge/build-CMake%20%2B%20MinGW-brightgreen.svg">
  <img alt="license" src="https://img.shields.io/badge/license-MIT-orange.svg">
</p>

---

## 简介

极域电子教室是许多学校机房使用的教学管理软件。它的全屏广播、黑屏安静、远程控制等功能，在某些场景下会让被控制者感到困扰。

本软件可以在**不破坏极域正常运行**的前提下，将全屏广播自动转为窗口模式，让你在老师演示的同时仍然可以自由操作电脑；同时提供反监视、防控制、自动关闭黑屏安静等功能。

由于是把全屏转为窗口，服务端看到的仍然是正常连接状态，不会暴露。

---

## 功能一览

| 功能 | 说明 |
|------|------|
| 全屏广播转窗口 | 被全屏广播时自动切为窗口模式，自由操作 + 看演示两不误 |
| 反监视 | 服务端无法远程查看你的屏幕 |
| 防控制 | 禁止服务端远程操作你的键鼠 |
| 自动关闭黑屏安静 | 检测到黑屏指令时自动恢复显示 |
| 强杀/启停极域进程 | 内置 StudentMain.exe 进程管理 |
| 破解卸载密码 | 支持新版极域的解锁卸载密码 |
| 远程命令拦截 | 监控极域远程执行命令，可选择放行或阻止 |
| UDP 攻击 | 可向指定 IP 发送 UDP 包（攻击窗口） |
| 防屏录 | 选择指定窗口禁止截屏/录屏（基于 `SetWindowDisplayAffinity`） |
| 圆形悬浮窗 | 始终置顶的小型控制入口，可拖可点 |
| 内核回调清除 | 内嵌 BYOVD 漏洞驱动，清除极域注册的内核回调 |

---

## 发布包结构

编译后的 Release 目录包含以下文件：

```
Release/
├── JiYuTrainer.exe          # 主程序（内嵌所有资源和依赖）
├── sciter.dll               # UI 渲染引擎
├── JiYuTrainerHooks.dll     # Hook 模块
└── JiYuTrainerDriver.sys   # 内核驱动
```

> 其他依赖（curl、mhook、MemoryModule、XZip 等）全部静态链接进 `JiYuTrainer.exe`，不再单独发布。

---

## 编译指南

### 环境要求

- **Windows 10/11**（32 位目标）
- **CMake 3.16+**
- **MinGW-w64 GCC 13**（i686，32 位）
  - 推荐：`D:\code\mingw32\bin\i686-w64-mingw32-g++.exe`

> 项目已不再使用 MSVC `.sln` / `.vcxproj`，全部迁移到 CMake。驱动模块 `JiYuTrainerDriver` 仍需 WDK 编译（可选，使用预编译的 `.sys` 即可）。

### 一键编译

```cmd
set PATH=D:\code\mingw32\bin;%PATH%
cd build32
mingw32-make -j4
```

产物位置：

```
build32/
├── bin/JiYuTrainer.exe
├── lib/libJiYuTrainerUI.a
├── lib/libJiYuTrainerHooks.dll.a
└── ...
```

### CMake 重新配置

如果需要重新生成构建文件：

```cmd
set PATH=D:\code\mingw32\bin;%PATH%
mkdir build32 && cd build32
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release ..
mingw32-make -j4
```

### 关键编译参数

| 参数 | 说明 |
|------|------|
| C++ 标准 | C++17 (`-std=gnu++17`) |
| C 标准 | C11 |
| 字符集 | Unicode + UTF-8 源码 + UTF-8 运行时 |
| `.rc` 资源 | GBK 编码（codepage=936），windres 编译 |
| 预处理器 | `WIN32`, `UNICODE`, `NOMINMAX`, `CINTERFACE` |

---

## 项目架构

```
JiYuTrainer-master/
├── CMakeLists.txt                  # 顶层 CMake
├── JiYuTrainer/                    # 主程序 (EXE)
│   ├── FloatingWindow.cpp          # 圆形悬浮窗 + 防屏录窗口列表 + PickOverlay
│   ├── TrainerWorker.cpp           # 核心对抗逻辑（全屏转窗口、反监视等）
│   ├── DriverLoader.cpp            # 驱动加载器
│   ├── KernelUtils.cpp             # 内核操作辅助
│   ├── MythwareExt.cpp             # 极域扩展接口层
│   ├── JyUdpAttack.cpp             # UDP 攻击实现
│   ├── inject.cpp                  # 进程注入
│   ├── window_utils.cpp            # 窗口工具（置顶同步、z-order）
│   └── ...
├── JiYuTrainerUI/                  # UI 层 (静态库, Sciter)
│   ├── MainWindow.cpp              # 主窗口
│   ├── HelpWindow.cpp              # 帮助窗口
│   ├── AttackWindow.cpp            # 攻击/扫描 IP 窗口
│   ├── BugReportWindow.cpp         # Bug 报告
│   ├── res/                        # PNG/BMP/ICO 资源
│   └── sciter-sdk/                 # Sciter 引擎（含 libsciter.a）
├── JiYuTrainerHooks/               # Hook 模块 (DLL)
│   ├── JiYuTrainerHooks.cpp
│   ├── disasm-lib/                 # x86 反汇编
│   └── mhook-lib/                  # inline hook
├── JyuHideHook/                    # 窗口隐蔽模块 (轻量 DLL)
├── JiYuTrainerUpdater/             # 自动更新 (静态库, 含 curl)
├── JiYuTrainerDriver/              # 内核驱动 (WDK 编译, 可选)
└── Release/                        # 发布产物
```

### 构建依赖顺序

```
JiYuTrainerUpdater  →  JiYuTrainerHooks  →  JyuHideHook  →  JiYuTrainerUI  →  JiYuTrainer
```

---

## 窗口层级策略

所有附属窗口通过 Windows **owned window** 机制跟随主窗口：

```
TOPMOST 层（同层内 z-order）
┌───────────────────────────────────┐
│ 圆形悬浮窗 (FloatingWindow)        │ ← 永久 TOPMOST，永远在最上
│ 悬浮窗 hidden owner                │ ← owned 于主窗口
│ 主窗口                            │ ← 400ms 刷 TOPMOST
│ HelpWindow / AttackWindow 等      │ ← owned 于主窗口，跟随置顶降级
└───────────────────────────────────┘
```

- 主窗口最小化 → 所有 owned 窗口自动隐藏
- 主窗口置顶 → 所有 owned 窗口自动置顶（悬浮窗保持最上）
- 点击窗口 → 被点击的自动到最前

---

## 第三方依赖

全部包含在项目中，无需额外安装：

| 库 | 用途 |
|----|------|
| [Sciter](https://sciter.com/) | UI 渲染引擎（HTML/CSS） |
| [mhook](https://github.com/martona/mhook) | inline hook |
| [MemoryModule](https://github.com/fancycode/MemoryModule) | 内存加载 DLL |
| [libcurl](https://github.com/curl/curl) | 自动更新模块 |
| [XZip-XUnZip](https://github.com/yuanjia1011/XZip-XUnZip) | 压缩/解压 |
| BYOVD | 内核回调清除驱动 |

---

## 已测试极域版本

- 2010 版（5.01.64.942 / 5.01 Baseline）
- 2015 豪华版
- 2016 豪华版（2.07.0.13488 / 2.07 CMPC）

## 系统要求

- **Windows 10 / 11**（64 位系统，32 位进程）

---

## 常见问题

**Q: 杀毒软件报毒？**
A: 本软件涉及进程注入、驱动加载等操作，可能被误报。可添加白名单或临时关闭杀毒。

**Q: 教师端限制了 U 盘？**
A: 先卸载极域的文件过滤驱动：
```cmd
sc stop TDFileFilter
sc delete TDFileFilter
```
然后再插入 U 盘。

**Q: 教师端限制了网络？**
A: JiYuTrainer 可自动卸载网络驱动。若无效，可使用 PCHunter 强制卸载 `TDNetFilter.sys`。

---

## License

[MIT License](LICENSE)
