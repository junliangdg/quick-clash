# QuickClash 开发约定

- 应用显示名 QuickClash；内部 project、程序基础名及命名空间根 quickclash。
- 仓库与 checkout 目录名约定 quick-clash；移动仓库后使用新的构建目录，缓存按摘要复核。源码子目录/文件仍遵循下述小写下划线约定。
- 先阅读 [进度约定](docs/progress/README.md) 与 [当前任务](docs/progress/TASKS.md)，再按范围读取相关设计；按实际依赖选择任务，保留已有工作区改动。
- 遵循 [源码规范](docs/development/cpp_style.md)：C++20、RAII、明确 QObject/资源所有权和异步生命周期；自有源码目录/文件 ASCII 小写下划线，类型名/Qt API 和工具固定名依规范例外。
- QML 文件同样小写下划线，CMake 显式 QT_QML_SOURCE_TYPENAME 导出类型；不绕过用户命名规则创建大写组件文件。
- 文档职责、最小交付记录与按风险选择验证统一遵循 [进度约定](docs/progress/README.md)；设计只维护稳定方案，交付时简记 TASKS，不为记进度自动提交或创建远端事项。
- 功能开发优先：代码交付做一次 Windows 增量构建和受影响验证；纯文档不构建产品。检查范围、扩展条件与复跑上限见 [停止规则](docs/progress/README.md#日常范围与停止规则)，发布核验见 [CHECKLISTS](docs/progress/CHECKLISTS.md)。
- 原始输出与临时夹具写 Git 忽略的 build/ 或 CI artifacts；长期证据、失败保留与归档遵循 [留存规则](docs/progress/reports/README.md#留存规则)，秘密不得入库。
- 改设计后运行 scripts/build_design_docs.ps1，再运行同脚本 -Check；交付前运行 scripts/check_progress.ps1、scripts/check_source_layout.ps1 与相关构建/测试。文档/台账脚本通过不等于产品验收。
- Mihomo 只用官方预编译资产，版本/来源/摘要冻结后接入；不改造内核。客户端以小内存占用为持续设计目标；保持Qt Quick默认RHI/GPU渲染，必要辅助进程计入客户端统计，内核单列；专用工作集、总工作集和私有提交同时记录，同环境比较并检查缓存有界与生命周期。首版优先 Windows 本地验收，跨平台原生构建与 CI 执行后置 P4。
