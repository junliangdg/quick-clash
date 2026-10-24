# AutoUic 误扫描修复记录

- 日期：2026-10-24（Asia/Shanghai）。
- 关联任务：P0-06；执行者：Zoo。
- 版本标识：D:/repos/quick-clash 当前未提交工作区上的增量修改；保留此前 P0/P1 及其他已有改动，不创建提交。
- 范围：修正 Designer 扫描开关，不修改产品设计、业务代码、文件命名或依赖版本。

## 1. 失败证据与根因

用户提供的 Windows build/dev 日志中，Ninja 在 `[21/148]` 的 quickclash 和 `[28/148]` 的 ui_state_test 自动生成步骤失败，最终退出 1。两个错误分别来自 [application_host.h](../../../app/application_host.h) 和 [ui_state.cpp](../../../src/presentation/ui_state.cpp) 对 `presentation/ui_state.h` 的包含；AutoUic 将其解释为 `state.ui` 的生成头文件，查找表单失败。

实际 [ui_state.h](../../../src/presentation/ui_state.h) 是手写 QObject/QML 状态类型，并非 uic 输出。工程使用 Qt Quick/QML，未声明 Designer 表单。根因是 [CMakeLists.txt](../../../CMakeLists.txt) 的 `qt_standard_project_setup()` 默认开启 CMAKE_AUTOUIC；按 `ui_*.h` 规则扫描时发生命名碰撞，不是头文件缺失，也不是需要补表单。Qt Widgets 仍用于托盘/菜单，不需要 Designer 扫描。

上述失败为用户日志证据，本轮没有重现构建。日志未给出完整编译器/Qt patch 信息，本轮不推测工具链版本。此前 ABI 检测超时和审批服务失败仍是历史记录，不覆盖此次新失败。

## 2. 增量修复与影响

在 Qt 标准设置之后、主程序及后续测试目标创建之前设置 `CMAKE_AUTOUIC OFF`。关闭位置保证不会被标准设置重新启用，并让 quickclash 和 ui_state_test 等后续目标采用同一默认值。

- 保留 AUTOMOC 与 QObject 元对象生成，不使用 SKIP_AUTOGEN。
- 保留 QML 模块、类型注册、资源和缓存配置。
- 不重命名手写头文件，不创建虚假的 state.ui，不逐个源文件绕过扫描。
- 不修改生成目录、构建缓存或第三方源码；后续重新配置才能更新现有构建树的目标属性。
- 未新增运行时分配或进程，资源影响仅涉及构建期扫描；未进行性能测量，不宣称满足 ≤100 MB。
- 本次不涉及异步、取消、迟到响应或业务恢复逻辑；实际构建失败恢复需后续原生验证。

## 3. 验证边界

| 检查 | 结果 | 说明 |
|---|---|---|
| 用户原构建 | Fail | 上述用户日志退出 1，两个目标 AutoUic 报错；不是修复后运行 |
| 进度一致性检查 | Pass | check_progress.ps1 退出 0，任务依赖、报告链接及验收记录一致 |
| 源码命名检查 | Pass | check_source_layout.ps1 退出 0，覆盖 28 个目录/59 个文件，其中产品源码 39 个 |
| 差异空白检查 | Pass | git diff --check 退出 0；只有已有工作区的 LF→CRLF 提示 |
| 修复后配置/自动生成/完整构建 | NotRun | 用户约定不构建，本轮也不执行配置或单独自动生成 |
| CTest/QML lint/启动/GUI/资源验收 | NotRun | 本轮未运行，不沿用历史静态检查作为本补丁构建证据 |

实际执行命令（仓库根目录，cmd.exe）：

```bat
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/check_progress.ps1 && powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/check_source_layout.ps1 && git diff --check
```

串联命令退出 0。首次检查时 P0-06 为 Doing，实施收尾后回到 Blocked；不以此脚本的 Pass 表示产品任务完成。

本次未改变设计，无需重建合并设计文档。文档/命名/差异检查不等于产品验收；没有相关既有阶段或 MVP Pass 需要撤销，历史原始记录保留。

## 4. 台账与下一步

P0-06 由 Doing 回到 Blocked，负责人 Zoo；修复已写入，必要的原生构建验证按约定未运行，不标 Done。P0 仍为 4 Done/5 Blocked，G0/G1 保持 Blocked，MVP 仍为 0/16。

下一任务仍为 P0-06：在允许构建的会话中重新配置现有构建树（或使用干净构建目录），验证主程序及状态测试不再执行 Designer 扫描，随后完成完整构建、现有 CTest、QML 检查及启动。其他潜在编译/运行错误不在本轮未运行的验证结论内；不要求为本修复删除整个构建目录。
