# P2响应式、多语言与标准订阅链接适配

日期/执行者：2026-10-24 / Codex。设计基线1.7，源码为HEAD 3b12f63510afdb1a95e863e7852fb08e06aafef4加未提交工作区修改，保留此前全部P0/P1/P2改动，无提交或推送。

结论：P2-18/19/20已完成；P2现为16Done、2Review、2Blocked、3Todo，G2仍Blocked。新增基础协议、自动组和具体诊断任务P2-21—23，Geo/DNS与高级能力的分期见[功能规划](20261024_mihomo_feature_plan.md)，尚未开发。四份私有样本能下载和识别，不代表可导入或节点可转发。

## 本次实现与可运行产物

- qml/main.qml和UiState：最小360×480逻辑像素，宽度<600隐藏侧栏、底部仅Dashboard/Proxies/Settings，≥600保留八模块侧栏/导航轨。设置提供其余五页入口，首页可管理订阅。同一PageHost及注册表，缩放不强制切路由、不重建QML引擎，轻量过滤状态保留；业务服务所有权独立。未专门验证Ready核心持续转发时resize，不能用引擎不重建代替该项证据。
- 首页/订阅/代理/设置：圆角贴片、窄窗换行/滚动、按钮Flow、端口表单双列回退。英文端口不再有千位分隔。首次检查暴露自定义SpinBox文本函数绑定循环，已改C locale并通过重跑；初次失败日志保留。
- Localization及resources/i18n：产品源码英文，Qt词表172条中文翻译已完成，应用/Qt中文资源内嵌。启动按系统uiLanguages优先序匹配en/zh，无匹配英文；中文变体当前统一简体，更改系统语言后重启。托盘/状态/注册模块标题使用翻译；协议值、错误码和用户自带名称保持。隔离开发探针可指定语言，非用户持久偏好。
- DesktopRuntime/config_preflight：标准订阅默认发送Clash/Mihomo UA及YAML Accept，用户头可覆盖；去除输入首尾空白，保持查询和签名；不猜格式参数、不用第三方转换。识别有界正文而非只看MIME，兼容错误的text/html标注；空/HTML/Base64/错误JSON有中英文原因，添加/更新/换来源共用。保持HTTPS校验、16MiB、15秒传输/20秒总截止、拒跳转、generation及原有配置策略/官方隔离-t，坏更新不改旧运行状态。Base64和分享链接转换未实现。

本地包：D:/repos/quick-clash/dist/p2_adaptive/bin/quickclash.exe，完整目录使用。客户端SHA-256为081e680d2b089183ced2a92611d0940d68f4a073ecd7f16bbc9b9917b59680e0。Qt6.8.0/MSVC19.50/CMake4.3/yaml-cpp0.9.0；官方Mihomo v1.19.33资产不变，核心SHA-256为1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a。包含Qt/QML/TLS/CRT/核心及许可清单；BUILD_TESTING开启，未签名，是开发机验证包，未宣称干净普通用户环境通过。[文件清单](p2_evidence/20261024/responsive/package_manifest.json)记录全包摘要。

## 真实检查与证据

| 检查 | 结果及范围 | 证据 |
|---|---|---|
| Windows Release增量构建 | /WX通过；独立build/p2_responsive保留用户正在运行的旧exe。第一次沙箱构建停在目录检查，停止本次CMake/Ninja后沙箱外重试成功 | [构建](p2_evidence/20261024/responsive/build_initial.log)、[最终构建](p2_evidence/20261024/responsive/build_final.log) |
| 受影响CTest | 最终7/7，19.54秒：contracts/runtime/localization/ui_state/qml_smoke/ui_keyboard/ui_responsive。官方内核和本地TLS/CONNECT、UA敏感订阅/头覆盖、错误更新保留Ready、语言回退、360/599/600/1160导航与八路由/键盘/状态。非全12项重跑；默认不写OS代理 | [结果](p2_evidence/20261024/responsive/ctest_final.log)、[JUnit](p2_evidence/20261024/responsive/ctest_final.xml)、[初次6/7失败](p2_evidence/20261024/responsive/ctest_initial.log) |
| QML lint | 退出0，未新增业务绑定问题；Qt6.8 Dialogs重复导入/类型警告仍存在，不称无警告 | [输出](p2_evidence/20261024/responsive/qmllint.log) |
| 本地完整包 | 部署smoke通过；工具提示缺dxcompiler/dxil、VCINSTALLDIR，CRT由脚本显式补齐。非干净机验收 | [打包](p2_evidence/20261024/responsive/package.log) |
| 最终视觉 | Windows D3D11、实际DPR1.25，360×780逻辑窗口：中文浅色首页与英文深色设置，底部三个入口、端口7890/9090可读；截图450×975含Qt捕获的边缘背景，不把物理尺寸当逻辑尺寸 | [中文首页](p2_evidence/20261024/responsive/final_home_zh.png)、[英文设置](p2_evidence/20261024/responsive/final_settings_en.png) |
| 私有订阅 | 本地授权4样本，均HTTP200、YAML；3个MIME误标text/html。S1生产路径隔离导入返回UnsupportedConfigFeature、0 profiles；其余仅内存结构检查，未假称导入成功。统计只保留标准键和类型计数 | [S1生产探针](p2_evidence/20261024/responsive/subscription_probe.json)、[匿名结构](p2_evidence/20261024/responsive/subscription_samples.json) |

单轮10秒/91样本：真实首页、仓库worker、官方owned Ready核心，不开系统代理，无回放或持续流量。客户端组无辅助进程，专用工作集峰60,329,984 B（60.33 MB），总工作集152,944,640 B，私有提交204,558,336 B。核心分别7,589,888 / 27,901,952 / 63,078,400 B，单列不计客户端预算。本场景≤100,000,000 B；未测导入峰值、真实私有节点流量、隐藏/重建组合、CPU/帧/GPU/长时。进程退出0且owned核心退出，摘要与最终包一致。[三项摘要](p2_evidence/20261024/responsive/runtime_memory/summary.json)和同目录CSV/状态保留原始记录。

## 证据边界及下一步

旧p2_final及中间p2_responsive包对当前UI/语言/订阅下载/资源结论为Stale，旧原始报告保留。P0关键核心契约及此前WinINet专项证据按未改范围保留；本轮受控HTTPS运行回归通过，MVP-05/06的历史通过范围不扩展为新私有协议支持。G0/G1仅历史工程/原型Pass；G2的OS事件、强杀/关键失败组合、干净普通用户环境仍未关闭。MVP-02虽新增HTML/Base64/空路径证据，完整限额/TLS反例未全跑，保持NotRun。

下一开发任务P2-21，先Hysteria2/Trojan/VLESS字段与常用TLS/Reality/传输；然后P2-22自动组、P2-23具体诊断。P3-02受管Geo、P3-08 fake-ip DNS、P3-09高级能力分别跟踪。所有私有URL/token/路径凭据/原文/节点均仅本地临时使用，不写仓库或夹具；新增任务只记录匿名统计。

交付检查：设计分册合并生成与-Check、check_progress、check_source_layout、C++格式及git diff --check按本轮执行；原始输出见[交付核对](p2_evidence/20261024/responsive/delivery_checks.log)。这些脚本只检查设计/台账/布局，不代替产品验收。
