# P2基础协议、自动代理组与兼容性诊断交付

日期/执行者：2026-10-24 / Codex。关联P2-21、P2-22、P2-23，三项Done；P2当前19Done/2Review/2Blocked，G2仍Blocked。设计基线1.7，工作目录D:/repos/quick-clash，HEAD为3b12f63510afdb1a95e863e7852fb08e06aafef4加未提交工作区变更；保留原有修改，未提交或推送。P0/P1和此前P2实现记录保留，本报告描述本轮增量。

## 1. 已实现的功能

| 任务 | 交付 | 实现位置与边界 |
|---|---|---|
| P2-21 | 内嵌Hysteria2、Trojan、VLESS、VMess基础配置，保留HTTP/SOCKS5/SS | config_schema.{h,cpp}按协议校验必填项、类型、限额和嵌套字段；支持常用TLS/SNI/ALPN/UDP、VLESS Reality/flow、Trojan/VLESS/VMess所需TCP/WS/gRPC，具体字段以config_policy.json为准 |
| P2-22 | select/url-test/fallback及嵌套组 | 拒绝名称冲突、缺失/重复成员、循环引用及超过16层引用；运行时映射Selector/URLTest/Fallback，只对Selector开放手选及恢复；自动组显示类型、当前成员和成员清单 |
| P2-23 | 多项具体兼容性原因及中英文提示 | Error.configIssues最多32项，分类协议/传输/组/规则/DNS/外部资源/字段；明确功能阶段；187条中文翻译完成，源文案为英文 |

配置树/重复键/大小检查保留在config_preflight，逐字段策略拆入config_schema。每类配置按固定字段表接受，不任意透传外部文件、下载地址、插件或未分类字段。规则仍为DOMAIN/DOMAIN-SUFFIX/DOMAIN-KEYWORD/IP-CIDR/IP-CIDR6/MATCH；Geo/provider和启用的内核DNS/fake-ip明确拒绝，分别按P3-02/08/09推进。

诊断只带固定code/feature/phase和结构索引path，未知键使用<field>，不回显用户键名、节点/组名、URL、凭据或YAML异常原文。可以汇总多类不兼容；解析/结构错误仍可提前终止。运行快照提供安全诊断，持久错误只记稳定总码；成功后清除旧诊断。有效预览遮蔽新协议全部嵌套值和组健康检查URL。节点身份沿用完整描述的本地HMAC，凭据变化不能复用旧身份。

自动组健康检查由Mihomo执行；客户端在Ready、操作完成或显式刷新时读取，未新增常驻测速轮询。命令入口及重启恢复都检查组能力和成员，不能从隐藏控件绕过能力限制。

实际窗口探针发现异步设置加载期间窗口尚未创建，main的--window-size分支直接调用QWindow::resize导致c0000005。修复为ApplicationHost暂存尺寸、创建窗口后应用；截图等待内容和运行态Ready，避免捕获空页面。异常发生于宿主初始化，非自动组渲染。修复后的430×850逻辑像素中文代理页显示手选组和两个自动组，底部保持三个入口，进程正常退出。

## 2. 构建与产物

环境：Windows x64、Qt 6.8.0 msvc2022_64、CMake 4.3.0、VS 18 Community；build/p2_responsive，Release、C++20、自有代码/WX、BUILD_TESTING开启。构建使用已有VS工具链环境，cmake --build build/p2_responsive；未改动官方Mihomo字节。

当前完整本地包：dist/p2_protocols_final，入口bin/quickclash.exe。需保留全部Qt/QML/TLS/CRT、core、licenses和metadata文件，不能只复制exe；这是未签名开发验证包，尚非干净普通用户或公开发行验收。

- 客户端SHA-256：bde55c99302143c3c3b33236b08609cea9ad123f1730a67e5d4292fd6ad7ee94。
- 官方Mihomo v1.19.33 SHA-256：1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a。
- [完整清单](p2_evidence/20261024/protocols/package_manifest.json)、[打包记录](p2_evidence/20261024/protocols/package_final.log)、[最终构建](p2_evidence/20261024/protocols/build_delivery.log)。
- [实际中文代理页](p2_evidence/20261024/protocols/proxies_final_zh.png)、[运行态](p2_evidence/20261024/protocols/ui_final_state.json)、[退出与摘要](p2_evidence/20261024/protocols/ui_final_result.json)。Ready=true、busy=false、error为空、退出0。

早期dist/p2_protocols以及此前p2_adaptive/p2_final保留历史，不是本轮最终包。

## 3. 实际验证

| 检查 | 实际结果 | 证据及范围 |
|---|---|---|
| Release增量构建 | Pass，退出0 | [build.log](p2_evidence/20261024/protocols/build.log)、[build_final.log](p2_evidence/20261024/protocols/build_final.log)及最终宿主修复构建 |
| 受影响CTest | Pass，7/7，22.99秒 | contracts、runtime、localization、ui_state、qml_smoke、ui_keyboard、ui_responsive；[日志](p2_evidence/20261024/protocols/ctest_final.log) / [XML](p2_evidence/20261024/protocols/ctest_final.xml) |
| 宿主窗口修复后UI检查 | Pass，3/3，3.67秒 | 仅重跑受影响的qml_smoke/ui_keyboard/ui_responsive；[日志](p2_evidence/20261024/protocols/ctest_window_fix.log) / [XML](p2_evidence/20261024/protocols/ctest_window_fix.xml) |
| 官方内核与生产应用链路 | Pass，受控范围 | [runtime_evidence.json](p2_evidence/20261024/protocols/runtime_evidence.json)：新协议隔离-t、owned Ready、选择读回/停止重启、坏候选保留、组类型/能力；原域名CONNECT/TLS/订阅/网站路径也回归通过，系统代理未改动 |
| 自动组健康行为 | Pass，受控范围 | 测试临时HTTP204健康服务收到核心请求，url-test/fallback从REJECT、DIRECT中选择DIRECT；手动修改自动组被拒绝，公开夹具另保留嵌套引用，不代表公网节点可达 |
| QML lint | 退出0，有既有工具警告 | [日志](p2_evidence/20261024/protocols/qmllint.log)：Qt 6.8 Dialogs重复类型/import警告保留，未称零警告 |
| 部署/最终UI | Pass，开发机 | 随包smoke通过，实际代理页已核对；不是无开发工具干净机，已有dxcompiler/dxil/VCINSTALLDIR部署提示保留，CRT显式随包 |
| 文档/进度/命名/格式/隐私 | 工程交付检查 | 命令、退出码和摘要见[delivery_checks.log](p2_evidence/20261024/protocols/delivery_checks.log)，不替代产品验收 |

测试输入为tests/fixtures/mvp/mainstream_valid.yaml和compatibility_unsupported.yaml及本地受控服务；域名为.invalid、凭据为公开假值。合约检查覆盖逐协议必填/非法字段、嵌套选项、组循环/引用、身份失效、预览脱敏、各类诊断及32项上限。本轮没有请求私有订阅，没有将其地址、查询/路径凭据、正文或节点名写入仓库。

保留失败：首次增加健康行为断言时6/7，夹具仅接受GET而核心发送HEAD，现补齐GET/HEAD后7/7；[首次失败](p2_evidence/20261024/protocols/ctest_health_initial_fail.log)与XML保留。窗口空指针的[调试栈](p2_evidence/20261024/protocols/ui_crash_stack.log)及早期空白proxies_zh.png保留，后者不作为界面通过证据。沙箱构建glob检查等待和ACL初始化失败后改在获准Windows环境执行，未关闭用户原有实例。

## 4. 资源证据与限制

按开发优先范围仅做一次10秒/91样本，未增加重复或长时矩阵。该次针对中间包dist/p2_protocols，客户端SHA为4c8945f73e9d2065e1afbf731d37bee618a6e642b221b94b11e2217297724e01，场景为真实Qt Quick首页、持久仓库worker、owned Ready核心，无持续转发/回放；[summary.json](p2_evidence/20261024/protocols/runtime_memory/summary.json)、[原始数据](p2_evidence/20261024/protocols/runtime_memory/memory.csv)、[清理](p2_evidence/20261024/protocols/runtime_memory/cleanup.json)。客户端退出0，owned核心已退出。

| 进程 | 专用工作集峰值/B | 总工作集峰值/B | 私有提交峰值/B |
|---|---:|---:|---:|
| 客户端进程组 | 58,830,848 | 150,122,496 | 201,064,448 |
| 独立核心 | 7,659,520 | 27,803,648 | 64,131,072 |

中间包该场景满足100,000,000B专用工作集目标；其后窗口/截图初始化修复改变了最终二进制，因此该资源结论对最终包标Stale，最终包资源验收NotRun。没有把中间包短测当作最终包或全部组合通过，也不把总工作集/私有提交作为硬门槛。未测导入峰值、持续转发、真实会话隐藏重建、CPU/帧/GPU或长期行为；不为本次文档交付重复性能测试。

## 5. 台账、未覆盖与接续

P2-21/22/23在上述范围内Done；P2-11和P2-16保留Review，P2-15及P2-17保留Blocked。G0/G1只保留历史工程/原型结论，G2仍Blocked。MVP-04/11按子条件更新，未宣称整个条目Pass；MVP-05/06保留原受控转发范围，未扩大为新增协议公网握手。

新增协议只证明官方校验、客户端会话接入及控制面行为，真实公网Hysteria2/Trojan/VLESS/VMess握手未测。含Geo/fake-ip依赖的原始私有订阅仍需P3，不静默删规则或关闭必要DNS来制造成功。

下一任务P2-11：一次代表性Windows实际换网/唤醒及请求合并核对；随后P2-15补客户端强杀到下次租约恢复、首次失败关键组合，P2-16补无Qt开发工具的普通用户环境，最后汇总P2-17/G2。保持必要验证范围，跨平台原生/CI后置P4，不增长时/图形矩阵。
