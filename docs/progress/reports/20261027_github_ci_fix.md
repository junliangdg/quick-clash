# GitHub三平台CI配置核验与修复

- 日期：2026-10-27；执行者：Codex；关联P0-07构建工具维护、P4-06跨平台CI。
- 状态：Review；配置和确定性编译阻断已修复，本地Windows构建及分批测试通过；GitHub远端重跑及Linux/macOS原生构建NotRun。保留已有工作区改动，不提交或推送。

## 已确认问题

- 用户提供的Windows/macOS日志在`pip install ninja==1.12.1`失败；PyPI在线元数据确认没有此包版本，1.13.2提供三平台wheel。
- Qt官方6.8.0包索引确认Linux架构为`linux_gcc_64`，原工作流错误指定`gcc_64`；Windows为`win64_msvc2022_64`，macOS为`clang_64`。
- Qt Action安装发生在项目Python设置之前，且其默认`setup-python`会自行选择Python；项目需先设置解释器并关闭Action内部设置。
- Windows只获取YAML便先运行runtime测试，Mihomo/Geo直到测试后才下载；完整资源必须提前准备。
- 非Windows的OwnedProcess明确返回CorePlatformUnsupported，不能运行要求Windows官方内核与隔离进程的runtime集成测试；保留原生构建与跨平台单元/UI测试。

## 在线来源与限制

- [Ninja PyPI元数据](https://pypi.org/pypi/ninja/json)。
- [Qt Action参数](https://github.com/jurplel/install-qt-action/blob/v4/action.yml)及[架构说明](https://github.com/jurplel/install-qt-action/blob/v4/README.md)。
- [Linux Qt 6.8.0索引](https://download.qt.io/online/qtsdkrepository/linux_x64/desktop/qt6_680/qt6_680/Updates.xml)、[macOS索引](https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_680/qt6_680/Updates.xml)、[Windows索引](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_680/qt6_680/Updates.xml)。
- 初次两次网络升级因自动审批服务401未执行；用户再次要求重试后成功。最初Qt索引URL遗漏一层目录返回404，修正后下载成功。
- 仓库Actions API未认证请求返回404，不能读取完整运行或触发重跑；不据此判断仓库不存在，也不声称远端通过。

## 验证

- 工作流已修复Ninja/Qt架构、Python/aqt固定、Windows完整资源先于测试、非Windows明确排除Windows内核runtime测试；增加依赖脚本回归与JUnit/aqt日志上传。Python统一3.14，符合当前Qt Action关于文件类型识别的上游建议，关闭其内部setup-python。
- CMake通过`qmake -query QT_INSTALL_TRANSLATIONS`定位翻译目录，避免Linux库目录及macOS framework布局错误。Windows重配置与翻译资源编译成功。
- actionlint1.7.12已按官方发布API的SHA-256核验；首轮发现`--only-binary=:all:`在单行YAML中的冒号错误，改多行run后校验退出0，见[actionlint日志](github_ci_evidence/actionlint_final.log.json)。
- 完整依赖离线校验及11项获取回归通过，见[资产日志](github_ci_evidence/assets.log)、[获取测试](github_ci_evidence/fetch_tests.log)；[进程执行器回归](github_ci_evidence/native_process_tests.log)通过。
- 首轮沙箱不能执行现有Ninja；授权后全目标增量构建在既有`provider_stream_test`第79行失败：QHash没有first方法。见[构建失败记录](github_ci_evidence/build_elevated.log)。本轮继续修复此确定性CI阻断并复测。
- 改为按`rule-providers:a`键取借用设备并检查非空，保留原断言语义；[最终全目标构建](github_ci_evidence/build_final.log)退出0，Windows/Qt6.9.0/MSVC14.51，CMake重配置与翻译资源、全部客户端/服务/测试目标通过。
- 相同aqtinstall3.3.0在Python3.14.4实际查询三平台Qt6.8.0架构均通过：[Linux](github_ci_evidence/aqt_linux.log)、[macOS](github_ci_evidence/aqt_macos.log)、[Windows](github_ci_evidence/aqt_windows.log)。官方索引同时确认QtTools/翻译均在默认架构包中。
- pip按Python3.14对三平台执行`--dry-run --ignore-installed --only-binary=:all:`，工具wheel解析通过：[Windows x64](github_ci_evidence/pip_windows.log)、[macOS arm64](github_ci_evidence/pip_macos.log)、[Linux x64](github_ci_evidence/pip_linux.log)；这只验证包可解析，不冒充三平台实际安装/产品构建。
- 首轮全套CTest被旧180秒执行器预算中断，runtime的90秒、publication的15秒各自超时，沙箱内single_instance失败；[原始输出](github_ci_evidence/tests.log)保留，不将不完整运行记为全套Pass。单实例在沙箱外通过，host_lifecycle单独通过。
- publication直接运行21.39秒退出0；runtime放宽预算后暴露取消测试订阅compact changed信号而未观察到内部生命周期状态的问题。测试改为在validationActive阶段观察semanticBytes，仍断言停止/取消清理；没有改动产品取消行为。[修复构建](github_ci_evidence/build_test_fix.log)通过，[定向最终JUnit](github_ci_evidence/targeted_final.xml)3/3通过：runtime129.12秒、publication21.00秒、single_instance0.62秒。单项预算调整为240/60秒，构建脚本整套CTest预算600秒。
- [Headless测试](github_ci_evidence/headless_tests.xml)5/5通过，实际执行同一build_windows脚本；QML lint退出0，有6项既有警告，不宣称零警告。
- [初轮仓库检查](github_ci_evidence/repository_checks_initial.log)通过设计生成物一致性、进度、源码布局、全自有C++格式和diff空白；后续持续交付设计修改另行生成/核对，最终结果随[发行报告](20261027_github_release.md)记录。
- GitHub远端及Linux/macOS产品构建NotRun，不能以配置/Windows验证宣称三平台通过。
