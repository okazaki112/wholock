<div align="center">

# WhoLock · 文件占用定位器

**「这个文件删不掉、改不了，到底被谁占着？」—— 单 exe 免安装，拖进来就知道**

[![Version](https://img.shields.io/badge/version-1.0.0-blue)](CHANGELOG.md)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2019045%2B%20%2F%2011-lightgrey)](#快速开始)
[![C++](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Build](https://img.shields.io/badge/build-CMake%20%3E%3D3.28%20%2B%20MSVC-069D62)](#从源码构建)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

*纯系统 Win32 API · 零第三方依赖 · 无网络无遥测 · `/MT` 静态 CRT 单文件*

</div>

---

## 目录

- [为什么需要它](#为什么需要它)
- [功能特性](#功能特性)
- [快速开始](#快速开始)
- [使用方法](#使用方法)
- [从源码构建](#从源码构建)
- [测试与门禁](#测试与门禁)
- [架构](#架构)
- [已知限制](#已知限制)
- [Roadmap](#roadmap)
- [版本与文档](#版本与文档)

## 为什么需要它

Windows 上最经典的僵局：U 盘弹不出、文件删不掉、目录清不了——资源管理器只告诉你"正在被其他进程使用"，**却不说是谁**。

WhoLock 用 **RestartManager + 句柄表 + 子进程隔离深扫** 三条通道定位占用者：给出 PID、进程名、完整路径、占用方式，并在安全护栏下提供结束进程 / 释放句柄 / 释放后删除的处置能力。

|  |  |
|---|---|
| 零安装 | 单个 exe（/MT 静态链接），拷到干净 Windows 双击即用 |
| 零依赖 | 仅系统 Win32 API，无第三方库、无网络、无遥测 |
| 双引擎 | RestartManager 快速通道 + 句柄表补盲通道，互为校验 |
| 安全护栏 | 系统进程名单拦死 + 危险模式默认关 + 二次确认，三层防线 |

## 功能特性

| 功能 | 说明 |
|---|---|
| 多引擎占用定位 | RM 官方通道 + 句柄表（NtQuerySystemInformation + DuplicateHandle）双通道合并去重 |
| 深度扫描 | 子进程隔离的全表句柄枚举，可发现 RM 看不见的**只读占用**（超时自动截断，挂死不传染父进程） |
| 多目标 / 目录 | 多行路径、整窗拖放、目录递归展开（深度 8 / 上限 2000），「占用文件」列精确定位到具体文件 |
| 句柄明细 | 选中进程即拉取其句柄清单（句柄值 / 访问权限 / 类型 / 路径） |
| 释放与处置 | 结束占用进程、关闭指定句柄、释放成功后可选删除文件（删除后验证消失） |
| 身份等价 | FileIdInfo 比对，8.3 短名 / 硬链接 / 软链接 / 卷 GUID 路径自动归一 |
| Explorer 右键集成 | HKCU 当前用户注册表，软件内一键开关、完全可逆 |
| CLI 模式 | 同体 exe `--cli` 切控制台，人读列表 / `--json` 机器输出、可选引擎 |
| DPI 感知 UI | PerMonitorV2 清单 + 原生 Win32 自绘，紧凑单窗布局 |

## 快速开始

**系统要求**：Windows 10 19045+ / 11（x64，UCRT 内置）；Win7/8 需先装 [KB2999226](https://support.microsoft.com/kb/2999226)。

**权限**：普通权限即可完成定位（RM 通道）；跨会话进程与部分系统进程需**以管理员身份运行**——非管理员时状态栏会黄字提示结果可能不全，但不会替你提权。

从 [Releases](../../releases) 下载 `wholock.exe`（或从 `发布版/<版本>/` 取构建产物），双击即用。

## 使用方法

### GUI

```text
1. 双击 wholock.exe
2. 拖入文件 / 目录到窗口（或多行输入路径，或点「浏览…」选定即扫）
3. 列表给出占用者 → 点选行自动拉取句柄明细
4. 勾选「启用危险操作」→ 结束进程 / 释放选中句柄（均有二次确认）
```

### CLI

```powershell
# 人读列表（双引擎）
.\wholock.exe --cli "D:\a\busy.txt"

# JSON 输出 + 仅 RM 引擎
.\wholock.exe --cli --json --engine=rm "D:\a\busy.txt"

# 多目标 + 目录自动展开
.\wholock.exe --cli "D:\a\b.txt" "D:\dir1" "D:\dir2"

# 版本号（与标题栏/关于框同源）
.\wholock.exe --cli --version
```

拖文件到 exe 图标上 = 填入路径并自动扫描。

## 从源码构建

**依赖**：Visual Studio 2022+ / Build Tools（含 C++ 桌面开发负载）、CMake ≥ 3.28。

```powershell
# 标准构建（configure + Release + ctest 全跑）
.\build.cmd

# 交付门禁（构建/警告/依赖方向/哨兵等机检）
.\scripts\check.ps1

# 一键发布：自动解析版本号，产物复制到 发布版/<版本>/
#   - 有 cmake.exe 走标准 CMake 构建（缓存指向旧目录时自动清理重配）
#   - 找不到 cmake.exe 自动回退 vcvars64 + cl.exe 直接编译
.\release.ps1
```

产物：`build\out\wholock.exe`（单文件，无 VC 运行时依赖）。

> **版本单一事实源**：`CMakeLists.txt` 的 `project(VERSION)` → `cmake/version.h.in` → 生成 `version.h`，标题栏 / `--version` / 关于框同源。

## 测试与门禁

```powershell
ctest --test-dir build -C Release --output-on-failure   # 一条命令跑全部
```

| 目标 | 判据 |
|---|---|
| `unit_tests` | domain 纯函数：路径归一化 / 合并 / kill 分级 / CLI 解析，含负样例 |
| `integration_engine_swap` | 引擎可插拔：Stub 换 RM/HT 桩，合并/降级语义不变 |
| `integration_self_lock` | **G1**：PowerShell 子进程真实写锁 → RM 必须报出该 PID，退出后必须消失 |
| `integration_identity` | **G6**：硬链接身份等价类（FileIdInfo 为尺 + 负对照防恒真） |
| `integration_release` | 深扫补盲 / 关句柄往返 / 释放后删除验证 / 取消兜底 |
| 其余 | 目录目标、右键集成、设置持久化等真跑集成样例 |

## 架构

<details>
<summary><b>分层与依赖方向（点击展开）</b></summary>

```text
ui_main.cpp ──▶ application/ScanCoordinator ──▶ domain(LockInfo/Normalize/Merge/RiskFlags/CliArgs)
                        ▲                                ▲
        infrastructure/win32(RmScanner, HandleTableScanner, WindowsPathMap,
                            Privileges, ProcessQuery, ProcessOps, Engines)
```

- **依赖严格单向**，domain/application 无 `<windows.h>`（`scripts/check-deps.ps1` 机检）
- **引擎接口** `ILockScanner` 两个真实实现 + 可插拔测试桩：
  - **RM**：RestartManager 官方通道，快；只覆盖"重启更新相关"占用（写锁/模块），只读共享句柄看不见
  - **HT**：句柄表快照 + DuplicateHandle + 路径解析，补盲通道；深扫走子进程隔离，预算超时截断并明示
- **安全护栏**（`domain/RiskFlags`）：PID≤4 / System / wininit / csrss / svchost / lsass / explorer / 本进程 一律拦下；Allowed 仍需二次确认
- **视觉层** `src/ui/Theme.*` 为设计令牌单一事实源（色板 / 字号级差 / hairline / owner-draw 按钮 / ListView customdraw / 状态带）
- **清单内嵌**：Common-Controls v6 + PerMonitorV2 + asInvoker，保持单 exe

</details>

## 已知限制

<details>
<summary><b>如实披露（点击展开）</b></summary>

- 目录扫描跳过符号链接重解析目录（防成环）
- UNC / 网络盘尽力支持；死映射句柄不计入结果
- 非 ASCII 路径比对按 ASCII 小写；NTFS 正确，区分大小写卷（WSL 挂载）未覆盖
- 非管理员运行时结果可能不完整，UI 黄字提示，不替目标提权
- 关闭远端句柄可能致占用方异常——危险模式 + 逐句柄确认约束下使用
- 系统存在挂起句柄时，全表枚举在预算（默认 25s）后截断，产出部分结果并明示

</details>

## Roadmap

- [ ] 深扫进度百分比与增量呈现
- [ ] MinGW `-static` 双工具链交叉验证
- [x] ~~Explorer 右键集成~~ → v1.0.0 已实装
- [x] ~~目录/批量目标~~ → v1.0.0 已实装
- [x] ~~RM 盲区（只读持有者）~~ → v1.0.0 深扫已实装

## 版本与文档

当前版本 **v1.0.0**，详见 [CHANGELOG.md](CHANGELOG.md)。更多实现细节与验收判据见 `实现方案.md`、`docs/manual-qa.md`。

## 许可证

本项目基于 [MIT License](LICENSE) 开源——可自由使用、修改、再分发（含商用），只需保留版权声明。软件按"现状"提供，**结束进程 / 关闭句柄等危险操作请自担风险**。
