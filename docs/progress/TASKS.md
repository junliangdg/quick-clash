# 开发任务台账

本文件只维护当前任务、负责人、依赖、证据和下一步，不记录逐次开发或修改流水。过程记录见[开发日志](DEVLOG.md)，详细验证见[报告索引](reports/README.md)，验收状态见[清单](CHECKLISTS.md)。

## 专项状态

优化优先级不复用仓库阶段任务ID，也不计入下表阶段任务数。

| 专项 | 实现状态 | 负责人 | 产品验证 | 证据/下一步 |
|---|---|---|---|---|
| 订阅更新后内存异常调查 | Review | Codex | NotRun | [调查报告](reports/20261026_subscription_memory_investigation.md)；可选--software-rendering构建/3项UI测试通过，旧页面已销毁；真实GPU专用175.68MB，Qt软件129.34—131.44MB；当前不设固定MB门槛，字体驻留504→491MB仍存在；继续定位Qt字体触发/私有分配，异常修复未关闭 |
| 内存优化P0/P1-A/P1-B/P2/P3 | Review | Zoo | NotRun | [跨批次审查与验证矩阵](reports/20261026_client_memory_optimization.md#验证范围)；另获构建授权后补新源码定向验证 |
| 系统代理冲突显式开启重建基线 | Done | Zoo | Review | [修复报告](reports/windows_validation.md#系统代理冲突修复)；增量构建/proxy_lease/recovery 退出0；全量 CTest 受既有 provider_stream_test 编译错误影响，GUI/原生转发验收 NotRun |
| GitHub三平台CI配置修复 | Review | Codex | Fail | [核验与修复报告](reports/20261027_github_release.md#ci故障与修复)、[Qt6.8回归](DEVLOG.md#2026-10-27--actions-qt68-ui回归调查)；远端两项UI Fail已在本机Qt6.8复现，RuntimeList绑定修复后构建/QML lint及9/9 UI回归Pass；远端仍待重跑，不删除测试 |
| Windows GitHub持续交付发行包 | Review | Codex | NotRun | [持续交付报告](reports/20261027_github_release.md)、[main编译复用](DEVLOG.md#2026-10-27--main直接推送编译复用)；保留main快检，同目录切换无测试发行配置；本机Qt6.8快速22/22、切换仅重编3个源文件及干净包/清PATH启动通过，远端Actions/发行待核对 |

## 阶段任务

ID 固定不复用；新增子任务用 P2-18 等下一序号，拆分前保留旧 ID 的范围/证据记录。依赖仅列必须完成的前置子项，阶段门另由 CHECKLISTS 管理。开始任务时填写执行者，未开始项保留未分配。任务证据可链接具体 DEVLOG 标题、GitHub Actions 运行或专项报告；子任务 Done 不表示阶段门通过。

| ID | 阶段 | 工作包 | 可验收交付 | 状态 | 负责人 | 依赖 | 证据 | 下一步/阻塞原因 |
|---|---|---|---|---|---|---|---|---|
| P0-01 | P0 | W00 | 工具链/Qt/YAML/OS 候选与许可基线 | Done | Codex | — | [P0 报告](reports/windows_validation.md#工程与内核) | 版本/许可基线完成；实际链接与启动由 P0-06 验证 |
| P0-02 | P0 | W00 | 官方 Mihomo 与静态资源锁定/离线校验 | Done | Codex | P0-01 | [P0 报告](reports/windows_validation.md#工程与内核) | Windows v1.19.33 官方资产与摘要离线复核完成；其他平台仅候选，Geo 不支持 |
| P0-03 | P0 | W00, W03 | P2 配置策略与脱敏成功/失败夹具 | Done | Codex | P0-02 | [P0 报告](reports/windows_validation.md#工程与内核) | 策略与正反样本完成；C++ 预检测试由 P0-08 验证，真实转发由 P2 验证 |
| P0-04 | P0 | W00, W08 | 锁定内核 API/启动/校验/Job 技术验证 | Done | Codex | P0-02, P0-03 | [当前验证](reports/windows_validation.md#工程与内核) | 官方字节、认证/选择/模式/重载、owned 监听、Job、零网络能力 AppContainer 正反校验通过；生产适配在 P2 |
| P0-05 | P0 | W01 | 统一视觉基准与八模块/状态边界 | Done | Codex | — | [统一视觉规范](reports/20261027_unified_visual_implementation.md#设计基准) | 当前基准由P3-15交付，产品组件/页面迁移由P3-16验证 |
| P0-06 | P0 | W02 | Windows CMake/Qt Quick 模块空壳 | Done | Codex | P0-01 | [当前验证](reports/windows_validation.md#工程与内核)、[AutoUic 修复](reports/windows_validation.md#工程与内核)、[QML 缓存警告修复](reports/windows_validation.md#工程与内核) | 保留 Zoo 的生成边界修复；Windows Release/cache/lint/6 项测试、启动通过 |
| P0-07 | P0 | W02 | Windows 本地构建/启动/无 UI 测试与基础 CI 配置 | Done | Codex | P0-06 | [当前验证](reports/windows_validation.md#工程与内核) | 2026-10-24 按用户指示收窄首版门槛；Windows 本地验证通过，Linux/macOS 原生与 CI 实际执行转 P4-06，未宣称跨平台兼容 |
| P0-08 | P0 | W03 | 领域类型/Operation/接口与无 UI 夹具测试 | Done | Codex | P0-03, P0-04, P0-06 | [当前验证](reports/windows_validation.md#工程与内核) | UI contracts 与独立 headless 1/1 通过；无 Quick/Widgets/Network 依赖 |
| P0-09 | P0 | W02, W18 | 构建/部署/内核获取与 P2 离线包方案 | Done | Codex | P0-02, P0-06 | [当前验证](reports/windows_validation.md#工程与内核) | 构建/部署脚本执行通过；脱离开发 Qt PATH 启动通过，普通用户完整产品包仍在 P2-16 |
| P1-01 | P1 | W04 | 单实例/托盘/窗口宿主 | Done | Codex | P0-06, P0-08 | [当前验证](reports/windows_validation.md#工程与内核) | IPC、无托盘关闭退路、真实 Windows 托盘关闭确认/隐藏/恢复探针通过 |
| P1-02 | P1 | W05 | 模块注册/导航投影/路由与嵌入边界 | Done | Codex | P0-05, P0-08, P1-01 | [当前验证](reports/windows_validation.md#工程与内核) | 八页面/双主题自动加载、路由/嵌入/导航投影通过；移动只验接口边界 |
| P1-03 | P1 | W05 | 双主题与代表页面回放基础 | Done | Codex | P1-02 | [原型功能证据](reports/windows_validation.md#原型生命周期与资源)、[当前视觉基准](reports/20261027_unified_visual_implementation.md#设计基准) | 原型功能与资源证据按版本保留；视觉结论Stale，方案C产品迁移/双主题与实际字体验收归P3-16 |
| P1-04 | P1 | W04, W05 | 显示/隐藏/卸载重建与需求释放 | Done | Codex | P1-01, P1-02 | [当前验证](reports/windows_validation.md#原型生命周期与资源) | 当前部署版隐藏/最小化需求释放、5 秒卸载、恢复通过；D3D11 basic 下 100 次句柄 779→779、QObject 133 |
| P1-05 | P1 | W06 | 性能采集脚本与可重复场景 | Done | Codex | P1-03, P1-04 | [当前验证](reports/windows_validation.md#工程与内核) | 双主题六场景均运行/留证，超限正确退出 1；采集交付不代表资源准入通过 |
| P1-06 | P1 | W06 | G1 原型短测报告与未来组件工程预留 | Done | Codex | P1-05 | [范围重评](reports/windows_validation.md#原型生命周期与资源) | 按用户要求和1.6范围，已有12/12短测49.7—54.9 MB及工程预留满足G1；长时/帧/GPU专项后置，转P2-01开发 |
| P2-01 | P2 | W07 | 原子仓库/schema/日志与损坏恢复 | Done | Codex | P0-08, P1-06 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 单写者/ACL/原子存储及manifest前后恢复通过；外部组合故障归P2-15 |
| P2-02 | P2 | W15 | 默认设置/DPAPI/偏好持久化 | Done | Codex | P2-01, P1-03 | [P2交付](reports/windows_validation.md#订阅与代理功能) | worker异步保存/迁移、DPAPI成功及损坏拒绝通过，默认不自动启动 |
| P2-03 | P2 | W08 | 内核适配/进程所有权/Ready/停止/Job | Done | Codex | P2-01, P0-04 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 官方摘要/版本、认证Ready、owned回环PID、挂起归Job及有界输出；当前直接终止owned Job |
| P2-04 | P2 | W09 | 配置子集/会话物化/隔离校验/停机选择 | Done | Codex | P2-02, P2-03, P0-03 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 原文/语义/会话分离、空历史/停机选择、隔离官方-t通过 |
| P2-05 | P2 | W09 | 首次启动/运行应用/补偿与 manifest 权威 | Done | Codex | P2-04 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 统一受控重启，节点读回后提交；运行端口失败恢复旧版本；热重载未启用 |
| P2-06 | P2 | W10 | 首页/组选择恢复/模式与有效配置 | Done | Codex | P2-05, P1-03 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 真实状态/组选择/模式、HMAC身份重开恢复、脱敏预览通过，实时流量后续 |
| P2-07 | P2 | W11 | HTTPS/本地导入与订阅最小管理 | Done | Codex | P2-02, P2-05 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 有界下载、坏候选保留、两配置/名称/来源头/更新/最后项删除通过 |
| P2-08 | P2 | W13 | Windows 系统代理适配/租约/迁移 | Done | Codex | P2-01, P2-03, P2-05 | [P2交付](reports/windows_validation.md#订阅与代理功能) | WinINet LAN快照读回/幂等/迁移/冲突/显式重开/磁盘失败恢复；不覆盖WinHTTP/RAS |
| P2-09 | P2 | W08, W13 | 组合启停/崩溃有限恢复/退出清理 | Done | Codex | P2-06, P2-08 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 首页/托盘统一门面，代理意图独立持久化及补偿、退出保留/UserStop清除；强杀组合归P2-15 |
| P2-10 | P2 | W12, W15 | 有界错误日志/最小修复入口 | Done | Codex | P2-05, P2-09 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 无内核错误/端口/导入选择/停止恢复、打开数据目录及手工修复说明可用 |
| P2-11 | P2 | W17 | Windows通知接入与有界合并核对 | Review | Codex | P2-07, P2-09 | [G2恢复开发](reports/windows_validation.md#恢复与本地包)、[范围复核](reports/windows_validation.md#阶段范围与待补项) | 接口/IP/连通性及唤醒已接，20→1/PID不变/旧测试失效/退出忽略已有证据；待按1.9逐条件核对，真实OS事件转P3-14，不自动Done |
| P2-12 | P2 | W20 | 可配置单目标 HTTPS 测试 | Done | Codex | P2-06, P2-09 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 自设目标DPAPI保存、当前入口HEAD/12秒/并发1、拒redirect/无直连回退 |
| P2-13 | P2 | W20 | 受控远端代理/HTTPS 端到端夹具 | Done | Codex | P2-05, P2-06, P2-07 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 独立GET经owned入口/域名CONNECT到受控TLS服务，远端记录断言通过；本机夹具范围 |
| P2-14 | P2 | W13, W20 | 系统代理端到端/重复租约/端口迁移 | Done | Codex | P2-08, P2-09, P2-13 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 真实PRECONFIG HTTPS/重复开启/迁移及原快照恢复通过，PAC/冲突另有注入适配器证据 |
| P2-15 | P2 | W07, W08, W09, W11, W13 | 首次失败/事务中断/核心崩溃/强杀恢复 | Done | Codex | P2-07, P2-09, P2-10, P2-14 | [G2恢复开发](reports/windows_validation.md#恢复与本地包) | 首次关键失败/停止打断/worker强杀→Job及真实WinINet下次租约/事务恢复、manifest前后、连续四次受管恢复及系统客户端回归通过；非全故障矩阵，真实OS事件独立归P2-11 |
| P2-16 | P2 | W18 | Windows本机完整离线包与依赖/启动核对 | Review | Codex | P0-09, P2-03, P2-04, P2-07, P2-10, P2-12, P2-14 | [G2恢复开发](reports/windows_validation.md#恢复与本地包)、[范围复核](reports/windows_validation.md#阶段范围与待补项) | dist/g2_recovery的186文件摘要/CRT/官方内核与清Qt PATH smoke通过；核对本地导入/Ready及随包修复说明证据，干净普通用户机器转P3-14，不自动Done |
| P2-17 | P2 | W06, W19, W20 | G2本机16项MVP证据与单场景资源汇总 | Todo | Codex | P2-11, P2-12, P2-13, P2-15, P2-16, P2-21, P2-22, P2-23, P1-05 | [范围复核](reports/windows_validation.md#阶段范围与待补项) | 先映射1.9条件与有效证据；只补GUI主链路及缺失的下载限额/TLS、身份/迟到/秘密保护定向检查，复用Ready首页短测；缺干净环境不再是本任务阻塞 |
| P2-18 | P2 | W05 | 窄窗竖屏布局与导航投影 | Done | Codex | P2-06 | [响应式行为](reports/windows_validation.md#订阅与代理功能)、[缩放修复](reports/windows_validation.md#订阅与代理功能) | 360×480、600断点和路由/状态保留已有技术证据；当前四入口与方案C视觉由P3-16统一验收 |
| P2-19 | P2 | W05, W15 | 英文源文案/中文翻译/自动系统语言 | Done | Codex | P2-18 | [本轮交付](reports/windows_validation.md#订阅与代理功能) | Qt词表内嵌，系统首选序匹配/英文回退、172条翻译和中英文截图通过 |
| P2-20 | P2 | W11 | 标准HTTPS订阅链接协商/内容识别/错误说明 | Done | Codex | P2-07 | [本轮交付](reports/windows_validation.md#订阅与代理功能) | 默认UA/Accept、头覆盖/空白、误标MIME与坏更新保留通过；4个私有样本仅识别，配置能力待扩展 |
| P2-21 | P2 | W09, W10 | Hysteria2/Trojan/VLESS/VMess基础协议与常用TLS/Reality传输 | Done | Codex | P2-20 | [协议交付](reports/windows_validation.md#订阅与代理功能) | 逐协议/嵌套字段、官方隔离-t/Ready/选择重启、身份与脱敏通过；未测新增协议公网握手 |
| P2-22 | P2 | W09, W10 | url-test/fallback组与能力化展示 | Done | Codex | P2-21 | [协议交付](reports/windows_validation.md#订阅与代理功能) | Selector/URLTest/Fallback能力映射、健康检查跳过REJECT、嵌套/引用保护与界面通过 |
| P2-23 | P2 | W09, W12 | 协议/组/规则/DNS具体不兼容原因与中英文提示 | Done | Codex | P2-22, P2-19 | [协议交付](reports/windows_validation.md#订阅与代理功能) | 最多32项安全结构诊断、七类不兼容与字段错误、中英文词表187条完成 |
| P2-24 | P2 | W05 | 缩放跨导航断点时消除D3D11秒级等待并稳定壳布局 | Done | Codex | P2-18 | [缩放修复](reports/windows_validation.md#订阅与代理功能) | 默认basic/D3D11关闭帧延迟等待对象；4/4检查与真实窗口160步通过，页面/需求保留，最终包Ready首页专用工作集60.75MB；完整帧/转发组合未测 |
| P3-01 | P3 | W10 | 代理组独立展开、节点选择与延迟测试 | Doing | Zoo | P2-06, P2-21, P2-22, P3-10 | [当前设计与验证边界](reports/20261027_unified_visual_implementation.md#设计基准) | 现有功能需按设计08/17完成验收；方案C共享控件/页面迁移由P3-16承接，不能将概念预览当产品Pass |
| P3-02 | P3 | W11 | 计划订阅/用量、受管provider及GEOIP/GEOSITE/RULE-SET资源 | Todo | 未分配 | P2-07, P2-21, P2-23, P3-10, P3-11 | [固定Geo切片](reports/windows_validation.md#订阅与代理功能)、[更新内存切片](reports/20261026_subscription_memory_investigation.md#已修复的缓冲问题) | Codex已修更新缓冲/重复解析与未变内容复用，最终包有变GUI峰76.45MB；完整订阅用量、动态资源/回退和provider/RULE-SET仍待交付，状态不提前Done |
| P3-03 | P3 | W12 | 连接/日志/规则/提供者完整页面 | Todo | 未分配 | P2-03, P2-06, P2-10 | — | 核对当前工作区按需数据流、虚拟化、关闭及脱敏导出，保存Windows功能证据 |
| P3-04 | P3 | W14 | Windows 权限服务/隔离控制器/TUN | Todo | 未分配 | P2-03, P2-05, P2-08, P2-09 | — | 核对现有服务实现；完成固定IPC/权限与TUN真实转发、迁移和退出清理，代码存在不等于验收通过 |
| P3-05 | P3 | W15 | 完整设置/备份/诊断 | Todo | 未分配 | P2-01, P2-02, P2-10, P2-12 | — | 核对当前工作区安全恢复、支持包与schema；网络设置面向系统代理/TUN开关，平台差异经能力提供 |
| P3-06 | P3 | W20 | 网站/系统资源完整能力与可选 IP 边界 | Todo | 未分配 | P2-06, P2-12, P2-13 | [内存展示切片](reports/20261026_subscription_memory_investigation.md#已修复的缓冲问题) | Codex已分离客户端/内核三指标并通过受影响UI检查；完整NET/RES、资源采样分层和可选IP边界仍待交付 |
| P3-07 | P3 | W06, W19 | G3 Windows日常使用交付与必要验收汇总 | Todo | 未分配 | P2-17, P3-01, P3-02, P3-03, P3-04, P3-05, P3-06, P3-08, P3-09, P3-12, P3-13, P3-14, P3-16 | — | 汇总八页面、真实转发/恢复、P3-14有限环境/宿主证据及静态兼容；资源复用有效证据/必要单场景短测，其他平台原生后置 |
| P3-08 | P3 | W09, W15 | fake-ip/redir-host DNS、解析/过滤/缓存与回环防护 | Todo | 未分配 | P2-23, P3-02, P3-10 | [内部DNS切片](reports/windows_validation.md#订阅与代理功能)、[夹具修复](reports/windows_validation.md#恢复与本地包) | 核对高级policy/设置/缓存/回环与规则匹配，原contracts夹具失败已修复，不再列阻塞；外部监听/系统DNS劫持另依赖P3-04 |
| P3-09 | P3 | W09, W10 | TUIC等高级协议/传输与load-balance策略扩展 | Todo | 未分配 | P2-21, P2-22, P2-23 | [功能分期](reports/windows_validation.md#阶段范围与待补项) | 核对当前工作区分项实现和支持矩阵，不任意透传外部文件/下载字段，不扩大为全部协议真实握手通过 |
| P3-10 | P3 | W09, W10, W11, W20 | 代表订阅的受管Geo/最小DNS兼容及真实节点HTTPS链路 | Done | Codex | P2-07, P2-21, P2-22, P2-23 | [订阅闭环](reports/windows_validation.md#订阅与代理功能) | S3导入56节点/3组，VLESS全局HTTPS200/1MiB/TLS及所选节点链通过；Geo缺失/损坏拒绝、Windows包及单轮短测通过；完整P3-02/08继续开发 |
| P3-11 | P3 | W10, W11, W18, W20 | Windows日常订阅调度/自动应用/节点筛选与可用包 | Done | Codex | P3-10, P2-08, P2-09 | [日常链路](reports/windows_validation.md#订阅与代理功能) | 定时更新/退避/自动应用/补偿与筛选完成；WinINet转发/精确恢复及私有56节点VLESS HTTPS/更新后转发通过；dist/p3_daily，完整G3仍待后续 |
| P3-12 | P3 | W02, W03, W19 | G3 Windows优先方案、三/五平台路线与验收边界 | Done | Codex | — | [方案调整](reports/windows_validation.md#阶段范围与待补项) | 设计1.8与任务/清单同步；仅文档方案交付，未改变G2/G3或产品兼容结论 |
| P3-13 | P3 | W02, W03, W16 | 跨平台静态兼容核对与Windows耦合收敛 | Todo | 未分配 | P3-12 | [初步缺口](reports/windows_validation.md#阶段范围与待补项) | 先抽离DesktopRuntime具体凭据/代理头、desktop_features的Win32/SCM采样及EngineSession的Windows进程类型；再核对CMake/能力/路径/导航/翻译/生命周期，原生运行转P4 |
| P3-14 | P3 | W04, W13, W17, W18 | Windows普通用户包与有限OS/GUI宿主集成补证 | Todo | 未分配 | P1-04, P2-11, P2-15, P2-16 | [范围复核](reports/windows_validation.md#阶段范围与待补项) | G3前补干净普通用户包/代表路径/基本修复、一次真实换网或唤醒、GUI整体强杀/下次恢复、运行会话隐藏重建及一次外部代理变更；只补差异，暂无干净环境不阻塞其他开发 |
| P3-15 | P3 | W01, W05 | 方案C统一视觉规范与双主题离线概念预览 | Done | Codex | — | [设计与预览验证](reports/20261027_unified_visual_implementation.md#设计基准) | 规范/预览/旧参考清理及浏览器、对比度、生成/一致性、进度/命名/diff检查通过；产品迁移见P3-16 |
| P3-16 | P3 | W05 | 方案C共享组件、八页面与双主题产品迁移 | Done | Codex | P3-15 | [实施与验证](reports/20261027_unified_visual_implementation.md) | Windows构建、10项测试及末次布局3项回归通过；本机125%DPI原生GPU的26张界面/弹层/错误态截图核对，含360/800/1000/1200宽；60点流量、预览符号/文字及右上运行状态完成；固定MB门槛已移除，完整功能/高DPI矩阵按原任务继续 |
| P4-01 | P4 | W16, W18 | Linux代理/托盘/服务与平台发行集成 | Todo | 未分配 | P3-07 | — | P4-06中Linux原生基线就绪后逐项实现，记录GNOME/KDE/X11/Wayland及安装/更新范围；不等待macOS完成 |
| P4-02 | P4 | W16, W18 | macOS菜单栏/代理/helper与平台发行集成 | Todo | 未分配 | P3-07 | — | P4-06中macOS原生基线就绪后逐项实现，记录多网络服务/签名/公证/授权与安装更新；不等待Linux完成 |
| P4-03 | P4 | W17 | 三平台网络/电源矩阵 | Todo | 未分配 | P4-01, P4-02 | — | 运行休眠、换网、时钟/会话变化用例 |
| P4-04 | P4 | W18 | Windows正式安装器/签名/应用与内核更新恢复 | Todo | 未分配 | P3-07 | — | Windows先行完成来源、防重放、schema与独立回退；不依赖Linux/macOS，其他平台发行集成归P4-01/02 |
| P4-05 | P4 | W06, W19 | G4 三平台 Beta/功能与资源矩阵 | Todo | 未分配 | P4-03, P4-04, P4-06 | — | 正式支持范围与各平台真实负载证据齐全 |
| P4-06 | P4 | W02, W16 | Linux/macOS 原生空壳与多平台 CI 实际运行 | Todo | 未分配 | P0-07 | [当前验证](reports/windows_validation.md#工程与内核) | 从 P0-07 后置；按冻结工具链原生构建/启动、无 UI/QML 测试与 CI run 留证，不阻塞 Windows 首版 |
| P5-01 | P5 | W19 | 代表性持续运行/关键故障与升级回归 | Todo | 未分配 | P4-05 | — | 按发布风险选取持续运行与资源抽查；24/72小时、全组合、帧/GPU和压力专项不统一强制，出现问题再扩展 |
| P5-02 | P5 | W19 | 候选发布/许可源码/用户说明与 G5 | Todo | 未分配 | P5-01 | — | 核验签名/来源、恢复说明与最终报告 |
