# QuickClash 开发约定

- 应用显示名 QuickClash；内部 project、程序基础名及命名空间根 quickclash。
- 仓库与 checkout 目录名约定 quick-clash，目标工作路径 D:/repos/quick-clash；实际迁移状态见 docs/progress/reports/20261024_repository_rename.md。源码子目录/文件仍遵循下述小写下划线约定。
- 先阅读 docs/progress/README.md、TASKS.md、CHECKLISTS.md 与 DEVLOG.md 的最新记录，按设计和依赖选择可验收任务，保留已有工作区改动。
- 遵循 docs/development/cpp_style.md：C++20、RAII、明确 QObject/资源所有权和异步生命周期；自有源码目录/文件 ASCII 小写下划线，类型名/Qt API 和工具固定名依规范例外。
- QML 文件同样小写下划线，CMake 显式 QT_QML_SOURCE_TYPENAME 导出类型；不绕过用户命名规则创建大写组件文件。
- 每个有意义开发交付同步任务状态/负责人/报告、真实验收结果及 DEVLOG 下一步；未运行的测试不得标 Pass，相关旧证据失效标 Stale。不为记进度自动提交或创建远端事项。
- 每完成一项开发任务或独立改动，先同步更新对应设计/使用说明、TASKS、CHECKLISTS、报告及 DEVLOG 中受影响的记录，再继续下一项；不把多项完成后的文档集中留到会话结束。实现已完成而验证未完成时明确写“验证中/NotRun”，验证或修复产生新结果后再次同步；仅更新相关文档，不为凑记录改动无关文档。
- 功能开发优先：代码交付做一次 Windows 增量构建和受影响的功能/现有测试；文档改动不重跑产品构建。资源敏感修改选单个10—30秒短测，有效证据可复用；长时/多轮/压力/帧/GPU专项按需后置，不为其阻塞下一功能。G1范围见设计1.6与20261024_development_first报告。
- 改设计后运行 scripts/build_design_docs.ps1，再运行同脚本 -Check；交付前运行 scripts/check_progress.ps1、scripts/check_source_layout.ps1 与相关构建/测试。文档/台账脚本通过不等于产品验收。
- Mihomo 只用官方预编译资产，版本/来源/摘要冻结后接入；不改造内核。Windows 客户端进程组专用工作集≤100 MB 是待实测硬目标，必要辅助进程计入，内核单列；总工作集/私有提交同时记录，非硬门槛。首版优先 Windows 本地验收，跨平台原生构建与 CI 执行后置 P4。
