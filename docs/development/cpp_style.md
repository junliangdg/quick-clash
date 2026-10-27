# QuickClash C++ 与源码规范

应用显示名称为 **QuickClash**；仓库和 checkout 目录名约定为 `quick-clash`，本机目标工作路径为 `D:/repos/quick-clash`，实际迁移状态见 [迁移记录](../progress/reports/windows_validation.md#工程与内核)。CMake project、程序文件基础名、命名空间与内部产品 ID 使用 `quickclash`；连字符只用于仓库目录，不用作 C++ 标识符。组织域名/签名发布者待真实信息固定，不虚构包标识；首次实现数据路径前统一设置应用名，发行后变更须设计迁移。

本文是 QuickClash 的实现约定，采用 C++20、C++ Core Guidelines 中的 RAII/所有权/类型安全实践及 Qt 的对象/线程规则；不盲目启用所有现代语法或所有 clang-tidy 规则。设计文档中的概念接口以 [03](../design/03-数据模型与接口契约.md) 为行为契约，真实命名遵守本文件。

## 1. 目录、文件和标识符

| 对象 | 规则 | 示例 |
|---|---|---|
| 项目自有源码/脚本/资源目录与文件主干 | ASCII 小写 `snake_case`，只使用字母、数字与下划线 | `core_supervisor.cpp`、`proxy_group_model.h`、`flat_tile.qml`、`check_progress.ps1` |
| C++ 源码 | 头文件 `.h`，实现 `.cpp`；平台桥按需 `.mm` | `src/platform/macos/system_proxy.mm` |
| 命名空间/CMake 自有 target | 小写下划线；命名空间根为 quickclash | `quickclash::application`、`quickclash_core` |
| C++/QML 类型 | PascalCase，类型名独立于磁盘文件名 | `CoreSupervisor`、`FlatTile` |
| C++ 方法/函数/变量、Qt signal/slot/Q_PROPERTY、QML 属性/id | lowerCamelCase，与 Qt API 一致；不要求把标识符也改为文件命名风格 | `startProxy()`、`operationId`、`selectedProfile` |
| C++ 私有数据成员 | lowerCamelCase 后加下划线；小型值类型公开字段不加 | `engineEpoch_` |
| 枚举/常量 | `enum class` + PascalCase 枚举项；命名常量 `kPascalCase`，优先 constexpr | `EngineState::Ready`、`kDefaultMixedPort` |
| CMake 变量与宏 | Qt/工具规定值保持原名，自有宏少用且带 QUICKCLASH_ 前缀 | `CMAKE_CXX_STANDARD`、`QUICKCLASH_VERSION` |

工具固定文件名不改：`CMakeLists.txt`、`CMakePresets.json`、`CMakeUserPresets.json`、`CTestConfig.cmake`、`qmldir`、`.clang-format`、`.clang-tidy`、`.editorconfig`；其中用户本机 presets 不应提交。文档现有编号/中文和 README 文件不属于源码约束。第三方/官方 Mihomo 原始资源的名字与字节按上游清单保存；不要通过改第三方源码来满足检查。生成文件只在构建目录，不作为项目自有源码提交。其他例外必须在本文件和检查规则中精确记录原因，不直接跳过整个自有模块。

QML 源码也采用小写下划线，不依赖文件名自动导出类型。通过 CMake 为文件设置 `QT_QML_SOURCE_TYPENAME`，例如：

```cmake
set_source_files_properties(qml/components/flat_tile.qml PROPERTIES
    QT_QML_SOURCE_TYPENAME FlatTile
)
qt_add_qml_module(quickclash_ui
    URI QuickClash.Ui
    QML_FILES qml/components/flat_tile.qml
)
```

属性必须在同目录的 `qt_add_qml_module` 处理文件前设置；实际 backing target 按工程依赖建立。类型通过模块导入使用，避免本地目录自动发现大写组件；`main.qml` 可作为入口，导出时同样显式命名。`qmllint`、qmlcachegen 和运行加载均须在 P0/P1 验证。Qt 官方 [QT_QML_SOURCE_TYPENAME](https://doc.qt.io/qt-6/cmake-source-file-property-qt-qml-source-typename.html) 说明此属性自 Qt 6.2 提供；具体 Qt patch 仍在 P0 锁定，查到文档不等于已编译通过。

推荐结构：

```text
app/main.cpp
app/application_host.h
src/domain/engine_status.h
src/application/proxy_application.cpp
src/infra/core/core_supervisor.cpp
src/infra/storage/config_repository.cpp
src/platform/windows/system_proxy.cpp
src/presentation/proxy_group_model.cpp
qml/main.qml
qml/components/flat_tile.qml
qml/features/subscriptions/subscription_page.qml
qml/shell/page_host.qml
tests/unit/config_selection_test.cpp
cmake/project_warnings.cmake
scripts/check_source_layout.ps1
```

## 2. 生命周期、错误和并发

- 优先值语义、栈对象与 RAII；非 QObject 独占对象用 `std::unique_ptr`，确有跨所有者生命周期需求才用 `std::shared_ptr`。原始指针/引用表示借用，不隐含所有权；释放 OS handle/租约/临时资源有专用 RAII 封装。
- QObject 采用父子所有权或单一智能指针所有权，不能双重管理。跨异步回调使用有 context 的 connect、QPointer 或明确 lifetime token；跨线程销毁/调用遵守对象 affinity，不用捕获裸 this 的后台任务拖过宿主寿命。
- 使用 `nullptr`、`enum class`、`override`、const 正确性、明确初始化；查询结果优先 `[[nodiscard]]`，只在真实不抛出时标 `noexcept`。不用 memset 初始化非平凡类型，不用 C 风格转换、裸 new/delete 或手写无界缓冲。
- 使用 Qt 容器/字符串处理 Qt 边界，标准容器处理合适的领域值；避免同一数据在 JSON/Qt/JS 多次转换。`span`/`string_view`/Qt view 不跨越未知异步寿命，64 位数据按 03 安全展示。
- 运行错误走 03 的强类型 Result/Operation/Error；C++20 不依赖 C++23 `std::expected`，可用小型值类型/variant 实现。异常不跨 Qt signal/slot、事件循环或 IPC 边界；第三方异常在适配边界转换，坏输入不使用 assert 处理。内部不变量可 assert，发行仍必须验证外部输入。
- 错误路径和提交结果必须检查；取消/超时不等同已撤销，响应核对 generation/epoch/revision。UI 不同步等待网络、文件、进程或权限服务；标准 mutex 的临界区不等待异步 IO，不凭 `shared_ptr` 假定线程安全。
- 优先小职责类、显式依赖注入、单一状态权威与 target 分层；不用全局可变单例、宏元编程/模板泛化或 PImpl 仅为形式。只有 ABI、编译依赖或实测需求才增加额外抽象。
- 头文件自包含、`#pragma once`、不在头文件 `using namespace`；include what you use、不要依赖传递包含。注释解释不明显的原因、事务/所有权与单位，不重复代码表面意思；TODO 关联真实任务 ID。

## 3. 自动检查与编译约束

随仓库提供 [.clang-format](../../.clang-format)、[.clang-tidy](../../.clang-tidy)、[.editorconfig](../../.editorconfig)。格式基于 LLVM，4 空格、100 列、左侧指针符；clang-tidy 先启用基础缺陷/所有权/现代化相关小集合，实际规则是否适配 Qt 在锁定 LLVM 版本下核验。没有核验的工具输出不能冒称质量门通过。

P0-01 锁定格式器/clang-tidy 的版本，P0-06/07 建立实际可运行命令与 CI：

1. CMake targets 设置 CXX_STANDARD 20、CXX_STANDARD_REQUIRED ON、CXX_EXTENSIONS OFF，优先 target 级 include/defs/options 与显式依赖，不全局污染第三方编译选项。
2. 自有代码 GCC/Clang 用 `-Wall -Wextra -Wpedantic`，MSVC 用 `/W4 /permissive- /utf-8 /Zc:__cplusplus`；CI 自有 target 的 warning 视为错误，第三方/生成 Qt 代码按独立边界处理。不全局屏蔽警告掩盖类型/寿命问题。
3. `clang-format --dry-run --Werror` 检查自有 C++/Objective-C++ 文件；clang-tidy 读取真实 compilation database，在支持的构建配置执行，Qt 工具链不支持的组合列明，不假装运行。禁止一开始把全部 readability/performance 规则机械设为阻断。
4. QML 使用锁定 Qt 的 qmlformat/qmllint 与模块缓存构建检查，不能用 clang-format 处理 QML。格式差异与真实绑定/类型问题分开处理。
5. 运行 `pwsh -NoProfile -File scripts/check_source_layout.ps1` 校验项目自有目录/文件命名；无源文件时脚本明确报告尚无产品源码，不构成 P0 完成证据。
6. CI 保留相关单元/契约/集成测试和 Release 构建；可用平台增加 ASan/UBSan 检查所有权/边界，TSan 只在支持环境按需使用。测试依行为和风险选择，已有 14 的阶段要求不重复造镜像测试。

风格/命名检查可以随每次修改运行；GUI/内核/系统代理/资源验证须按进度台账保存真实证据，参见 [开发进度入口](../progress/README.md)。

## 4. 逐项开发与文档同步

每完成一项开发任务或可独立说明的改动，先同步受影响的设计/使用说明、TASKS、CHECKLISTS 与 DEVLOG，有相关专项报告时更新其结论，再开始下一项。只更新受影响记录；多文件共同构成一个修复时作为一项记录，不逐文件记流水账。日常改动不生成 report，也不把历史输出另写成 Markdown。

设计目录只维护目标、架构、技术契约、设计决策和验收标准；同步设计时直接修订对应方案，不追加日期式开发记录、优化批次、当前任务状态、构建/测试结果或实测数字。过程写 DEVLOG，当前状态维护在 TASKS/CHECKLISTS；常规改动在 DEVLOG 留版本、验证命令、结果和限制，任务证据可链接具体条目或 Actions 运行。reports 只保留阶段验收、重要调查、发行核验及性能对比，阶段门/MVP 仍用逐项结论报告。

原始日志、JUnit/CSV、批量截图和临时夹具写入 Git 忽略的 build/ 或 CI artifacts（默认14天）；长期证据人工精选到 reports/evidence/ 并记录来源、版本与限制。保留重要失败事实，不永久保存每次输出；重复报告可合并并修复引用，未关闭问题和不可替代证据须保留。详见[留存规则](../progress/reports/README.md)。

实现完成但尚未验证，先记录实现位置、变更行为、负责人及“验证中/NotRun”；完成构建、测试或修复后，立即补记实际结果与证据、失效的旧结果和下一步。只有真实满足完成条件才标 Done/Pass。只更新受影响文档，文档同步不触发无关产品构建、不自动提交代码或创建远端事项。
