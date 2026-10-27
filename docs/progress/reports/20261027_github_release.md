# Windows持续交付与CI核验

2026-10-27 / Codex；状态Review。Windows发行构建/干净包及本地回归通过；远端Windows进程测试Fail与Qt6.8两项UI Fail已本机复现修复，修复版远端重跑与实际Release发布NotRun。未提交/推送，不将本地结果称GitHub通过。

## 交付行为

[日常工作流](../../../.github/workflows/build.yml)以Windows为发布门，main推送/PR自动运行，其他分支经PR或手动验证。普通运行22项快速测试（含9项UI），v标签/手动full_validation运行完整25项、headless及官方内核探针；纯文档只做仓库检查，依赖/策略/随包许可变化例外。Linux/macOS移至[独立手动平台验证](../../../.github/workflows/platform_validation.yml)，不阻塞Windows发布、不分发其用户包。本机Qt6.8快速构建/22项测试、headless构建/5项测试及工作流/脚本检查通过，见[DEVLOG](../DEVLOG.md#2026-10-27--windows优先快速迭代ci)；远端执行NotRun。

默认分支（含手动运行）更新continuous预发行，v标签生成对应版本，连字符版本为预发行。Windows验证runner复用工具/资产，测试完成后同目录切换无测试Release配置、增量构建及包启动检查并上传ZIP/摘要；本机Qt6.8快检22/22通过，配置切换仅重编3项、其余111项生产对象复用，无测试包/清PATH启动核对通过，见[DEVLOG](../DEVLOG.md#2026-10-27--main直接推送编译复用)。发布job只下载同次artifact并核验发布，仅它有contents写权限。continuous替换同名资产前核对分支HEAD；过期验证可取消，标签/发布不主动取消。资产缓存命中仍核验摘要；不使用跨机器CMake缓存。

release preset为Release/BUILD_TESTING=OFF，本地默认独立目录、CI顺序切换复用测试目录。[打包脚本](../../../scripts/package_release_windows.ps1)安装Qt/QML/app-local CRT、锁定官方Mihomo/Geo、许可/元数据/用户说明，拒绝测试构建及非运行内容，生成ZIP/SHA256；[发布脚本](../../../scripts/publish_release.ps1)校验后上传。诊断只写build，Actions日志保留14天，不影响Release资产。当前未签名，干净普通用户/TUN/应用内更新签名链未验收。

## CI故障与修复

- PyPI无ninja1.12.1包，改1.13.2，见[PyPI](https://pypi.org/pypi/ninja/json)。Qt6.8 Linux架构应linux_gcc_64，Windows为win64_msvc2022_64、macOS为clang_64，见[Qt Action](https://github.com/jurplel/install-qt-action/blob/v4/README.md)。
- Python3.14先设置，关闭Qt Action内部setup-python，锁aqt3.3.0/py7zr1.1.0。Windows测试前获取YAML/Mihomo/Geo，非Windows仅获取YAML并排除依赖Windows受管进程的runtime。Qt翻译目录改qmake查询，避免库/framework路径假设。
- QHash无first，provider_stream_test改按键借用设备并断言非空；失败后全目标重建通过。publication/runtime调整合理预算，取消断言改在validationActive观察semanticBytes，保留清理语义，产品取消行为未改。初轮超时/不完整CTest未计全套Pass。
- Windows native_process_test中文损坏，本机复现双管道各100000字符完整、CP936字节误按UTF-8解码，见[原字节诊断](evidence/native_encoding_diagnostic.json)。执行器/子夹具显式UTF-8并记录可选TextEncoding；参数/输出独立断言，保留退出7/3秒超时及子孙回收。
- [父代码页CP437/936/UTF-8](evidence/native_encoding_matrix.json)回归均Pass。远端旧测试无原字节，不能断言其具体代码页。未来日志统一build/script_tests并always上传。
- Qt6.8的ui_runtime_lists主题空引用、host_lifecycle重建恢复Fail已本机同版本复现；RuntimeList显式声明主题并组合FlatTile后两项恢复，最终9/9现有UI回归22.61秒全Pass、构建/lint退出0。修复前Qt6.12两项Pass不能证明Qt6.8兼容；字体目录/raise提示在成功测试中仍存在，不能据此归因。具体命令、日志位置和限制见[DEVLOG](../DEVLOG.md#2026-10-27--actions-qt68-ui回归调查)；该修复未删除测试或放宽错误门槛，远端待重跑。

aqt实际查询三平台Qt6.8架构、pip三平台wheel dry-run均通过，只证明信息可解析。仓库Actions未认证API返回404，未取得修复版远端构建/发布结果。

## 依赖固定来源

Geo原latest/master变化与冻结摘要不符，拒绝正确。2026-10-26核对上游活动后固定数据提交bfa68b978ec9c0494203d7c0d64e186640f7257b、许可提交964be7423e659538e4aa4b77ff908188b7102890；实测与原2026-10-24摘要匹配，未放宽校验/自动升级。来源/大小/SHA以[清单](../../baseline/release_matrix.json)为准。真实在线/离线获取与11项隔离回归通过，不代表Geo自动更新或产品转发验收。

## 本地验证

本机Windows x64/Qt6.9.0/MSVC14.51，CI仍Qt6.8.0/windows-2022，不能替代远端环境。

| 验证 | 真实结果与限制 |
|---|---|
| Windows全目标增量构建 | 退出0，客户端/服务/测试及翻译通过，QML lint退出0、6项既有警告 |
| 最终开发回归 | [22项JUnit](evidence/delivery_tests.xml)全Pass（65.93秒），另[定向3项](evidence/targeted_tests.xml)runtime129.12秒/publication21秒/单实例0.62秒通过；25项均有证据，未称一次整套通过 |
| 编码修复后headless | 同build_windows入口构建退出0，[5/5 JUnit](evidence/headless_tests.xml)6.54秒；GUI/内核沿用未受影响证据 |
| 无测试Release构建 | 首次未隔离回放变量失败，修正编译期边界后通过；包拒绝Qt附带vc_redist安装器，改app-local CRT重建/打包通过 |
| 包内容 | [191项核验](evidence/release_package.json)：190个清单文件及清单本身，仅quickclash/quickclash_service/mihomo三个exe，无QtTest/测试/符号/源码/日志/安装器；dev在写dist前拒绝 |
| 清开发PATH启动 | [结果](evidence/release_smoke.json)：page_ready=true/qml_errors=0/replay=false；软件渲染仅部署检查，无系统代理/服务安装 |
| 发布编排 | mock gh覆盖首次/重复continuous、过期commit、正式/RC、上传失败与坏摘要，无外部写入 |
| 工程检查 | 11项依赖回归、进程执行器、发布模拟、完整Repository checks、actionlint1.7.12及PowerShell/格式/文档检查通过，不等于远端产品通过 |

本地ZIP77,197,685B，SHA256=58fe68e4166a5499bfd6c4ff0e6cf9db3a9b765f3adee175e7405f6d6baa0ac2；Version=local-validation/Commit为空，不冒充已提交版本。下一步推送后核对Windows Actions/Release资产，Linux/macOS按需手动核对；专项仍Review。日常改动不追加本报告流水，不为重试新建report。
