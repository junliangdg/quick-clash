# 开发任务台账

状态唯一来源为本表；状态与证据规则见 [使用说明](README.md)。设计范围见 [18 实施任务](../design/18-实施任务与验收清单.md)。

核对日期：2026-10-24，设计基线1.7。P0为9项Done、P1为6项Done，G0/G1保留历史工程/原型Pass；P2为20项Done、2项Review、2项Blocked，G2 Blocked。P2-24已交付[缩放卡顿修复](reports/20261024_resize_layout.md)；P2-18/19/20见[响应式/语言/链接适配](reports/20261024_p2_responsive_localization.md)，P2-21—23见[协议/自动组/诊断交付](reports/20261024_p2_protocols.md)；Geo/DNS扩展仍按P3分期。P3—P5共17项Todo。任务数不代表产品百分比；保留必要检查和单轮短测。

ID 固定不复用；新增子任务用 P2-18 等下一序号，拆分前保留旧 ID 的范围/证据记录，避免改号导致报告失效。依赖仅列必须完成的前置子项；阶段门另由 CHECKLISTS 管理。开始任务时填写执行者，未开始项保留未分配。证据使用相对本文件的本地 Markdown 报告链接；子任务 Done 不表示阶段门通过。

| ID | 阶段 | 工作包 | 可验收交付 | 状态 | 负责人 | 依赖 | 证据 | 下一步/阻塞原因 |
|---|---|---|---|---|---|---|---|---|
| P0-01 | P0 | W00 | 工具链/Qt/YAML/OS 候选与许可基线 | Done | Codex | — | [P0 报告](reports/p0_implementation.md) | 版本/许可基线完成；实际链接与启动由 P0-06 验证 |
| P0-02 | P0 | W00 | 官方 Mihomo 与静态资源锁定/离线校验 | Done | Codex | P0-01 | [P0 报告](reports/p0_implementation.md) | Windows v1.19.33 官方资产与摘要离线复核完成；其他平台仅候选，Geo 不支持 |
| P0-03 | P0 | W00, W03 | P2 配置策略与脱敏成功/失败夹具 | Done | Codex | P0-02 | [P0 报告](reports/p0_implementation.md) | 策略与正反样本完成；C++ 预检测试由 P0-08 验证，真实转发由 P2 验证 |
| P0-04 | P0 | W00, W08 | 锁定内核 API/启动/校验/Job 技术验证 | Done | Codex | P0-02, P0-03 | [当前验证](reports/20261024_p0_p1_validation.md) | 官方字节、认证/选择/模式/重载、owned 监听、Job、零网络能力 AppContainer 正反校验通过；生产适配在 P2 |
| P0-05 | P0 | W01 | UI 参考台账、八模块/状态映射复核 | Done | Codex | — | [P0 报告](reports/p0_implementation.md) | 截图摘要、八模块/状态/嵌入边界完成；页面/主题与视觉实现在 P1 |
| P0-06 | P0 | W02 | Windows CMake/Qt Quick 模块空壳 | Done | Codex | P0-01 | [当前验证](reports/20261024_p0_p1_validation.md)、[AutoUic 修复](reports/20261024_autouic_fix.md)、[QML 缓存警告修复](reports/20261024_qmlcache_warning_fix.md) | 保留 Zoo 的生成边界修复；Windows Release/cache/lint/6 项测试、启动通过 |
| P0-07 | P0 | W02 | Windows 本地构建/启动/无 UI 测试与基础 CI 配置 | Done | Codex | P0-06 | [当前验证](reports/20261024_p0_p1_validation.md) | 2026-10-24 按用户指示收窄首版门槛；Windows 本地验证通过，Linux/macOS 原生与 CI 实际执行转 P4-06，未宣称跨平台兼容 |
| P0-08 | P0 | W03 | 领域类型/Operation/接口与无 UI 夹具测试 | Done | Codex | P0-03, P0-04, P0-06 | [当前验证](reports/20261024_p0_p1_validation.md) | UI contracts 与独立 headless 1/1 通过；无 Quick/Widgets/Network 依赖 |
| P0-09 | P0 | W02, W18 | 构建/部署/内核获取与 P2 离线包方案 | Done | Codex | P0-02, P0-06 | [当前验证](reports/20261024_p0_p1_validation.md) | 构建/部署脚本执行通过；脱离开发 Qt PATH 启动通过，普通用户完整产品包仍在 P2-16 |
| P1-01 | P1 | W04 | 单实例/托盘/窗口宿主 | Done | Codex | P0-06, P0-08 | [当前验证](reports/20261024_p0_p1_validation.md) | IPC、无托盘关闭退路、真实 Windows 托盘关闭确认/隐藏/恢复探针通过 |
| P1-02 | P1 | W05 | 模块注册/导航投影/路由与嵌入边界 | Done | Codex | P0-05, P0-08, P1-01 | [当前验证](reports/20261024_p0_p1_validation.md) | 八页面/双主题自动加载、路由/嵌入/导航投影通过；移动只验接口边界 |
| P1-03 | P1 | W05 | 双主题/圆角贴片/首页与大列表回放 | Done | Codex | P1-02 | [当前验证](reports/20261024_windows_private_working_set.md) | 代表主题/720×560/实际 DPR 1.25—2.5 截图和键盘导航/筛选/关闭弹层通过；真实业务、多屏移动/OS 字体缩放按 P2—P5 后续验收 |
| P1-04 | P1 | W04, W05 | 显示/隐藏/卸载重建与需求释放 | Done | Codex | P1-01, P1-02 | [当前验证](reports/20261024_windows_private_working_set.md) | 当前部署版隐藏/最小化需求释放、5 秒卸载、恢复通过；D3D11 basic 下 100 次句柄 779→779、QObject 133 |
| P1-05 | P1 | W06 | 性能采集脚本与可重复场景 | Done | Codex | P1-03, P1-04 | [当前验证](reports/20261024_p0_p1_validation.md) | 双主题六场景均运行/留证，超限正确退出 1；采集交付不代表资源准入通过 |
| P1-06 | P1 | W06 | G1 原型短测报告与未来组件工程预留 | Done | Codex | P1-05 | [范围重评](reports/20261024_development_first.md) | 按用户要求和1.6范围，已有12/12短测49.7—54.9 MB及工程预留满足G1；长时/帧/GPU专项后置，转P2-01开发 |
| P2-01 | P2 | W07 | 原子仓库/schema/日志与损坏恢复 | Done | Codex | P0-08, P1-06 | [P2交付](reports/20261024_p2_implementation.md) | 单写者/ACL/原子存储及manifest前后恢复通过；外部组合故障归P2-15 |
| P2-02 | P2 | W15 | 默认设置/DPAPI/偏好持久化 | Done | Codex | P2-01, P1-03 | [P2交付](reports/20261024_p2_implementation.md) | worker异步保存/迁移、DPAPI成功及损坏拒绝通过，默认不自动启动 |
| P2-03 | P2 | W08 | 内核适配/进程所有权/Ready/停止/Job | Done | Codex | P2-01, P0-04 | [P2交付](reports/20261024_p2_implementation.md) | 官方摘要/版本、认证Ready、owned回环PID、挂起归Job及有界输出；当前直接终止owned Job |
| P2-04 | P2 | W09 | 配置子集/会话物化/隔离校验/停机选择 | Done | Codex | P2-02, P2-03, P0-03 | [P2交付](reports/20261024_p2_implementation.md) | 原文/语义/会话分离、空历史/停机选择、隔离官方-t通过 |
| P2-05 | P2 | W09 | 首次启动/运行应用/补偿与 manifest 权威 | Done | Codex | P2-04 | [P2交付](reports/20261024_p2_implementation.md) | 统一受控重启，节点读回后提交；运行端口失败恢复旧版本；热重载未启用 |
| P2-06 | P2 | W10 | 首页/组选择恢复/模式与有效配置 | Done | Codex | P2-05, P1-03 | [P2交付](reports/20261024_p2_implementation.md) | 真实状态/组选择/模式、HMAC身份重开恢复、脱敏预览通过，实时流量后续 |
| P2-07 | P2 | W11 | HTTPS/本地导入与订阅最小管理 | Done | Codex | P2-02, P2-05 | [P2交付](reports/20261024_p2_implementation.md) | 有界下载、坏候选保留、两配置/名称/来源头/更新/最后项删除通过 |
| P2-08 | P2 | W13 | Windows 系统代理适配/租约/迁移 | Done | Codex | P2-01, P2-03, P2-05 | [P2交付](reports/20261024_p2_implementation.md) | WinINet LAN快照读回/幂等/迁移/冲突/显式重开/磁盘失败恢复；不覆盖WinHTTP/RAS |
| P2-09 | P2 | W08, W13 | 组合启停/崩溃有限恢复/退出清理 | Done | Codex | P2-06, P2-08 | [P2交付](reports/20261024_p2_implementation.md) | 首页/托盘统一门面，代理意图独立持久化及补偿、退出保留/UserStop清除；强杀组合归P2-15 |
| P2-10 | P2 | W12, W15 | 有界错误日志/最小修复入口 | Done | Codex | P2-05, P2-09 | [P2交付](reports/20261024_p2_implementation.md) | 无内核错误/端口/导入选择/停止恢复、打开数据目录及手工修复说明可用 |
| P2-11 | P2 | W17 | Windows 换网/休眠最小核对 | Review | Codex | P2-07, P2-09 | [P2交付](reports/20261024_p2_implementation.md) | 已接OS事件与1.5秒合并/过期结果取消，模拟调用已测；补一次实际换网/唤醒与请求计数核对 |
| P2-12 | P2 | W20 | 可配置单目标 HTTPS 测试 | Done | Codex | P2-06, P2-09 | [P2交付](reports/20261024_p2_implementation.md) | 自设目标DPAPI保存、当前入口HEAD/12秒/并发1、拒redirect/无直连回退 |
| P2-13 | P2 | W20 | 受控远端代理/HTTPS 端到端夹具 | Done | Codex | P2-05, P2-06, P2-07 | [P2交付](reports/20261024_p2_implementation.md) | 独立GET经owned入口/域名CONNECT到受控TLS服务，远端记录断言通过；本机夹具范围 |
| P2-14 | P2 | W13, W20 | 系统代理端到端/重复租约/端口迁移 | Done | Codex | P2-08, P2-09, P2-13 | [P2交付](reports/20261024_p2_implementation.md) | 真实PRECONFIG HTTPS/重复开启/迁移及原快照恢复通过，PAC/冲突另有注入适配器证据 |
| P2-15 | P2 | W07, W08, W09, W11, W13 | 首次失败/事务中断/核心崩溃/强杀恢复 | Blocked | Codex | P2-07, P2-09, P2-10, P2-11, P2-14 | [P2交付](reports/20261024_p2_implementation.md) | 等P2-11实际事件核对；已测部分中断/坏更新/占用端口/核心崩溃，补客户端强杀→下次租约及首次失败关键组合 |
| P2-16 | P2 | W18 | Windows 离线验证包与干净环境运行 | Review | Codex | P0-09, P2-03, P2-04, P2-07, P2-10, P2-12, P2-14 | [P2交付](reports/20261024_p2_implementation.md) | 完整本地包/CRT/官方内核/许可/摘要/说明和随包smoke已具备；缺普通用户无Qt开发工具的干净环境 |
| P2-17 | P2 | W06, W19, W20 | G2全部MVP证据与真实闭环资源报告 | Blocked | Codex | P2-12, P2-13, P2-15, P2-16, P2-21, P2-22, P2-23, P1-05 | [协议交付](reports/20261024_p2_protocols.md) | 基础协议/自动组/诊断完成；真实OS事件、关键恢复及干净环境未关闭，资源仍非全部组合验收 |
| P2-18 | P2 | W05 | 窄窗竖屏布局与三个底部导航入口 | Done | Codex | P2-06 | [初始交付](reports/20261024_p2_responsive_localization.md)、[缩放修复](reports/20261024_resize_layout.md) | 360×480、600断点、八路由/状态/键盘保留；P2-24补真实窗口连续缩放，不代表真实转发时帧验收 |
| P2-19 | P2 | W05, W15 | 英文源文案/中文翻译/自动系统语言 | Done | Codex | P2-18 | [本轮交付](reports/20261024_p2_responsive_localization.md) | Qt词表内嵌，系统首选序匹配/英文回退、172条翻译和中英文截图通过 |
| P2-20 | P2 | W11 | 标准HTTPS订阅链接协商/内容识别/错误说明 | Done | Codex | P2-07 | [本轮交付](reports/20261024_p2_responsive_localization.md) | 默认UA/Accept、头覆盖/空白、误标MIME与坏更新保留通过；4个私有样本仅识别，配置能力待扩展 |
| P2-21 | P2 | W09, W10 | Hysteria2/Trojan/VLESS/VMess基础协议与常用TLS/Reality传输 | Done | Codex | P2-20 | [协议交付](reports/20261024_p2_protocols.md) | 逐协议/嵌套字段、官方隔离-t/Ready/选择重启、身份与脱敏通过；未测新增协议公网握手 |
| P2-22 | P2 | W09, W10 | url-test/fallback组与能力化展示 | Done | Codex | P2-21 | [协议交付](reports/20261024_p2_protocols.md) | Selector/URLTest/Fallback能力映射、健康检查跳过REJECT、嵌套/引用保护与界面通过 |
| P2-23 | P2 | W09, W12 | 协议/组/规则/DNS具体不兼容原因与中英文提示 | Done | Codex | P2-22, P2-19 | [协议交付](reports/20261024_p2_protocols.md) | 最多32项安全结构诊断、七类不兼容与字段错误、中英文词表187条完成 |
| P2-24 | P2 | W05 | 缩放跨导航断点时消除D3D11秒级等待并稳定壳布局 | Done | Codex | P2-18 | [缩放修复](reports/20261024_resize_layout.md) | 默认basic/D3D11关闭帧延迟等待对象；4/4检查与真实窗口160步通过，页面/需求保留，最终包Ready首页专用工作集60.75MB；完整帧/转发组合未测 |
| P3-01 | P3 | W10 | 筛选/节点延迟/动态身份恢复 | Todo | 未分配 | P2-17 | — | 按 08 完成 PX 扩展与限额 |
| P3-02 | P3 | W11 | 计划订阅/用量、受管provider及GEOIP/GEOSITE/RULE-SET资源 | Todo | 未分配 | P2-17 | [功能分期](reports/20261024_mihomo_feature_plan.md) | 官方兼容Geo来源/摘要/许可、离线同版本、资源缺失/损坏/更新回退与受控规则匹配 |
| P3-03 | P3 | W12 | 连接/日志/规则/提供者完整页面 | Todo | 未分配 | P2-17 | — | 按需数据流、虚拟化、关闭及脱敏导出 |
| P3-04 | P3 | W14 | Windows 权限服务/隔离控制器/TUN | Todo | 未分配 | P2-17 | — | 验证固定 IPC、身份/字段/路径和迁移清理 |
| P3-05 | P3 | W15 | 完整设置/备份/诊断 | Todo | 未分配 | P2-17 | — | 完成安全恢复、支持包与 schema 验证 |
| P3-06 | P3 | W20 | 网站/系统资源完整能力与可选 IP 边界 | Todo | 未分配 | P2-17 | — | NET/RES 必需；IP 启用则完整验收，否则明确隐藏 |
| P3-07 | P3 | W06, W19 | G3主平台Alpha与组合资源验收 | Todo | 未分配 | P3-01, P3-02, P3-03, P3-04, P3-05, P3-06, P3-08, P3-09 | — | 纳入真实解析/资源更新/高级能力及必要组合资源检查 |
| P3-08 | P3 | W09, W15 | fake-ip/redir-host DNS、解析/过滤/缓存与回环防护 | Todo | 未分配 | P2-17, P3-02 | [功能分期](reports/20261024_mihomo_feature_plan.md) | DNS与TUN解耦；默认回环/端口冲突/引导与回退；系统DNS劫持另依赖P3-04 |
| P3-09 | P3 | W09, W10 | TUIC等高级协议/传输与load-balance策略扩展 | Todo | 未分配 | P2-17 | [功能分期](reports/20261024_mihomo_feature_plan.md) | 按官方内核能力分项实现与支持矩阵，不任意透传外部文件/下载字段 |
| P3-10 | P3 | W09, W10, W11, W20 | 代表订阅的受管Geo/最小DNS兼容及真实节点HTTPS链路 | Doing | Codex | P2-07, P2-21, P2-22, P2-23 | [订阅闭环](reports/20261024_p3_subscription.md) | 匿名S3预检拒绝；补固定Geo与有界DNS，保持原规则并验证真实节点转发；完整P3-02/08继续独立排期 |
| P4-01 | P4 | W16 | Linux 桌面代理/托盘/权限服务 | Todo | 未分配 | P3-07 | — | 分别记录 GNOME/KDE/X11/Wayland 支持范围 |
| P4-02 | P4 | W16 | macOS 菜单栏/代理/helper | Todo | 未分配 | P3-07 | — | 记录多网络服务、签名、公证与授权路径 |
| P4-03 | P4 | W17 | 三平台网络/电源矩阵 | Todo | 未分配 | P4-01, P4-02 | — | 运行休眠、换网、时钟/会话变化用例 |
| P4-04 | P4 | W18 | 正式安装器/签名/应用与内核更新恢复 | Todo | 未分配 | P4-01, P4-02 | — | 完成来源、防重放、schema 与独立回退 |
| P4-05 | P4 | W06, W19 | G4 三平台 Beta/功能与资源矩阵 | Todo | 未分配 | P4-03, P4-04, P4-06 | — | 正式支持范围与各平台真实负载证据齐全 |
| P4-06 | P4 | W02, W16 | Linux/macOS 原生空壳与多平台 CI 实际运行 | Todo | 未分配 | P0-07 | [当前验证](reports/20261024_p0_p1_validation.md) | 从 P0-07 后置；按冻结工具链原生构建/启动、无 UI/QML 测试与 CI run 留证，不阻塞 Windows 首版 |
| P5-01 | P5 | W19 | 代表性持续运行/关键故障与升级回归 | Todo | 未分配 | P4-05 | — | 按发布风险选取持续运行与资源抽查；24/72小时、全组合、帧/GPU和压力专项不统一强制，出现问题再扩展 |
| P5-02 | P5 | W19 | 候选发布/许可源码/用户说明与 G5 | Todo | 未分配 | P5-01 | — | 核验签名/来源、恢复说明与最终报告 |
