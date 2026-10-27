# P0 实施与依赖兼容修复记录

> 历史记录：Windows 首版 P0/G0 已完成，当前以 [Windows 验收报告](20261024_windows_private_working_set.md) 和任务台账为准；下文是原实施时状态。

- 日期/执行者：2026-10-23 / Codex。
- 范围：P0-01—09，工作包 W00—W03/W08/W18；设计 1.4 的工程实施。
- 实现标识：原实施会话以未提交工作区新增 CMake/app/qml/src/tests、原生构建/资产/核心验证脚本、CI、docs/baseline。2026-10-24 核对时这些产物已在本地 HEAD `1ee26e6`（feat: bootstrap cross-platform QuickClash foundation），核对开始时工作区干净；本报告没有远端 CI run 证据。
- 结论：本报告保留P0工程与资产证据。P0-01/02/03的基线完成；P0-04/06/07/08/09当时尚缺必要验证，G0为Blocked。当前UI基准直接见[设计17](../../design/17-界面参考与QtQuick落地.md)。

后续接续记录见 [2026-10-24 P0 验证报告](20261024_p0_validation.md)，下面保留原实施/状态核对记录；新的局部通过不替代完整构建与 G0。

## 2026-10-24 状态对齐

此前任务台账停留在早期状态，没有反映本报告及已有代码。本次将 [任务台账](../TASKS.md) 对齐为 4 项 Done、5 项 Blocked，给每个 P0 任务补负责人、证据及可接续步骤；[验收清单](../CHECKLISTS.md) 仅勾选有完整基线证据的 G0-A，其余保留未勾选并说明局部结果。P0-07/08/09 已有实现或方案草稿，正在等待前置验证；P1—P5 尚未开始，MVP 仍为 0/16 Pass。

“基线任务 Done”只证明该任务的版本/资产/策略/参考交付完成，不代表 C++ 预检、客户端链接/启动或真实代理已通过。yaml-cpp 0.9.0 的策略声明检查通过，也不能替代 CMake 完整配置、编译与测试。下一项仍是 P0-06，随后 P0-04/08，再补 P0-07/09 与 G0。

本次未运行构建或内核探针。进度检查、源码命名检查、合并设计检查及 `git diff --check` 最终均退出 0；合并设计首次检查发现旧 1.3 产物过期，已根据现有 1.4 分册重新生成，并将入口中的过时实施状态改为指向进度台账。上述检查不增加任何产品通过结论。

## 交付

| 任务 | 实物/状态 | 剩余验证 |
|---|---|---|
| P0-01 | [版本矩阵](../../baseline/release_matrix.json)、[许可台账](../../baseline/licenses.md)；CMake 4.3.0、Qt 6.8.0、MSVC 19.50.35727.0、Ninja 1.12.1、LLVM 21.1.2、yaml-cpp 0.9.0 已固定 | P0-06 实际 ABI/链接/启动；候选系统不冒充已支持 |
| P0-02 | 官方 v1.19.33 归档与程序 SHA-256、来源/CPU、离线复核；Geo 明确不支持/无资产 | Linux/macOS 候选资产未执行；未称上游签名 |
| P0-03 | [配置策略](../../baseline/config_policy.json)、[夹具说明](../../baseline/test_fixtures.md)；明确 http/socks5/ss、select、普通规则，拒绝 Geo/providers/fake-ip/外部文件/未知字段/别名 | C++ preflight 及其正反测试属于 P0-08，尚未运行；真实转发属于 P2 |
| P0-04 | [真实契约](../../baseline/mihomo_contract.md)、API/退出码/重启证据 | Windows Job 与 OS 端口归属、无网络强制隔离未通过；任务 Blocked |
| P0-06 | CMake targets、snake_case QML 显式类型、QuickClash 空壳、presets、Windows 构建入口；Qt ≥6.8 可由 kit/用户 preset 发现，构建脚本支持显式 VS/Qt/Ninja 或沿用当前 MSVC 环境 | 完整构建/启动/cache/qmllint 仍未通过；工具链选择分支仅完成静态检查，任务 Blocked |
| P0-07 | Windows/Linux/macOS CI 配置、原生/headless/format/QML 检查 | 尚未运行 CI 和三平台；依赖 P0-06，当前只记录实现草稿 |
| P0-08 | domain/Operation/Error/上下文/接口、无 UI 测试目标、受限 YAML preflight/session；C++ 格式检查通过 | 未编译/运行 tests，前置 P0-04/06 未完成 |
| P0-09 | [构建和包方案](../../baseline/build_and_package.md)，插件/TLS/数据目录/异常恢复边界 | 文档已具备；构建命令与部署可执行性需 P0-06 证据 |

## 实际验证

| 命令/检查 | 结果与边界 |
|---|---|
| qmake -query QT_VERSION，cmake --version，ninja --version，clang-format/tidy --version | 版本如矩阵；编译器 ID 实际为 MSVC 19.50.35727.0，编译器 ID 小程序编译成功，不等于项目成功 |
| fetch_dependencies.ps1；随后 -Offline | 退出 0；yaml-cpp 正式 0.9.0 归档 SHA-256=298593d9c440fd9034b8b193d96318b76d49bc97c6ceadb7b0836edf0b6d7539；Mihomo 归档和程序摘要均符合矩阵 |
| CMake 4.3.0 -P，下载归档中原样提取的前三行声明 | 退出 0，cmake_minimum_required(VERSION 3.5...3.30) 被接受；未调用 project/编译/FetchContent 完整工程，不宣称完整兼容已实测 |
| probe_mihomo.ps1 -ApiOnly | 退出 0，认证/配置/编码组名/选择/模式/重载/重启通过；[summary](p0_core/summary.json)、[API](p0_core/api.json)；未更改系统代理，无真实节点流量 |
| 完整 probe 的 Get-NetTCPConnection | 拒绝访问；后续 -ApiOnly 显式跳过 OS 归属和 Job，报告为 NotRun，没有把跳过写 Pass |
| clang-format --dry-run --Werror 自有 C++ | 退出 0；已统一格式，不检查行为 |
| clang-tidy --verify-config | 退出 0；未运行真实编译数据库静态分析 |
| qmlformat -i 两个文件；qmllint flat_tile.qml | 退出 0；未完成带生成模块的 main.qml lint/cache/runtime |
| PowerShell Parser.ParseFile 全部脚本 | 无语法错误；不证明脚本全部分支运行 |

2026-10-23 后续工作区调整将固定 Qt/MSVC 参考环境与本地开发选择分离：CMake 不再要求 Qt 6.8.0 精确 patch，而要求 Qt ≥6.8；版本矩阵和正式验收仍固定 6.8.0。`build_windows.ps1` 的 Qt、VS 安装与 Ninja 路径改为可选输入。Windows PowerShell 解析、`check_progress.ps1`、`check_source_layout.ps1` 和 `git diff --check` 同批命令退出 0；后者只报告现有 DEVLOG/TASKS 的 LF→CRLF 工作区提示。`pwsh` 不在当前 PATH，首次检查命令退出 1，随后明确改用 `powershell.exe`。此次调整未运行构建，原构建失败/NotRun 结论不变；脚本新分支需要后续在所选 VS/Qt 环境实测。

## 构建故障与修复

用户定位的策略故障正确：项目自身 CMake ≥3.24 不冲突，yaml-cpp 0.8.0 声明 3.4；CMake 4 删除低于 3.5 的兼容，FetchContent 阶段会失败。升级正式 0.9.0（声明 3.5...3.30），版本化缓存名且 CMake/下载脚本读取同一清单；未修改上游文件、未全局加策略覆盖、未降低项目要求。另修复 Qt emit 宏与内部函数重名，补全 QStringList include 和 YAML 反例引号。

当前会话两套 CMake 的完整构建在受限环境编译探测/子 Ninja 阶段挂起，已中止本次进程；未获得源码编译结果。请求在正常本机环境构建时，自动审批服务两次返回 401 Unauthorized（无可用账户凭据），未执行请求；这是审批服务故障，不是构建成功或软件安全性判定。不得绕过该审批改用其他通道执行同一受阻构建。

解除条件：修复审批服务后运行 build_windows.ps1 -Fresh 和 -Headless，再运行完整 probe、qmllint/cache/smoke、clang-tidy；或用户自行执行文档中的同一命令并提供结果。Linux/macOS 需要实际原生 CI 结果。相关任务未标 Done，G0-D 不勾选，MVP 全部 NotRun。当前资源目标没有测量，未声明 ≤100 MB。

后续第一项是 P0-06 完整构建，随后 P0-04 Job/归属和 P0-08 契约测试；P0-07/09 依赖其实际通过。P1/P2 没有提前开发业务实现或改变系统代理。
