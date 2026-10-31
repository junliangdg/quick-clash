# 阶段与 MVP 验收记录

本文件只维护阶段门、验收条件、实际结果、证据和待补项；开发及修改过程见[开发日志](DEVLOG.md)，命令和历史证据见[报告索引](reports/README.md)。

当前内存优化各切片实现Review，专项产品验证NotRun；本机MVP Pass=16/16，G2-A—E证据齐全，G1仍Stale，因此G2/G3整门为NotRun。[逐项验收](reports/windows_validation.md#g2本机mvp逐项验收)不关闭[内存优化专项](reports/20261026_client_memory_optimization.md#验证范围)；阶段范围见[设计01](../design/01-目标范围与阶段计划.md)，MVP条件见[设计19](../design/19-网络测试与信息采集.md)。

## 1. 阶段门状态

| ID | 阶段 | 状态 | 证据 | 下一步/阻塞原因 |
|---|---|---|---|---|
| G0 | P0 | Pass | [当前验证](reports/windows_validation.md#工程与内核) | Windows 本地 Release/启动/QML/headless、官方内核关键契约和部署通过；跨平台原生与 CI run 后置 P4-06 |
| G1 | P1 | Stale | [当前视觉基准](reports/20261027_unified_visual_implementation.md#设计基准)、[P3补验](reports/20261027_unified_visual_implementation.md#p3字体与可访问性补验) | P3-27字体注入/名称/键盘和125%DPI原生检查已通过并进入Review；实际OS文本设置、高DPI/多屏迁移及读屏NotRun，保留原生命周期/资源边界，不提升整门Pass |
| G2 | P2 | NotRun | [16项逐条件验收](reports/windows_validation.md#g2本机mvp逐项验收) | MVP16/16及G2-A—E通过，P2全部Done；前一门G1 Stale，待P3-27补实际OS可访问性后才能提升整门 |
| G3 | P3 | NotRun | [Windows方案](reports/windows_validation.md#阶段范围与待补项)、[补证分期](reports/windows_validation.md#阶段范围与待补项) | 八页面/真实转发、TUN/服务、静态兼容及P3-14干净普通用户环境与有限OS/GUI宿主证据；其他平台后置 |
| G4 | P4 | NotRun | — | 三平台支持/更新/安装/恢复矩阵 |
| G5 | P5 | NotRun | — | 长稳、性能与稳定候选最终门 |

勾选每条时在该行附实际 reports 报告链接；尚未完成整个阶段时门仍为 NotRun/Blocked。阶段 Pass 还要求本阶段任务 Done、前一门 Pass 和汇总报告。Fail/Blocked/Stale 不使用勾选替代状态；回归时撤销受影响勾选并保留旧报告链接。

### G0：工程与关键契约

- [x] G0-A：具体工具链/Qt/解析器/OS、依赖和许可台账冻结；官方 Mihomo 资产/CPU/摘要、Geo 支持范围与来源可核验。Windows 资产离线校验完成，其他系统仍为候选。[P0 报告](reports/windows_validation.md#工程与内核)
- [x] G0-B：P2 配置策略与脱敏正反样本完成；同一官方内核在零网络能力 AppContainer 中执行 -t 正反例、无新增文件；隔离探针先核对父进程可连接而子进程受阻。[当前验证](reports/windows_validation.md#工程与内核)
- [x] G0-C：真实内核启动/校验/认证/API/组选择/模式/Ready、owned PID 回环监听与 Windows Job 子进程回收通过，能力限制明确。[当前验证](reports/windows_validation.md#工程与内核)
- [x] G0-D：按用户 2026-10-24 调整，Windows 本地空壳 Release/启动、QML 与无 UI 契约测试通过，基础 CI 配置具备；跨平台原生与 CI run 由 P4-06 后置。[当前验证](reports/windows_validation.md#工程与内核)
- [x] G0-E：统一视觉规范/概念预览、构建/部署与离线包方案齐全；原部署命令及脱离开发Qt PATH启动证据有效。按1.9完整本机包归P2-16，普通用户干净环境归P3-14。[统一规范](reports/20261027_unified_visual_implementation.md#设计基准)、[部署验证](reports/windows_validation.md#工程与内核)、[范围复核](reports/windows_validation.md#阶段范围与待补项)

### G1：UI 与资源路线

- [x] G1-A：桌面左侧八模块入口/路由/嵌入复用独立，导航常驻不创建全部页面，移动只验边界。[当前验证](reports/windows_validation.md#工程与内核)
- [ ] G1-B：P3-16方案C八页面/双主题/草稿/菜单/确认/错误态历史证据保留；P3-27正文14/21/32px×宽窄窗八路由、中英文名称/键盘、标准Cancel/返回及卸载字体重建、原生D3D11/125%DPI补验通过。实际OS文本设置、高DPI/多屏迁移与读屏NotRun，不能用注入字号替代；本项及G1保持未关闭。[逐项补验](reports/20261027_unified_visual_implementation.md#p3字体与可访问性补验)
- [ ] G1-C：Stale。单实例/托盘/无托盘退路及100次句柄历史证据保留，当前host_lifecycle通过；2026-10-30额外原生回放tray-test在实验性卸载重建出现theme销毁期QML错误、exit11，修改前后部署包均可复现，不能称该条件当前整体Pass。生产会话Host既有有限证据保留，需定向核对回放销毁与恢复。[本轮失败记录](DEVLOG.md#2026-10-30--灵猫q品牌与侧栏收展)、[Qt6.8回归](reports/20261027_github_release.md#ci故障与修复)
- [x] G1-D：Release环境、三项内存原始数据具备，六场景双主题单轮短测12/12通过，专用工作集49.7—54.9 MB；按1.6无需长时/帧/GPU矩阵。[范围重评](reports/windows_validation.md#原型生命周期与资源)
- [x] G1-E：已记录原型实测及未来解析、仓库日志、服务更新的资源风险；固定MB分项预留不再作为当前要求，真实组件随交付测量，不将估算称为实测。[原型历史](reports/windows_validation.md#原型生命周期与资源)、[当前资源策略](reports/20261027_unified_visual_implementation.md)

当前视觉交付单独记录，不能以文档检查替代产品验收：

| 范围 | 结果 | 证据/下一步 |
|---|---|---|
| 仪表盘视觉层级与图标 | Pass | 视觉定向：Windows构建/lint、localization及8项UI（含键盘、宽窄、订阅/列表双语）通过，45张原生D3D11截图、对比度核对和10秒资源短测完成；旧五卡截图Stale，由本轮替代。全页双语可访问性Fail于未修改的订阅密码输入框名称，未声称可访问性完整通过；既存Qt警告/整门状态保留。[记录](DEVLOG.md#2026-10-31--仪表盘视觉层级与图标) |
| 仪表盘运行概况紧凑卡 | Pass（定向范围） | 内核名称/版本与内存/时长并列，重复进程详情移除；Windows Qt6.12构建/lint、localization/qml_smoke/ui_visual/ui_responsive 4/4通过，D3D11/DPR1.25中文39张截图核对四/两/单列、双主题与详情。旧布局截图Stale，由本轮替代；既有资源共享采样/释放和10秒短测证据保留，ui_visual再次覆盖缺失/过期/部分数据及端口状态。既存Qt警告及长稳/服务模式限制保留。[记录](DEVLOG.md#2026-10-31--运行概况内核信息与详情去重) |
| 仪表盘五卡与网络工具 | Pass（定向范围） | Windows Qt6.12增量构建/lint、12项定向测试、D3D11/DPR1.25双主题31张原生截图通过；360/800/1000/1200/1440宽度、顶部等宽等高及宽屏内部两栏核对通过；真实Ready首页10秒三指标采样与退出回收正常。原连接与系统布局/资源展开折叠采样证据Stale，由运行概况专项替代；公网IPv6成功和长期资源未验，其他页面既存颜色警告保留。[记录](DEVLOG.md#2026-10-31--仪表盘五卡与网络工具) |
| 开发目录主界面启动 | Pass | Windows增量构建自动部署Qt依赖；清Qt PATH的4/4窗口回归、普通配置原生主界面及关闭到托盘后单实例重开、独立部署smoke及服务status通过。Qt6.12个人启动仍记录8条颜色undefined警告，未扩大为全UI无警告或G1-C整体Pass。[记录](DEVLOG.md#2026-10-31--开发目录主界面启动修复) |
| 灵猫Q品牌与侧栏收展 | Pass | Windows构建/lint、7/7现有回归及居中修正4/4、EXE九档图标逐字节核对、独立部署原生双主题/宽窄截图及188项完整离线包核对通过；旧紫底Q及顶部交互由当前证据替代。额外tray回放卸载失败独立记G1-C Stale，不扩大本项结论。[记录](DEVLOG.md#2026-10-30--灵猫q品牌与侧栏收展) |
| 导航圆角线性图标替换 | Pass | 用户采用第一套，八个本地SVG统一侧栏/图标轨/底栏；Windows Qt6.8增量构建/lint及6/6回归通过，独立部署清Qt PATH启动、D3D11/125%DPI原生双主题与宽窄导航核对通过。[记录](DEVLOG.md#2026-10-30--导航圆角线性图标) |
| 方案C设计、静态概念图与离线预览 | Pass | 导航/品牌与产品同源SVG，Light/Dark静态图已重建；1200/800/360宽×双主题六组合无溢出/缺图，鼠标和键盘收展通过；未受影响的历史48项响应式/74组对比度证据保留。浏览器预览不替代产品验收。[记录](DEVLOG.md#2026-10-30--灵猫q品牌与侧栏收展) |
| 方案C Qt Quick共享组件、八页面与代表状态 | Pass | 当前导航补验构建/lint、6/6回归及独立部署26张原生截图通过，覆盖八页面双主题、菜单/确认/错误态和360/800/1000/1200宽；另补两张明暗图标轨。旧符号截图仅作历史，当前SVG取代该部分证据；其余P3-16有效证据保留。[实施报告](reports/20261027_unified_visual_implementation.md)、[本轮记录](DEVLOG.md#2026-10-30--导航圆角线性图标) |
| P3-27字体与键盘产品补验 | Pass | Windows Qt6.8增量构建/lint、最终13/13回归28.51秒；原生D3D11/125%DPI单屏、14/21/32px×360/1000宽八路由、711条控件名称记录、默认取消/焦点返回、只读预览和字体重建通过；仅上述有限范围。[记录](DEVLOG.md#2026-10-29--p3字体与可访问性) |
| 实际Windows文本大小/高DPI多屏迁移/读屏 | NotRun | 单屏DPR1.25；本轮真实设置窗口应用授权超时，未改系统值或运行读屏。用户暂无额外环境，P3-27 Review/G1 Stale保持；字体注入和QAccessible名称不能替代。 |

### G2：Windows MVP

- [x] 启动与服务入口修复 Pass（有限范围）：2026-10-30旧会话CoreListenersNotOwned及便携包ServiceIdentityRejected的失败事实保留；已增加端口预检/有界Ready核对，完成7项回归、完整包验证及实际受保护安装更新。个人配置由应用事务保存为7892/9091并Ready，两个回环监听属于随包核心；普通GUI服务身份/控制/断线清理通过，便携包拒绝并提示正式路径。另一代理Meta网卡正在接管默认路由，本轮未切换实际TUN，P3-04路由/DNS与P3-25跨用户仍NotRun。[记录](DEVLOG.md#2026-10-30--内核监听与服务安装入口修复)

追加功能检查（不替代下列G2-A—E或16项MVP）：

- [x] 端口冲突与TUN名称/路由保护 Pass（实现/定向范围）：Windows构建/lint、10项回归和最终runtime补验通过，精确端口原因、候选确认/取消/回滚、第三方Meta不得认领、稳定名称/GUID/LUID和DNS残留策略通过；188项完整包清Qt环境导入Ready/缺核心修复通过。正式安装未更新，旧服务相关结论对此实现Stale；实际新服务/TUN创建、转发与单一TUN环境清理仍NotRun/P3-04/25，不提升G3。[记录](DEVLOG.md#2026-10-30--端口与tun冲突处理)

- [x] P3-24：支持包固定字段/未知字符串拒绝、冻结预览/确认/隐藏取消、无内核与只读导出；损坏/新版schema原字节归档、重开空仓库不启动、中断标记/文件占用失败回退、旧确认/未处理租约拒绝通过。总量1GiB/缓存64MiB/导出日志15MiB软预算保留配置/历史/事务/草稿/核心缓存/备份/归档，超额提示而不删保护数据；测速凭据及草稿保留有回归。Windows增量构建/lint、最终9/9回归及完整runtime114.03秒Pass，10秒162轮预览/取消三指标与释放核对通过；无GUI短测不代表长期/GPU结论，真实服务/TUN仍未验收。[验收记录](DEVLOG.md#2026-10-29--p3支持包与存储修复)

- [ ] 本机Qt6.8更新/切页内存异常调查：真实GPU EX2专用175.68MB/工作集768.73MB；Qt软件129.34—131.44MB/690.97—693.07MB，字体驻留504→491MB仍存在；当前不设固定MB门槛，异常调查仍未关闭。可选--software-rendering构建与3项UI测试通过，隔离切页确认旧页面/节点控件销毁；保留原符号/Qt字体，实际异常修复未关闭。[调查报告](reports/20261026_subscription_memory_investigation.md)

- [x] P2-18：窄窗断点、路由/键盘/状态保留的技术证据有效；当前四入口与方案C视觉归P3-16核对。[响应式技术交付](reports/windows_validation.md#订阅与代理功能)、[当前基准](reports/20261027_unified_visual_implementation.md#设计基准)。
- [x] P2-19：英文源文本、172条中文、系统首选序与英文回退、随包资源；[响应式/语言/链接适配交付](reports/windows_validation.md#订阅与代理功能)。
- [x] P2-20：标准链接协商、误标MIME内容识别、显式头及坏更新保留；[响应式/语言/链接适配交付](reports/windows_validation.md#订阅与代理功能)。
- [x] P2-21/22：基础字段/官方加载与启停、自动组能力/受控健康检查和实际代理页通过；不代表公网握手。[协议交付](reports/windows_validation.md#订阅与代理功能)
- [x] P2-23：最多32项具体协议/传输/组/规则/DNS/外部资源原因、安全路径、脱敏和中英文词表通过。[协议交付](reports/windows_validation.md#订阅与代理功能)
- [x] P2-24：默认Windows D3D11连续跨1000/600断点无原秒级等待；首页/代理各80步保持页面实例、引擎和需求，4/4 UI检查通过；最终包10秒Ready首页专用工作集60.75MB，非完整帧/转发组合验收。[缩放修复](reports/windows_validation.md#订阅与代理功能)
- [x] P2-25：离线url-test/fallback不伪造current，automatic/sources投影与父Selector嵌套出口递归完成；组读取失败保留最后快照并标记stale/error，refresh_groups只重读内核展示快照；代理页等待/来源/刷新反馈通过。Windows增量构建、contracts/localization/runtime/engine_startup/qmllint通过；真实provider远端刷新、真实健康检查故障转移及GUI目视NotRun。[开发与验收](DEVLOG.md#2026-10-31--代理自动组离线嵌套与新鲜度补全)

- [x] G2-A：MVP-01—16按设计19/1.9逐条件Pass，版本/证据/限制齐全；不使用N/A。[逐项验收](reports/windows_validation.md#g2本机mvp逐项验收)
- [x] G2-B：真实GUI HTTPS订阅/第二本地导入→官方核心→代理页节点选择/首页模式→系统代理→独立显式及PRECONFIG客户端受控HTTPS→正常退出/同目录重开通过。[主链路](reports/windows_validation.md#g2本机mvp逐项验收)
- [x] G2-C：首次关键失败/运行补偿/manifest前后/退避停止及生产worker强杀恢复、冲突/迟到/失败不删数据均有代表性证据；当前6/6定向回归及原生WinINet恢复Pass。[恢复映射](reports/windows_validation.md#g2本机mvp逐项验收)
- [x] G2-D：当前完整187项离线包摘要/依赖、清Qt环境本地导入Ready、缺core修复及秘密保护/手工说明通过；干净普通用户环境仍归P3-14。[本机包](reports/windows_validation.md#g2本机mvp逐项验收)
- [x] G2-E：真实Ready首页10秒93样本客户端三指标/内核单列，记录同轮首末/峰及退出清理；幂等/异步worker路径核对，不宣称长期或字体问题关闭。[资源范围](reports/windows_validation.md#g2本机mvp逐项验收)

上述本机条件通过不提升G2整门：G1仍Stale，须P3-27补实际OS环境验收。

### G3：Windows 日常使用交付

- [ ] P3-14 Review：最终包普通Medium令牌/清开发Qt环境/中文空格路径及缺核心修复、真实托盘五秒卸载/转发/单次重建、整GUI强杀→Job回收→同目录单实例重开，以及独立进程WinINet外部冲突/显式新基线释放/原快照精确还原均Pass；独立验收工具包Package/Flow本机Pass，用户暂无额外环境，干净普通用户机器和一次真实换网或唤醒仍NotRun。[逐项结论](reports/windows_validation.md#p3普通用户宿主补证)

- [ ] P3-25 Review：实际UAC安装及普通用户握手/SCM身份、固定命令、核心控制器/配置隔离、start重放、客户端强杀和服务强杀重启清理通过；跨用户桌面争用未运行，不能以同用户第二连接Busy代替。服务恢复能力明确NotAvailable，TUN/代理即时恢复未获Pass。[验收记录](DEVLOG.md#2026-10-29--p3权限服务)

| P3-25条件 | 结果 | 证据范围与限制 |
|---|---|---|
| SVC-01 | Pass | 当前便携受信根客户端可握手；普通PowerShell被ServiceClientUntrusted拒绝，静默/碎片/超限占满后3037ms回收，超额连接断开。新方案明确允许普通用户启停已注册服务，旧停止AccessDenied结论Stale |
| SVC-02 | Stale | 旧清单身份矩阵不适用；当前坏Geo摘要拒绝及权限拒绝opener单测通过，完整路径逃逸/链接/超权限资源真实矩阵未验，不能标整体Pass |
| SVC-03 | Pass | 当前便携服务实测客户端、owned内核、SCM服务分别强杀后旧核心消失、查询clean并可新启动；17秒暂停后单请求超时不拆会话、同PID恢复。不代表TUN故障或全部GUI宿主矩阵 |
| SVC-04 | Pass | 当前便携服务重跑协议99及无握手拒绝；旧注册/移动目录返回ServiceProtocolMismatch，未安装返回ServiceInstallationRequired，能力不匹配不进入权限命令 |
| SVC-05 | NotRun | 同用户第二连接Busy通过；缺第二活动用户桌面/切换的真实证据，RDP不受支持 |
| SVC-06 | Pass | 普通GUI无法打开真实核心pipe或读取服务配置；危险external-controller-pipe字段拒绝，固定组读取/选择通过 |
| SVC-07 | Pass | 当前便携服务实测start同ID精确返回相同PID/epoch，改报文复用ID断开；单测覆盖在途重复/淘汰ID拒绝 |
| SVC-08 | Pass | 定向：当前受信根握手，另一目录被拒；移动目录需重授权且修复后可用。完整链接/ACL/竞态矩阵未验 |
| SVC-09 | NotRun | 已测旧版UAC迁移、普通权限重开/停止后启动/同路径update、共享Geo与卸载；新方案完整首次GUI流程、实际TUN和系统重启未验 |
| SVC-10 | Pass | 定向：旧保护目录迁移清理、目录搬迁重授权、无效服务镜像注册失败后回退原注册与受信根；不代表并发/强杀安装矩阵 |
| SVC-11 | NotRun | 新便携服务实际TUN启动镜像追踪未验，第三方Meta保持启用 |
| SYS-05 | Pass | 仅能力降级分支：proxyRecovery=NotAvailable，不跨用户写HKCU，继续用户态租约/下次恢复；可信移交与即时恢复未实现 |

2026-10-31当前便携服务重验：最终Windows构建、10/10回归61.38秒及最后启动并发修订8/8回归31.91秒通过，实际普通服务启动/Geo/协议/免UAC更新再次通过；第三方Meta保持Up，新方案真实TUN与系统重启NotRun。[当前记录](DEVLOG.md#2026-10-31--tun便携目录持久服务替换)。

2026-10-31此前受信根服务补验（历史范围）：SVC-01/03/04/06/07上述定向范围已重跑通过（协议99/无hello拒绝、start同ID重放、改ID报文断开、控制器/配置拒读、固定组控制及同用户Busy）；3秒未拥有连接期限、16KiB握手限额及超过16KiB合法首个start通过。服务实际暂停17秒后单请求ServiceTimeout，同会话同PID恢复Ready，lost=false，显式停止后clean。新增显式析构修复socket销毁时回调已销毁成员；最终7/7回归Pass29.83秒。[完整记录与限制](DEVLOG.md#2026-10-31--服务连接回收与普通服务恢复验收)。真实TUN故障按用户选择NotRun。

- [ ] P3-28 Review：首次UAC直接注册便携目录的SERVICE_AUTO_START服务，普通GUI保持原权限，后续启动/同目录update免UAC；普通与服务模式共用官方内核/Geo来源，ProgramData仅保留保护授权、私有运行会话与清理记录。旧Program Files服务迁移及识别载荷清理通过；隔离副本的根外拒绝、移动后要求授权、授权修复、坏服务镜像触发原注册/受信根回退、卸载与便携文件保留通过，结束恢复build/dev注册。SVC-08为定向Pass（非完整权限/链接竞态矩阵）；SVC-09已测普通权限重开/停止后启动/同路径更新/共享Geo及卸载，完整首次GUI UAC链路、新方案实际TUN和系统重启仍未验；SVC-10为迁移/移动/失败回退的定向Pass；SVC-11 NotRun。旧复制安装/Manual/停止AccessDenied结论Stale，保留历史于DEVLOG。[替换与验收](DEVLOG.md#2026-10-31--tun便携目录持久服务替换)。

- [ ] P3-29 Review：服务每秒 status 的轮询曾把 ready=true/tun=false、请求超时、IPC 错误或缺少响应对象都当作内核退出，主动停止 owned 会话并报 CoreExited；这与 mihomo 崩溃无关，却导致 TUN 适配器消失、开关关闭和反复重启。现仅明确 ready=false 判定退出；状态失败记 Unknown，不停止内核；TUN 以 On/Missing/Unknown 独立观测，连续三次缺失才向界面报告不可用并在恢复 On 时清除；连续三次核心运行不足 60 秒的 TUN 失败后暂停自动重开 TUN，保留普通代理并报 TunRecoveryExhausted，用户显式操作 TUN 时重置。真实退出记录退出码、UTC 时间和脱敏截断的 stderr 尾部。补充修复：连接与握手共享 15 秒截止，握手后每次请求单独计时，单请求超时不再关闭服务会话或停核；握手携带 serviceBuild（服务程序 SHA-256），与随包服务载荷不一致时返回 ServiceProtocolMismatch 并请求一次受控 update；每次显式开启 TUN 都重新执行服务预检，持久化 tunDesired=true 不跳过；清理不确定只阻止服务模式，未建立会话的退出不再产生 ServiceCleanupUncertain；TUN 开关增加 tunPending/tunDisabling 过渡，用户实测开启保持、关闭立即保持且无回弹。Windows 增量构建与格式检查通过；core_health、service_protocol、engine_startup、ui_state、runtime_test 回归 Pass。用户本人多次开关真实服务 TUN，核心随开关预期重建，应用日志无新增 CoreExited/ServiceTimeout；验证时 TUN 开启，适配器 QuickClash-a880b33f（198.18.0.1）、默认路由 metric 0、DNS 198.18.0.2，经 TUN 的 HTTP 探测 204；关闭后适配器移除并由普通内核接管；适配器 LUID、连续短时失败后的 TunRecoveryExhausted 与普通代理恢复仍 NotRun，TUN-04/TUN-05 不得标 Pass。

- [x] P3-21：Core/App/Service来源、2000条/4MiB双预算/截断/丢弃、暂停缺口与跟随区分、冻结脱敏预览/默认隐藏正文/令牌失效完成；Windows主程序/服务构建、8项回归及最终4项UI/筛选、headless5项Pass。默认GPU2000条页面短测模型/页面释放通过；真实服务安装联验仍P3-25，未关闭字体驻留专项。[验收记录](DEVLOG.md#2026-10-28--p3日志与导出)
- [x] P3-23：schema2原文/覆盖与离线展开备份、schema1兼容、固定预检副本/确认令牌及整批官方校验完成；摘要/路径拒绝、晚项失败清理、取消和空仓库不启动通过。Windows增量构建/lint、5项定向及完整runtime Pass，10秒66次预览/取消短测候选均释放；不包含密码加密。[验收记录](DEVLOG.md#2026-10-28--p3配置备份恢复)
- [x] P3-26：客户端/已核验服务三指标聚合、内核与临时校验核心单列、PID/创建时间及SCM身份核对、缺失/异步失败清旧值与部分状态完成；共享5—6秒采样/多消费者及释放通过，Windows构建/lint、7项回归、最终真实内核和headless5项Pass。10秒进程短测有界，真实安装服务联验仍P3-25，不代表TUN或长期资源验收。[验收记录](DEVLOG.md#2026-10-28--p3资源采样)

- [x] P3-20：双库完整下载/官方隔离校验/配置事务切换、严格同摘要复用且不重启、第二项失败与坏资源保留、下载/校验取消及迟到拒绝、候选启动失败补偿、随包离线恢复/历史回退及重开引用通过；Windows构建/lint与最终4/4定向测试Pass，headless5/5含写入器完整性Pass。使用受控HTTPS和真实冻结Geo资源，公网可用性与TUN动态Geo未宣称。[验收记录](DEVLOG.md#2026-10-28--p3受管geo更新)

- [x] P3-06：NET-01—05三路径、无直连回退、TLS/跳转拒绝/循环/整链截止与超限、Cookie隔离、取消迟到/历史失效及8项批量/32历史淘汰完成；Windows构建/lint、13项回归与最终runtime100.84秒Pass。系统DNS可能返回NetworkError，保留真实分类；该历史范围不含IP；IP双路径扩展见仪表盘专项，未宣称公网网站验证节点转发。[验收记录](DEVLOG.md#2026-10-28--p3网站测试)

- [x] P3-05：设置请求/保存/当前生效区分，停止无生效端口；网络模式/端口/DNS一次确认并捕获epoch/选择代次，真实内核事务和端口失败补偿Pass。状态重开、过期确认拒绝与有界滚动弹层通过，最终4项UI回归Pass；沿用主题/托盘偏好证据，备份/诊断另验。[验收记录](DEVLOG.md#2026-10-28--p3设置生效状态)

- [x] P3-03：完整有界连接快照/速率边界、稳定详情和结束保留、超限暂停重试、捕获目标关闭计数/超时只核对完成；真实Mihomo关闭保留新连接、旧epoch拒绝及停止清理结果，Windows构建/lint与6项最终定向测试Pass；1000连接默认GPU短测/离页释放Pass。[验收记录](DEVLOG.md#2026-10-28--p3连接详情与关闭)

设计1.8明确Windows日常使用及共享静态兼容，1.9增设P3-14集中有限环境/宿主补证；Linux/macOS原生和移动后置。P3-12方案与P3-13静态核对分别留证，见[静态边界](reports/windows_validation.md#跨平台静态边界)与[G2范围复核](reports/windows_validation.md#阶段范围与待补项)；静态通过不等于非Windows支持。

- [x] P3-10最小切片：代表订阅成功导入/选择、官方owned核心Ready、真实VLESS TLS HTTPS转发及连接链，固定Geo离线验证与内部DNS，资源异常拒绝；不代表下列G3整阶段通过。[订阅闭环](reports/windows_validation.md#订阅与代理功能)

- [x] P3-01：独立展开/空集合恢复、真实选择读回/最新意图、设置目标及测速并发/500队列、取消迟到/10分钟过期、配置切换失效；500节点视口及键盘末尾定位、折叠/离页释放完成。沿用PX-01/03/08能力及身份恢复证据，Windows增量构建/lint与定向测试Pass；默认GPU短测仅证明对象有界，不关闭字体/缓存内存专项，动态provider身份归P3-19。[验收记录](DEVLOG.md#2026-10-28--p3代理与测速)、[测速调优](DEVLOG.md#2026-10-31--代理页测速并发与超时调优)

2026-10-31 P3-01/PX-07滚动性能补验：原实现按可视行范围重建全部节点委托，滚动跨行时整体销毁/创建卡片及其子对象；改为固定行池环形复用、只重绑成员，并移除逐节点ToolTip。Qt6.8.0 D3D11、1000×640、DPR1.25、500节点×3组短测：单组6秒CPU 5203→1406ms，中位帧132.1→15.2ms、p95 141.4→20.6ms；三组6秒CPU 5016→2172ms，中位帧82.4→15.5ms、p95 226.4→20.7ms；专用工作集单组94.2→87.0MB、三组峰值103.2→92.1MB。18秒重复滚动中位15.4/17.8ms，节点控件33个；折叠/离页归零，默认页内存探针退出0，UI回归5/5及3/3通过。仅为合成窗口短测，GPU利用率未单独仪器化，不代表G5长稳、字体驻留或实际用户滚动手感已整体通过。

- [x] P3-02：严格用量解析及采集时间、未知不归零、90%/耗尽/7天/已过期提示；同内容更新替换元数据、重开恢复、本地调度边界无额外下载。Windows构建/lint及15项定向测试Pass，复核计划合并/失败保留/自动应用及双语响应式卡片。[验收记录](DEVLOG.md#2026-10-28--p3订阅用量与告警)

- [x] P3-08内部DNS：fake-ip子网、IPv6/映射回环及bootstrap校验，policy/filter/缓存字段与模式覆盖；官方受控UDP解析、缓存命中/清理重查及redir-host覆盖通过。Windows增量构建/lint和6项定向测试、headless构建与5项测试Pass；外部DNS接管留P3-04。[验收记录](DEVLOG.md#2026-10-28--p3内部dns)

- [x] P3-09：TUIC字段与v4/v5认证互斥、原生UDP能力，TCP/WS/gRPC/Reality声明矩阵及三种load-balance策略；负载均衡不虚构当前节点、不允许手选，停机不恢复自动组选择。Windows构建/lint和4项测试、headless5项Pass，官方加载与能力读回通过；未声称所有远端协议握手。[验收记录](DEVLOG.md#2026-10-28--p3协议与负载均衡)

- [x] P3-18：主源显式Direct/RunningProxy/Auto及上次实际路径、最多5次HTTPS跳转与跨站认证/Cookie隔离完成；DPAPI来源绑定ETag/Last-Modified、304缺失/坏源单次重取、整链20秒/单跳15秒及16MiB、停止/删除迟到通过。Windows构建/lint及真实runtime、最终专项与双语UI回归Pass；provider下载策略独立，worker三指标短测不代表Qt字体问题关闭。[验收记录](DEVLOG.md#2026-10-28--p3订阅下载兼容)

- [x] P3-19：快照单项更新不重取主源/其他资源，缓存来源/摘要匹配、坏更新及停止迟到保留；显式运行时模式的use/RULE-SET、代理/规则热刷新不重启、坏载荷保留核心缓存、动态身份不保存/恢复、导入fallback备份实际恢复和停止后GC通过。固定服务kind/name白名单拒绝额外URL/未知身份；Windows构建/lint、完整runtime与专项/双语UI、headless5项及10秒worker短测Pass。运行时使用冻结内核解析/网络策略，真实安装服务/TUN仍P3-25 NotRun，完整规则页面由P3-22交付。[验收记录](DEVLOG.md#2026-10-28--p3提供者生命周期)

- [x] P3-22：有效序号/来源、RULE-SET计数/时间及未知/有限信息，所选快照/当前内核区分、逐项结果/待应用/失败/不确定、分类读取失败保留、串行刷新及覆盖编辑入口完成。Windows主程序/服务构建及lint、定向11/11与完整runtime110.75秒Pass；目录候选变化自动重读补丁后最后5/5 Pass，10秒43次刷新/最后需求释放清列表/状态/结果通过。API不能恢复快照展开前来源或可靠命中数，真实安装服务/TUN及G3不在本项结论内。[验收记录](DEVLOG.md#2026-10-29--p3规则与提供者页面)

- [ ] G3-A：P3-01/02/11的Windows订阅计划/失败保留、卡片自动应用、用量/到期/告警、组/节点及单独/批量测速可用；P3-18显式下载路径/安全重定向/304缓存，P3-19/20的provider/RULE-SET与Geo，P3-08/09的DNS/声明协议分别有证据。模式与选择读回；首页订阅卡不重复内核状态、上下行同卡，代理组独立展开并按订阅记忆，测速目标来自设置，测速行高度稳定。
- [ ] G3-B：八页面具备真实功能及设计17的方案C共享控件/完整Light/Dark状态；P3-17原文/覆盖编辑、草稿冲突/脱敏来源差异/历史回退，P3-03/21/22连接/日志/规则/提供者，P3-05/23/24设置/备份/诊断，P3-06/26网站/系统资源按条件通过。备份预览及恢复不接管旧会话，存储配额清理保护引用；后台内核/崩溃恢复可用，IP未启用则隐藏。
- [ ] G3-C：P3-25权限服务安装/身份/固定IPC/隔离/可信租约与P3-04 TUN授权、迁移和清理有实测；系统代理及未显式配置代理的TUN客户端分别验证真实节点转发。P3-14干净普通用户包/代表路径/基本修复、真实换网或唤醒、GUI强杀/下次恢复、运行会话隐藏重建及外部代理变更有记录；只补差异，不覆盖后续用户修改。
- [ ] G3-D：后台无页面专属连接/日志大流；P3-26按RES-01—03核对客户端/必要服务聚合、内核单列、PID复用/权限不足及共享需求释放。资源敏感交付选一个10—30秒短测或复用有效证据，记录三项内存及同环境变化/有界生命周期，不设固定MB或全场景矩阵。
- [ ] G3-E：P3-13静态兼容已知违规修正，P3-27补字体/DPI/键盘/代表状态并恢复G1有效结论；G2须实际通过，P3-07逐项汇总G3、Windows包/说明/限制和恢复证据。非Windows原生仍按实际状态记录，不作为G3前置，正式签名/在线更新归P4。

- [x] P3-17：1MiB原文/独立JSON覆盖、注释保留/持久草稿、新订阅冲突拒覆盖、另存/重载、脱敏双预览及来源层次、历史选择与摘要校验完成；Windows构建/lint、8项回归、真实provider回退及编辑候选启动失败补偿/重开草稿、headless5项Pass。24秒GPU短测仅证明隐藏/关闭释放文本及通道，Qt大文本内存峰值/缓存仍高，不代表低内存专项通过。[验收记录](DEVLOG.md#2026-10-28--p3配置编辑)

P3任务拆分和遗漏映射复核见[2026-10-28记录](DEVLOG.md#2026-10-28--p3任务范围与依赖复核)；P3-17—27承接独立交付，不表示产品能力已验证。G1仍Stale、G2/G3仍NotRun，MVP结果不变。

P3-13执行清单（均为静态或已有接口核对，不要求其他平台设备）：

2026-10-31补齐Unsupported ServiceClient::hadSession链接契约及共享显式析构定义；Windows上编译真实后端并直接调用该接口的测试Pass，包含最终7/7回归，见[接口修复](DEVLOG.md#2026-10-31--非windows服务会话接口补齐)及[最终验收](DEVLOG.md#2026-10-31--服务连接回收与普通服务恢复验收)。Linux/macOS原生构建与TUN能力仍NotRun。

- [x] 共享层/公开头不引用Windows具体凭据、代理、SCM/WinINet/DPAPI/资源采样或进程实现；DesktopServices职责接口由infra组装，include检查及人工核对通过。
- [x] CMake按平台选择源/PRIVATE系统库，公开头不泄漏平台SDK；Unsupported真实实现契约在Windows编译/测试通过，非Windows原生NotRun。
- [x] Qt路径/逻辑资源不固定盘符/注册表，PID仅桌面诊断/所有权；凭据/进程/代理拒绝变更且递归目录不新增，服务不打开IPC、缺失采样不报零。
- [x] 注册表投影/独立嵌入、英文源与中文原因、QObject context/generation及停止销毁保留替换点；中英文禁用控件/键盘与host生命周期Pass，移动VPN/后台/触摸NotRun。
- [x] 核对位置、修正、Windows14项/真实核心/headless5项回归及10秒采样写入[静态边界验收](reports/windows_validation.md#跨平台静态边界)，Linux/macOS原生/系统验收明确NotRun归P4。

### G4：后续逐平台扩展并汇总桌面三平台 Beta

Windows正式安装/更新先行，Linux/macOS各自补齐原生构建与实际功能；每个平台可单独留证/交付，不互等。G4用于未来三平台汇总，不阻塞G3 Windows交付。五平台不属于本门，移动端另按设计15启动。

- [ ] G4-A：P4-06 的 Linux/macOS 原生构建/启动及多平台 CI 有记录，Linux 与 macOS 实际支持平台/桌面/架构矩阵齐全，代理/托盘/权限差异有退路与运行证据。[CI配置核验与修复](reports/20261027_github_release.md#ci故障与修复)记录Windows远端进程测试Fail及本机编码修复，远端修复版待重跑；不能替代非Windows产品实测，阶段仍NotRun。
- [ ] G4-B：三平台换网/休眠/会话变化、系统代理/TUN 所有权及恢复矩阵通过。
- [ ] G4-C：正式安装/卸载、签名/公证/来源和内核资产清单通过，卸载不损害第三方状态。[Windows持续交付实现与本地干净包](reports/20261027_github_release.md)已有证据，真实远端发行/签名与正式安装验收NotRun。
- [ ] G4-D：应用/内核更新签名、防重放、schema、断点和独立回退通过，不能启动新应用时仍可恢复。
- [ ] G4-E：支持范围内功能/平台验收与真实组合资源预算通过，更新受影响的 MVP/功能结果。

### G5：稳定版候选

- [ ] G5-A：代表性持续运行和实际支持负载资源抽查无明显持续增长；24/72小时、全组合、精细CPU/帧/GPU按发布风险或异常专项选择，不统一强制。
- [ ] G5-B：故障/权限/配置和更新回退矩阵最终回归，无未解决的阻断缺陷。
- [ ] G5-C：受影响平台的 MVP 与必需功能验收对候选版本有效，旧通过结果未被直接沿用。
- [ ] G5-D：发行包/源码/许可/摘要/签名与构建记录可追溯，用户关闭/退出/恢复/平台差异说明齐全。
- [ ] G5-E：稳定候选、全部门和限制/缺陷记录一致，最终报告给出实际发布结论。

## 2. MVP 实际结果

每次验收用[验收报告模板](templates/acceptance-report.md)，关联构建/内核/OS/配置/目标和清理证据。下表按设计19/1.9记录本机条件，16项全必需，不得用NotRun/Blocked当N/A。16项是追踪编号，不要求16套独立测试；一次运行可同时证明多项。旧证据可逐条件映射复用，未完成映射不因范围收窄自动Pass；P3-14的日常补证单独记录，不混在本表反复加码。

| ID | 通过条件摘要 | 关联任务 | 结果 | 证据 | 下一步/失败原因 |
|---|---|---|---|---|---|
| MVP-01 | 本机完整离线包、依赖/摘要、清Qt PATH后本地导入/Ready | P2-03, P2-16 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 当前187项包hash/x64/CRT/官方核心/Qt/QML/Schannel、中文空格路径导入Ready及缺核心修复通过；干净机器归P3-14 |
| MVP-02 | HTTPS/本地导入、限额/TLS/坏候选保留 | P2-04, P2-07, P2-20 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | GUI HTTPS订阅及第二本地导入通过；HTML/空坏更新、16MiB+1与TLS名称拒绝保留sourceHash/PID/Ready |
| MVP-03 | owned单会话、认证/回环/PID/端口归属与幂等 | P2-03, P2-09 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 错secret拒绝/owned回环端口、占用补偿有效；GUI重复当前卡/非活动导入PID不变，模式事务先停后启 |
| MVP-04 | 代理页分组/节点可见可选、自动组能力与模式读回 | P2-06, P2-21, P2-22 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 真实代理页键盘选fixture/读回、首页Direct→Rule通过；声明协议加载与url-test/fallback能力证据有效 |
| MVP-05 | Ready 后写代理、独立系统客户端验证 | P2-08, P2-09, P2-14 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 独立WinINet PRECONFIG客户端经当前系统设置访问受控HTTPS，上游CONNECT/站点请求及完整原快照恢复通过 |
| MVP-06 | 显式本地入口与受控远端请求证据、域名 CONNECT | P2-13 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 独立显式客户端经本地入口访问受控HTTPS，域名CONNECT及远端GET核对排除DIRECT；真实重开再次通过 |
| MVP-07 | 停止/退出恢复owned状态、冲突不覆盖、端口修复 | P2-08, P2-09, P2-10, P2-15 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | GUI/原生worker恢复、实际外部冲突/新基线、坏更新/端口补偿与修复入口通过 |
| MVP-08 | 真实首页/错误、Ready首页三项内存短测及有界生命周期 | P2-06, P2-10, P2-17 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 10秒93样本三指标及内核单列、同轮首末/峰和正常退出清理具备；不关闭字体专项或宣称长期性能 |
| MVP-09 | 两配置/偏好重开/可靠身份恢复及失效保护 | P2-02, P2-04, P2-06, P2-07 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 两配置/选择代次/节点/主题/模式/端口真实重开一致；同名端点变化、已选成员删除和凭据变化不恢复旧身份 |
| MVP-10 | 来源/名称/更新/删除、迟到不复活/失败不删数据 | P2-07, P2-15 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 名称/URL头/更新/最后项删除、停止删除迟到、来源校验器及停止释放失败保留配置/代次通过 |
| MVP-11 | 声明协议/组/基础规则预检/官方加载及脱敏诊断 | P2-04, P2-07, P2-13, P2-21, P2-22, P2-23, P3-10 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 声明字段/官方加载/危险项拒绝/脱敏、固定Geo及内部DNS证据有效；不扩张全协议公网矩阵 |
| MVP-12 | 首次关键失败/运行回退/manifest前后恢复 | P2-05, P2-09, P2-15 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 首次配置/端口/平台/代理写后落盘、后台平台部分失败、运行补偿及manifest提交前后权威恢复通过 |
| MVP-13 | 崩溃退避/停止优先、生产worker强杀/Job/下次恢复 | P2-03, P2-09, P2-10, P2-15 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 连续四次恢复/停止打断；当前原生worker强杀、Job/端口清理及下次WinINet租约/事务恢复16.702秒Pass |
| MVP-14 | 幂等租约/端口迁移/PAC/外部冲突/新快照恢复 | P2-08, P2-14 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 原before幂等、迁移/PAC/自动发现/绕过、冲突/显式新before及原生恢复通过 |
| MVP-15 | Windows通知接入/受控合并核对、离线有界 | P2-11 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 通知/析构取消、20→1/PID不变/即时Stale/退出忽略及离线有界通过；真实OS事件归P3-14 |
| MVP-16 | 无内核基本修复/元数据/DPAPI/秘密保护/自设目标 | P2-02, P2-10, P2-12, P2-16 | Pass | [逐条件证据](reports/windows_validation.md#g2本机mvp逐项验收) | 缺core修复、坏/过新元数据只读、DPAPI失败拒绝、日志/诊断脱敏及自设HTTPS当前入口无直连回退通过 |

## 3. 失效与重跑

源码、依赖、内核、schema、配置策略或支持平台变化时，依据影响将相关 Pass 标 Stale，撤销对应阶段检查项，旧报告保留。行为变化导致验收失败则标 Fail；补救完成重跑后再标 Pass。G2 Pass 只表示记录版本的 Windows MVP，不自动证明三平台/稳定版。P3/P4/P5 汇总报告必须逐项注明哪些旧验收仍有效、哪些已重跑；新平台需新增对应报告，不把 Windows 记录当作其他平台结果。
