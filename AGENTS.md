# QuickClash 开发约定

- 应用显示名 QuickClash；内部 project、程序基础名及命名空间根 quickclash。
- 先阅读 docs/progress/README.md、TASKS.md、CHECKLISTS.md 与 DEVLOG.md 的最新记录，按设计和依赖选择可验收任务，保留已有工作区改动。
- 遵循 docs/development/cpp_style.md：C++20、RAII、明确 QObject/资源所有权和异步生命周期；自有源码目录/文件 ASCII 小写下划线，类型名/Qt API 和工具固定名依规范例外。
- QML 文件同样小写下划线，CMake 显式 QT_QML_SOURCE_TYPENAME 导出类型；不绕过用户命名规则创建大写组件文件。
- 每个有意义开发交付同步任务状态/负责人/报告、真实验收结果及 DEVLOG 下一步；未运行的测试不得标 Pass，相关旧证据失效标 Stale。不为记进度自动提交或创建远端事项。
- 改设计后运行 scripts/build_design_docs.ps1，再运行同脚本 -Check；交付前运行 scripts/check_progress.ps1、scripts/check_source_layout.ps1 与相关构建/测试。文档/台账脚本通过不等于产品验收。
- Mihomo 只用官方预编译资产，版本/来源/摘要冻结后接入；不改造内核。客户端≤100 MB 是待实测硬目标，必要辅助进程计入，内核单列。
