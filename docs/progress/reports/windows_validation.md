# Windows 阶段验证摘要

本页合并工程、原型、P2/P3功能与恢复证据，并补充后续阶段专项结论，只证明各自版本的已测范围；当前状态以[任务](../TASKS.md)和[清单](../CHECKLISTS.md)为准，不从历史Pass推断当前产品通过。日常修改不再生成report。

2026-11-02复核：P3主要业务切片已有实现，P3-04原Todo漏记了TUN设置/服务预检、模式迁移、失败补偿和清理保护，现改Blocked（P3-25前置验收未关闭）。历史复制安装服务的人工TUN开关/HTTP204不能替代当前便携服务的完整转发/恢复验收。当前Qt6.12开发产物20项不同回归中，排除沙箱影响后18项Pass，中英文可访问性测试均Fail于subscription_url密码输入框名称为空；ui_prototype与host_lifecycle本轮Pass，原生tray卸载未复验。无新构建或服务/网络变更；P3-14/25/27/28/29仍Review、P3-07汇总仍Todo，G1 Stale、G2/G3 NotRun。详见[复核与命令](../DEVLOG.md#2026-11-02--p3实现与台账复核)。

## 工程与内核

2026-10-31主界面启动修复：上轮为开发服务手工复制QtCore/Network引起Qt路径重定位，QtQuick.Controls插件未找到，普通启动只保留先创建的托盘。当前Windows构建改用windeployqt收集QML/插件/动态DLL，并生成相对qt6.conf；增量构建、清Qt环境4/4启动/窗口回归、个人配置原生主界面及托盘隐藏后单实例重建、独立部署smoke与普通服务status通过。个人Qt6.12启动另有8条颜色undefined警告，不关闭旧G1-C回放问题。详见[修复记录](../DEVLOG.md#2026-10-31--开发目录主界面启动修复)。

2026-10-31便携持久服务替换：当前实现已改为首次UAC直接注册便携目录服务，SCM为AUTO_START/LocalSystem，普通GUI可启停已授权服务，同目录update日志确认Administrator:no。普通与服务模式共用官方内核及选定Geo；不再保留Program Files服务载荷。实际旧服务迁移与清理、普通服务三类进程强杀/17秒暂停恢复、更新Geo加载及坏摘要拒绝通过；隔离副本移动后拒绝、重新授权、坏镜像注册回退、卸载后文件保留均通过并恢复开发目录注册。Windows增量构建与QML lint退出0（订阅页有3条既存类型推断警告），10/10受影响回归Pass61.38秒；最后启动并发状态修订后8/8回归Pass31.91秒，普通服务启动/Geo/协议/免UAC更新探针再次通过。文档/台账/命名/平台边界与格式检查通过。真实TUN/系统重启与跨用户未验；旧复制安装、Manual及普通用户停止拒绝的运行结论Stale，以下保留历史，详见[当前记录](../DEVLOG.md#2026-10-31--tun便携目录持久服务替换)。

历史方案（已由便携持久服务替换）：2026-10-31原生服务入口（取代 PowerShell 安装脚本）：便携/开发目录开启 TUN 遇到旧服务在 hello 阶段断线时，客户端现将该失败握手归为 ServiceProtocolMismatch，而不是终态 ServiceUnavailable；GUI 以同一 quickclash.exe 的固定 --service-setup 动作发起一次 UAC，按未安装/协议不兼容/已停止或未授权分别走 install/update/authorize，成功后原进程自动重试一次。服务安装、更新、授权与卸载全部由程序内代码完成，不再依赖 PowerShell、安装脚本或包清单；服务程序、内核与 Geo 仍只从 `C:\Program Files\QuickClash` 启动，该目录只是服务载荷，不复制 quickclash.exe，也不是第二个 GUI。install 可归档没有服务注册的残留目录，install/update 只保留最近一份旧安装，authorize 只替换受信根。Windows dev 与 CI 增量构建通过，6/6 定向测试 Pass（trusted_client_root、engine_startup、unsupported_platform、localization、ui_state、service_protocol）；旧服务只读 status 探针在当前构建返回 ServiceProtocolMismatch，指向一次 update 授权；设计合并与检查、台账/源码布局/平台边界检查通过。真实交互式 UAC 更新与 helper 退出后的自动重试已补验，见下条；TUN 实际开启/转发、便携目录移动/改名后的重新授权、取消 UAC 及 SVC-08/10/11 仍 NotRun，SVC-09 只记录到“更新成功并自动重试进入预检”，不因随后路由冲突拒绝而标完整 Pass；此前“便携 GUI 明确返回 ServiceInstallationRequired”的结论由本轮取代。

历史方案（复制安装相关结论Stale）：2026-10-31服务更新实测：从 `build/dev` 普通权限 GUI 开启 TUN，旧服务在 hello 阶段断线被识别为 ServiceProtocolMismatch，GUI 发起一次 update UAC；用户确认后成功日志 `C:\Users\Administrator\AppData\Local\Temp\quickclash-setup-1793384916538.log` 显示管理员 helper 复制服务依赖、等待旧服务进程退出、归档旧安装、写入受信根并重启服务，exit 0。固定服务现为 Running/Manual/LocalSystem，路径 `"C:\Program Files\QuickClash\bin\quickclash_service.exe"`；安装服务哈希 `687EE16D88C35740030DA5B9675640FE96ED7278D0791DE73E79B4AD951138DE` 与 `build/dev/quickclash_service.exe` 一致，GUI 哈希 `6744E19A6F9D9593797E0FB2E6E2073A9F58ABD79DA63D2D20517672B0514502`。受保护目录只有 bin/core/geo，SYSTEM/Administrators 完全控制、Users 只读执行；旧安装保留在 `C:\Program Files\QuickClash_previous_4b9b7d505b0040b5ad76a27eddbb57eb`。两次失败保留：`quickclash-setup-1793384220428.log` 因资源管理器停留在安装目录导致根目录改名连续 20 次拒绝访问；`quickclash-setup-1793384775200.log` 因文件 SDDL 缺对象类型标志报 “Cannot parse the security descriptor”，改为 `D:P(A;;FA;;;SY)(A;;FA;;;BA)` 后经 `ConvertStringSecurityDescriptorToSecurityDescriptorW` 实测通过。helper 成功后原 GUI 进程自动重试，errors.json 末条为 `ServiceTunRouteConflict`；当前 `Meta Tunnel #2` 持有 `0.0.0.0/0`（下一跳 198.18.0.2，metric 0），属于设计中的明确拒绝，QuickClash TUN 未创建。真实 TUN 转发/路由/DNS、关闭 Meta 后开启、取消 UAC、移动/改名后重授权、并发安装与强杀恢复仍 NotRun；GUI 持有服务会话时只读 status 探针返回 ServiceSessionBusy，不作为 SVC-08/09 完整 Pass。

2026-10-30端口/TUN冲突实现：端口错误区分占用、系统拒绝和其他不可用，候选通过现有确认/事务应用；TUN使用受保护稳定名称及会话GUID/LUID，不按Meta、QuickClash或mihomo/clash名称认领第三方。Windows构建/lint及10/10定向测试Pass131.70秒，含双语确认、端口回滚和第三方Meta/清理策略反例；预览消费修正后构建19.006秒及runtime补验112.64秒Pass。原生网络快照检测到当前虚拟默认路由冲突。最终包build/network_conflicts/quickclash_conflicts_final共188项，manifest SHA256=AEAF2891BDFC2240003C6E80772139B43B5BEC6296FFEE47E9BDF4F152CAD9D7；清Qt环境本地导入Ready2.437秒、缺核心修复2.143秒、D3D11/QML错误0及退出核心清理通过。尚未更新正式安装，旧版实际服务证据对此实现Stale；新服务实机集成、实际TUN创建/转发/路由/DNS清理仍NotRun，P3-04及跨用户边界不变。[实施与最新结果](../DEVLOG.md#2026-10-30--端口与tun冲突处理)。

2026-10-30启动/服务入口补验：用户旧会话CoreListenersNotOwned保留为失败证据，诊断曾复现TCP7891绑定AccessDenied及Mihomo mixed-port=0；正式安装更新后原端口也曾Ready，故不将具体系统原因或启动时序臆断为唯一根因。当前应用事务已保存代理7892/控制9091，用户原配置Ready、两端口均为随包核心的IPv4回环监听，产物摘要有效。新版本增加端口预检、有界配置/归属核对和TCP表有限重读；Windows增量构建及7项回归Pass。完整188项包清环境检查通过，已在保留旧安装的前提下更新Program Files固定目录；普通GUI服务身份/核心控制/控制器隔离/断线清理通过，便携GUI明确返回ServiceInstallationRequired。实际TUN转发、路由/DNS及跨用户不在本轮Pass范围，详见[修复记录](../DEVLOG.md#2026-10-30--内核监听与服务安装入口修复)。

早期基线HEAD为1ee26e64f2304e830ab4c7db73aad74ede50bc31及工作区补丁；Windows NT10.0.26300 x64、Qt6.8.0/MSVC19.50、CMake4.3.0、Ninja1.12.1、LLVM21.1.2、yaml-cpp0.9.0。Ninja是当时本机版本，当前CI安装1.13.2。依赖来源/摘要与许可的权威仍为[版本矩阵](../../baseline/release_matrix.json)和[许可台账](../../baseline/licenses.md)，配置范围见[配置策略](../../baseline/config_policy.json)。

- yaml-cpp0.8.0的CMake兼容声明与CMake4冲突，升级上游0.9.0后完整构建通过，无全局策略降级。
- AutoUic误扫描手写ui_state.h，Qt标准设置后关闭AUTOUIC，保留AUTOMOC/QML缓存。MSVC C4702豁免仅限Qt生成的qmlcache单元，自有代码仍/W4、/WX。
- Windows Release、QML cache/lint、7/7 CTest及独立headless1/1通过；headless不依赖Quick/Widgets/Network。完整目录部署、绝对prefix及移除开发Qt PATH后的启动通过，不能当作干净普通用户环境验收。
- 官方Mihomo v1.19.33认证、编码组名/选择读回、三模式、重载及重启选择重置通过；owned PID回环监听、挂起创建→Job绑定→恢复→owner退出回收通过，见[核心汇总](evidence/core_contract.json)。
- 零网络能力AppContainer先确认父进程可连接/子进程1秒内受阻，再用相同官方字节执行正例-t=0/反例=1、无新增文件，清理profile/临时目录；未改系统代理。早期阻塞connect超时，改为非阻塞有界select后通过。控制面不等于代理转发。

早期受限环境挂起及审批服务401没有产生构建证据，后来环境恢复后才取得上述结果。首版按用户要求收窄为Windows，非Windows原生和CI没有因此获得Pass。checkout已迁移为D:/repos/quick-clash；移动仓库后旧CMake/Ninja绝对路径不可复用，应新建构建目录，下载缓存按摘要复核。

## 原型生命周期与资源

当前订阅显示修复以[定向验收](../DEVLOG.md#2026-11-02--仪表盘当前订阅显示修复)为准：下拉绘制所选名称，并在模型晚到、重排和页面重建后同步选中项与详情。Windows构建/lint及11项不同测试通过；旧订阅显示证据对此问题Stale，真实个人配置重启和全局阶段门未因本次验证提升。

当前仪表盘操作精简以[验收记录](../DEVLOG.md#2026-10-31--仪表盘按钮精简)为准：移除收起/显示曲线、查看代理、详细测试及当前订阅重复状态角标，曲线固定显示且忽略旧收起偏好。Windows Qt6.12增量构建/lint、10项现有回归及51张原生D3D11/DPR1.25截图通过；旧按钮/折叠/角标截图Stale，保留既存Qt警告与可访问性/G1-C限制，采样及资源证据沿用未受影响范围。产物位于build/delay_concurrency。

当前流量刻度与页头展示以[精简验收](../DEVLOG.md#2026-10-31--流量图刻度与页头展示精简)为准：上限移至绘图区外纵轴上方，自动KiB/MiB/GiB速率；主题只留设置，重复运行错误文字统一到右上角悬浮/聚焦提示及可访问描述，错误前后PageHost几何不变。独立Windows构建/lint、7项定向回归及51张原生D3D11/125%截图通过；旧叠字/页头主题/错误行截图Stale。复用下述未受影响的采样/资源证据，未重跑服务或长期性能；构建位于build/traffic_scale，保留正在运行的旧构建。

当前流量生命周期以[连续记录验收](../DEVLOG.md#2026-10-31--实时流量跨页与最小化连续记录)为准：窗口显示/最小化期间保留一条约1Hz流和60点历史，切页不重连，不可见不绘图；隐藏或实际卸载才停止并清空。Windows构建/lint、8项定向回归和真实owned核心跨页/最小化/隐藏卸载短测通过，客户端/内核三项内存已记录；旧最小化释放全部需求/清空历史的结论Stale。未重验服务模式或长时CPU/功耗，不关闭下述既存G1-C回放卸载限制。
高低峰连续性补验以[修复记录](../DEVLOG.md#2026-10-31--流量曲线高低峰连续性)为准：样本到达即安排发布，避免独立轮询的额外等待与覆盖风险；折线抗锯齿/圆角、断档后孤立点可见。Windows构建/lint、5项定向回归、极端峰谷逐像素检查及深浅主题原生D3D11截图通过；同机修复前短测未复现漏点，最大样本年龄205ms，修复后16ms、最大间隔1001ms。已曝光窗口10秒100个资源样本，客户端专用工作集/总工作集/私有提交MiB峰77.56/173.22/210.41，内核峰7.38/26.39/60.32；仅为本轮短测，长时/CPU/功耗及服务模式仍未补验。

Qt Quick承载八页面/双主题、路由与独立摘要嵌入，Widgets仅用于托盘菜单。单实例、首次关闭确认、无托盘退路、隐藏释放需求、卸载恢复及键盘检查通过；移动只验接口边界。

2026-10-30补验限制：品牌任务额外运行原生回放--tray-test，首次关闭/隐藏/恢复通过后，实验性卸载阶段出现proxy_summary/theme销毁期QML错误、累计错误导致重建断言exit11；修改前导航包及当前品牌包均同样复现。当前host_lifecycle自动回归仍Pass，历史生产会话Host有限证据保留，但回放卸载整体结论Stale/G1-C重新待验，不能以旧版本或图标替换覆盖本次失败。详见[品牌补验记录](../DEVLOG.md#2026-10-30--灵猫q品牌与侧栏收展)。

2026-11-01设置页原型入口清理已通过Windows构建/lint及六项现有回归；额外ui_prototype在Qt6.12 offscreen/software环境出现ReplayList孵化/销毁失败，同环境修改前后程序均可复现。开发回放保留，G1-C限制不关闭，详情见[设置页验收与对照](../DEVLOG.md#2026-11-01--设置页移除原型数据入口)。

D3D11 threaded下100次重建句柄755→953，basic对照稳定；Windows未指定循环时采用basic，保留GPU。最终部署版100次句柄779→779、窗口QObject133、引擎generation101、QML错误0，同步创建20—29ms，见[生命周期数据](evidence/prototype_lifecycle.json)。不是首帧/可交互时延，外部覆盖渲染循环需重测。

加载整份msyh.ttc额外占约40—50MB专用工作集；直接选择已安装Microsoft YaHei并按系统字体回退后，参考部署版双主题六场景各一次（3秒预热/10秒采样/100ms、500节点/1000连接、DPR1.25）专用峰49,672,192—54,849,536B。[12场景原摘要](evidence/prototype_resources.json)仅证明当时原型。部署exe SHA256=f2019bd7ca1927c9980fd964ac5858db3de37749e016953793e1e3a5b48a27ca。

早期按总工作集/私有提交双指标100MB评定Fail，之后改专用工作集并修复字体；设计1.6移除长时/帧/GPU作为功能前置。当前已取消固定100MB门槛，三指标同时记录、必要辅助客户端计入、内核单列；历史Fail保留当时政策含义，不改Pass。原型通过不关闭[真实字体异常](20261026_subscription_memory_investigation.md)，当前视觉见[产品验收](20261027_unified_visual_implementation.md)。

## 订阅与代理功能

订阅管理支持默认隐藏的完整URL查看/复制，空名导入取最终响应标题/文件名并保留手填/更新名称；Windows构建/lint、11项受影响回归及管理宽度修订4项复验通过，双主题宽窄8张原生D3D11截图核对通过。URL仅显式操作一次性传递，不进列表/快照；全局阶段门与服务/TUN限制维持，详见[验收记录](../DEVLOG.md#2026-11-01--订阅地址查看复制与导入自动命名)。

2026-11-01界面语言设置通过定向范围验收：设置→外观提供跟随系统/English/简体中文，保存成功后持久化并立即刷新当前QML、导航、页面标题、C++派生状态与托盘，不重建QML引擎或网络内核；旧设置缺字段按`system`兼容，非法值进入设置恢复，测试构建的`--probe-language`保持固定覆盖语义。Windows Qt6.12构建/lint、localization/storage/ui_state/ui_language 4/4及qml_smoke/ui_keyboard/ui_visual/ui_responsive 4/4回归、原生语言与独立托盘探针通过。未新增双语截图或资源测量；真实系统语言切换后重启、跨平台原生与长稳NotRun。[验收与限制](../DEVLOG.md#2026-11-01--界面语言设置与即时切换)。

2026-11-01手动测速组上下文修复：组内单节点、组批量和仪表盘嵌套出口请求与显示采用相同目标；队列、活动请求及共享缓存按节点/目标区分，同目标跨组去重并共享进行中/取消，不同目标独立，完成后读取核心出口且不清除固定选择。Windows构建/lint、13项不同定向回归通过，含真实官方v1.19.33组内HTTP目标、批量4组合而成功端点各增1次、两目标并行取消/迟到/重试、完整runtime及服务协议。10秒100样本原生三指标/生命周期通过，真实服务/TUN、大订阅缓存及长稳未验；旧共享状态专项对自动组手动请求缺少覆盖，现由此项补齐。新版输出build/delay_context_bin以避开用户运行中的旧EXE，未终止旧进程。[验收与限制](../DEVLOG.md#2026-11-01--手动测速组上下文与跨组同步修复)。

2026-11-01节点测速样式统一：代理页、仪表盘及规则出口下拉当前值/选项共用DelayLabel，默认12px随全局字号缩放、DemiBold与相同颜色阈值；下拉名称左/延迟右，选中不改变延迟语义色，原始选择值保留。Windows构建/lint、9项现有UI回归26.09秒及59张原生双主题D3D11/DPR1.25截图通过，目视核对代理页、下拉选中态和360宽出口卡；旧下拉整行同色截图对此样式Stale。测量、网络、缓存及生命周期逻辑未改，资源/业务边界沿用上项，不提升整门。[验收与限制](../DEVLOG.md#2026-11-01--节点测速显示样式统一)。

2026-11-01共享代理状态补验：代理页、仪表盘出口及成员下拉统一读取groups/nodes/delay_details，真实官方v1.19.33自动健康检查的成功/失败、自动选择和嵌套出口已核对，读取刷新不额外测速；不同目标按hash隔离，原时间10分钟失效，手动队列/取消及切换清理通过。Windows增量构建/lint与14项不同定向测试通过，最后会话清理修订5项复验通过；原生双主题53张及单轮10秒三指标/隐藏卸载通过。资源最小夹具未填满延迟缓存，真实服务/TUN、大订阅长稳及全局阶段门不在本轮Pass范围；命令、失败修正和原始证据位置见[实施记录](../DEVLOG.md#2026-11-01--全客户端代理状态与内核延迟同步)。此前仅手动测速的展示证据对此范围Stale。

2026-10-24：HEAD3b12f63510afdb1a95e863e7852fb08e06aafef4及工作区补丁、Qt6.8.0/MSVC19.50。P2最终客户端SHA256=075d3f620f1c67e0df47551cec35597481a2cfb7ae052526fb6cf39bb4d21767；增量构建、10/10 CTest（24.20秒）、headless2/2、QML lint退出0，当时Qt Dialogs警告保留。

已运行本地/HTTPS导入→预检/官方隔离校验→选择/owned Ready→节点/模式→系统代理→受控HTTPS转发→停止恢复。仓库单写者、manifest前后恢复、DPAPI拒绝降级、代理租约幂等/迁移/冲突与补偿、坏更新留旧会话、端口失败/崩溃恢复有对应测试。源节点秘密仍在受限ACL原文/运行配置中，不能称整个数据目录密文。

独立WinINet PRECONFIG HTTPS通过本机受控CONNECT代理，退出后原快照精确恢复。[受控断言](evidence/g2_runtime.json)为后续恢复版本记录。回环TLS夹具证明协议出站，不替代公网跨机验收；产品未绕过证书校验或修改系统信任库。

| 后续范围 | 当时真实结果与限制 |
|---|---|
| 响应式/语言/订阅识别（P2-18—20） | 7/7测试19.54秒；英文源/中文资源、窄窗导航、UA/Accept及错误MIME识别通过。4私有样本能下载/识别不表示均可导入；旧视觉已被当前统一视觉系统替代。 |
| 基础协议/组/诊断（P2-21—23） | 7/7测试22.99秒；Hysteria2/Trojan/VLESS/VMess与select/url-test/fallback官方加载、健康检查/能力拒绝通过，未逐协议公网握手。 |
| 连续缩放（P2-24） | 4/4测试4.86秒；默认D3D11原生首页/代理各80步事件p95约14.9ms，不是呈现帧率。修正异步窗口resize空指针和帧等待，offscreen曾未暴露原生卡顿。 |
| Geo/DNS及代表订阅（P3-10） | 4/4测试23.03秒，固定Geo有效/缺失/损坏检查通过。S3第二候选VLESS全局HTTPS200、1MiB、正常TLS、所选节点链无DIRECT，见[匿名结果](evidence/subscription_e2e.json)/[Geo检查](evidence/geo_integrity.json)。首个Hysteria2未成功，不能推断原因/全节点可用。 |
| 计划更新/自动应用（P3-11） | 7/7测试42.43秒、原生WinINet44.39秒通过；内容不变不重启、有变应用、候选补偿、取消不复活、退避和意图持久化覆盖。公开/私有样本不代表全部订阅。 |
| 受管提供者生命周期（P3-19，2026-10-29工作区） | 快照单资源刷新及显式运行时use/RULE-SET热刷新、坏更新缓存保留/停止迟到、动态选择不恢复与导入fallback备份恢复完成。Windows最终3/3（runtime108.62秒/provider19.35秒）、双语界面5/5、headless5/5及10秒worker三指标短测Pass；固定服务命令白名单已测，真实安装服务/TUN未测，无新发行包。[范围与证据](../DEVLOG.md#2026-10-28--p3提供者生命周期) |

初轮失败曾涉及YAML反例引号、资源manifest schemaVersion、VLESS空flow过严及探针采样时机，修正后才获得对应结果，未将失败尝试计Pass。实际支持边界以配置策略和设计为准。

## 恢复与本地包

2026-10-25：P2-15关键恢复完成，recovery_test --native-system-proxy退出0（17.45秒）、runtime_test --native-system-proxy退出0（80.89秒）。首次端口/平台/落盘失败及外部冲突使用注入适配器；真实worker强杀后Job/端口回收、下次租约/未提交事务恢复使用真实WinINet。停止可打断退避，20次模拟网络事件合并1次，退出忽略迟到通知。GUI整体强杀、真实换网/休眠未测。

首轮6/7测试通过，恢复失败来自父夹具读后未关QFile，阻止子进程原子替换；关闭后通过。SDK头顺序错误修复后重建，产品存储规则未放宽。

本地验证包dist/g2_recovery共186文件，exe SHA256=9b26165aeebf4c913f2de75d73ad9ef133b04d00b3038357f8dba0c467ad5fac；依赖/摘要及清开发PATH启动通过。BUILD_TESTING开启、未签名、未安装服务/TUN，不是正式发行或干净环境证明。

10秒91样本Ready首页、公开DNS/Geo、无持续流量/系统代理修改：客户端专用/总工作集/私有提交峰63,504,384/160,784,384/209,506,304B，内核49,025,024/70,217,728/106,930,176B；正常退出且owned核心清理。[原摘要](evidence/g2_memory.json)不证明导入峰值、持续运行或当前版本。

2026-10-29当前本机包补证：Windows增量构建/lint及contracts/smoke/host_lifecycle 3/3 Pass7.01秒（p3_20261029T152255）。完整包位于build/P3 日常验证/QuickClash 离线包，manifest187文件SHA256=B65A1E57D824B5ED966EFB878FA1ABFC4E4569320A2C5A1319BF212B684DE6BA，GUI SHA256=FA7A2DD305930EE1594167E32D89C2437944737D02528BDF0DD7364F7B7B0AA5。逐文件尺寸/摘要、官方冻结核心、GUI/核心/Qt/CRT的x64、QML/platform/Schannel及说明核对通过。包缺核心时不再回退开发路径，CoreIntegrityFailed保留修复UI。

tests/scripts/desktop_package_test.ps1 -Phase Package在普通Medium令牌、仅System32/Windows PATH、清Qt导入/插件/渲染环境下通过（build/p3_package_probe_1）：本地中文空格文件/数据路径导入→生产SettingsService/官方核心Ready 2.451秒，正常退出owned核心消失；故意不带core的副本1.980秒给出CoreIntegrityFailed/PID0，端口设置和订阅/设置/日志/首页路由可用。默认D3D11 graphicsApi=4、真实托盘、QML错误0、系统代理/TUN关闭。P2-16 Done、MVP-01 Pass；该包仍BUILD_TESTING=ON/未签名，本机清PATH不能替代P3-14干净机器或真实TLS网络链路。

P3-14当前包Host阶段也通过（build/p3_host_probe_1）：真实托盘隐藏五秒卸载、需求/流清零、内核PID2844不变；隐藏前/隐藏后/重建后三次显式HTTP代理入口到回环夹具转发成功。QML代次1→2，路由/筛选/选择代次保持。整体强杀GUI29376（非KillTree）后核心9700消失，同一数据目录及SingleInstance重开到Ready为核心29688，选择ID/代次/两个端口保持，正常退出核心消失。系统代理/TUN关闭；本轮不证明GUI强杀后的代理租约恢复。真实OS事件/干净机器仍NotRun。

## G2本机MVP逐项验收

2026-10-29，P2-17按设计19/1.9逐条件核对。Windows开发机、Qt6.8/MSVC x64 Release、官方冻结Mihomo v1.19.33；本轮GUI为BUILD_TESTING=ON、未签名的本地验证包。包位于build/P3 日常验证/QuickClash 主链路包4，GUI SHA256=05994D2D385A5E4FEECC635663FC2B67FA38054BA4C983B22CAB5FDA0CAC0FDE，manifest187项/SHA256=EC55AB4A48910EA4D824AEC35A195B9AB1A28574913E31D5D06414C35EA4F2F9。版本不同的证据只复用于未改变的实现范围；本轮新增探针和QML定位名不改变业务路径。

证据索引（原始日志、JSON、JUnit均在Git忽略的build/）：F=build/g2_flow_4；K=build/g2_package_final；N=build/g2_native_recovery；R=build/ci_qt680/logs/p3_20261029T154903；T=build/ci_qt680/logs/p3_20261029T145519；U=build/ci_qt680/logs/p3_20261029T145205。R增量构建/lint退出0，storage/contracts/proxy_lease/runtime_provider/runtime_subscription_download/recovery为6/6 Pass73.12秒；T完整runtime112.46秒Pass，U的14/14回归62.08秒Pass。早期冻结核心契约evidence/core_contract.json仅证明未变化的认证/owned监听/Job契约，不充当当前整GUI证据。

| 条件 | 结果 | 逐条件证据与范围 |
|---|---|---|
| MVP-01 | Pass | K核对187项尺寸/hash、官方核心/x64/Qt/QML/Schannel/CRT/固定资源和随包修复说明；Medium令牌、清Qt环境、PATH仅Windows/System32，中文空格包/本地文件/存储路径导入Ready并正常退出清理。缺core副本保留修复UI，未借开发核心。清环境不等于干净机器。 |
| MVP-02 | Pass | F从真实订阅页HTTPS Direct下载完整YAML→官方校验→Ready；第二本地导入保留选择与PID。T的HTML/Base64/空坏更新保留确认配置；R下载专项30.53秒核对证书名称不匹配及16MiB+1拒绝后sourceHash/PID/Ready不变，停止/删除后迟到不复活。 |
| MVP-03 | Pass | F真实单实例/worker/官方核心，当前卡重复选择及非活动导入不重启；节点/模式读回与转发确认Ready。冻结核心契约覆盖错secret 401、owned PID回环监听/Job；T端口占用补偿未误认第三方Ready。模式事务按EngineSession先停旧会话再启动，PID允许改变；F退出前后owned清理，非全操作并发压力。 |
| MVP-04 | Pass | F真实代理页分组/成员可见，键盘选择fixture并从官方核心读回；首页Direct→Rule确认，模式事务后选择保留。T及既有协议交付覆盖声明字段/官方加载、url-test/fallback能力/健康状态与失败不伪成功；不宣称全部远端协议握手。 |
| MVP-05 | Pass | F首页Ready后开启真实WinINet代理；独立WinINet PRECONFIG客户端经本地入口请求受控HTTPS，新增上游CONNECT和站点GET /system-client；关闭后完整原快照精确一致。非仅读取注册表或显式指定代理。 |
| MVP-06 | Pass | F独立Qt客户端显式本地代理请求fixture.quickclash.invalid HTTPS，站点收到GET /gui-proof；上游记录域名CONNECT，排除DIRECT。退出重开两种客户端再次通过。公开测试CA仅作用于进程/该请求，未改OS信任。 |
| MVP-07 | Pass | F正常关闭/退出还原拥有的原代理并回收核心；N真实worker强杀/下次恢复原WinINet。R租约冲突不覆盖；build/p3_proxy_probe_1独立进程实际外部变更/显式新基线/原快照还原，T坏更新保留与端口补偿、K缺core端口修复通过。 |
| MVP-08 | Pass | F真实Ready首页/错误投影、默认D3D11，10秒93样本客户端三指标及内核单列，见下表。同轮首末及峰有界；当前卡/导入幂等、异步worker路径、正常退出清理核对。运行会话隐藏重建另有P3-14证据；不据短测关闭字体驻留或长期性能调查。 |
| MVP-09 | Pass | F两配置、选择ID/代次、mode/端口/dark及可靠节点退出重开一致，存储/租约核对后后台Ready；首次网络开关关闭。T及R覆盖确认/选择分离、外部冲突保留；R提供者23.61秒补同名端点变化及已选成员删除不恢复旧选择，contracts核对凭据变更身份失效。 |
| MVP-10 | Pass | T重命名/来源URL与显式头替换/手动更新/活动及最后配置删除；R下载专项覆盖停止/删除迟到拒绝和来源绑定校验器。R recovery16.42秒新增释放代理失败时不删活动配置、选择ID/代次保留；T运行更新/候选端口失败补偿旧会话。 |
| MVP-11 | Pass | T及R contracts逐项预检声明节点/组/基础规则并经官方加载，未知/危险字段、路径和不支持项明确拒绝且诊断脱敏；P2协议与P3-10固定Geo/内部DNS证据有效，P3-08/19/20另补实际DNS/受管资源。无隐式任意下载或静默删规则；不扩大为全协议公网矩阵。 |
| MVP-12 | Pass | R/N recovery首次配置/端口/平台/代理写后落盘失败、后台仅平台失败保留Ready且Operation失败；T运行端口/配置失败补偿；R storage核对manifest前中断保留旧权威、提交后未终结日志以新manifest为准。无空lastGood回退或错误确认。 |
| MVP-13 | Pass | T连续四次核心强杀后台恢复，R停止打断退避/退出不再核对；N recovery_test --native-system-proxy真实独立生产runtime worker强杀、Job回收核心/端口、同目录下次租约及未提交事务恢复，16.702秒退出0且精确还原原设置。退避序列1/2/4/8/16/30秒上限由实现核对；不确定/第三方冲突拒绝自动覆盖，手工恢复说明随包。 |
| MVP-14 | Pass | R proxy_lease核对重复开启保留最初before、监听迁移、PAC/自动发现/绕过恢复、外部冲突及重新申请后的新before；F及N原生恢复，P3-14实际外部变更补充独立进程差异证据。 |
| MVP-15 | Pass | Windows接口/IP/Qt连通性/介质/恢复通知及QObject析构取消已核对；U/R recovery受控20→1、旧测试即时Stale、PID不变、退出后请求不增加。T离线保留监听/编辑及有界重试/无自动IP查询；真实换网或唤醒归P3-14。 |
| MVP-16 | Pass | K无core仍有错误/端口/订阅选择/日志入口；R租约释放、storage坏/过新元数据只读且拒空值覆盖、DPAPI密文/坏blob显式失败；contracts的URL/查询/Bearer/JSON/空格凭据/私钥/UUID日志脱敏、诊断allowlist和预览保护。T自设HTTPS目标经当前入口测试、失败无直连回退；不把默认诊断与确认后原文导出混同。 |

F首程17.390秒、真实进程重开1.868秒，均exit_code=0/timed_out=false、QML错误0、真实托盘。GUI21740初始核心24100，模式事务后核心35780；两配置/端口12272和12273/模式rule/主题dark/节点fixture保持。显式与PRECONFIG客户端各产生一次上游CONNECT及站点请求，重开再通过两次；所有GUI退出后owned核心消失。测试只写隔离root，系统代理完整还原，未启动TUN或替换安装服务。K两次GUI也exit0，原生D3D11。

Ready首页三项单位为字节，顺序为专用工作集/总工作集/私有提交。93个样本覆盖0—9907ms，单轮总时长约10秒：

| 进程 | 起始 | 末次 | 样本峰值 |
|---|---|---|---|
| GUI | 76165120 / 180264960 / 228990976 | 72622080 / 176795648 / 223068160 | 81432576 / 185552896 / 234348544 |
| 官方核心 | 8032256 / 28913664 / 63758336 | 8261632 / 29147136 / 64012288 | 8261632 / 29147136 / 64012288 |

无参与该会话的产品辅助进程；独立fixture服务器及请求后已退出的测试客户端属于测试基础设施。已有安装服务空闲、未参与会话。上述同轮起止用于当前场景基线，不与旧g2_memory不同版本/夹具直接作优化差值，不代表导入峰、长期、CPU/帧或全部GPU兼容。字体驻留专项仍Review。

MVP为16/16 Pass，G2-A—E证据齐全，P2-17 Done；**G2整门仍NotRun**，因为G1 Stale，当前P3-27名称审计Fail且实际OS文本/高DPI多屏/读屏仍待补验。P3-14/25仍Review；P3-04已有实现和历史有限实测，2026-11-02已纠正为Blocked，等待P3-25前置与当前便携服务TUN验收，不能再记为尚未启动。G3不因本表完成而提升。

## P3普通用户宿主补证

2026-10-29最终本地验证包：build/P3 日常验证/QuickClash 最终包，GUI SHA256=E55FE2E0332858C26214130532A196365C1BFC17B02DE6EF882C229B78704CCF，manifest187项/SHA256=77F3D2F42E1E1B4D49DCE765DEB0C3EB492E68B083EC8C3D91E87D46C890A8D0。当前Windows开发机、Medium普通令牌、原生D3D11/真实托盘，PATH仅System32/Windows且清Qt插件/导入/渲染环境；BUILD_TESTING=ON、未签名，不是公开发行包。最终探针增量构建/lint及single_instance/smoke 2/2 Pass0.69秒（p3_20261029T152832）；代理/恢复3/3 Pass15.77秒（p3_20261029T152717）。

| P3-14条件 | 结果 | 当前证据及边界 |
|---|---|---|
| 完整包/代表路径/基本修复 | Pass（本机） | p3_package_verified：187项全尺寸/hash/x64，官方核心/CRT/Qt/QML/Schannel与说明齐备；中文空格本地文件/存储/包路径导入Ready5.750秒，退出核心清理；缺core副本1.850秒错误CoreIntegrityFailed/PID0，端口修改及订阅/设置/日志/首页可用。P2-16/MVP-01证据更新到最终包。 |
| 无Qt/开发工具的普通用户干净机器 | NotRun | 本机有Qt/VS，清环境不等于干净环境；尚无另一干净机器或隔离OS的实际运行证据。 |
| 真实换网或唤醒 | NotRun | 已接通知及受控合并/PID/旧测试失效Pass；未实际切换网络或睡眠唤醒，不以模拟消息或手动reconcile代替。 |
| 运行会话隐藏/重建 | Pass | p3_host_verified/lifecycle 8.396秒：真实托盘隐藏五秒卸载、QML代次1→2、需求/流清零；内核PID36960不变，选择/代次/路由/筛选保持；隐藏前/卸载后/重建后三次HTTP入口转发到回环夹具成功。仅验证宿主保持本地转发，非远端节点/TLS重验。 |
| 整GUI强杀/下次恢复 | Pass（无代理分支） | p3_host_verified：强杀GUI38888本身，owned核心30452由Job回收；同一目录使用真实SingleInstance重开1.148秒Ready为核心34012，选择ID/代次及端口6866/6867保持。正常退出核心消失；系统代理/TUN关闭，原生租约恢复沿用生产worker既有证据，不称GUI租约强杀分支已测。 |
| 实际外部代理变更 | Pass | p3_proxy_probe_1/proxy 5.924秒：独立进程实际写WinINet绕过列表，保留可用端点；GUI手动reconcile后保留外部值/提示冲突、PID39556保持；释放不覆盖，显式重新开启后关闭恢复新外部基线。最后完整原快照核对还原，租约Released、owned核心退出；无注入适配器，不打印原个人快照。 |

2026-10-30准备独立工具包build/P3 日常验证/QuickClash 验收工具包，完整复制G2主链路包4（原manifest SHA256=EC55AB4A48910EA4D824AEC35A195B9AB1A28574913E31D5D06414C35EA4F2F9），加必要脚本/公开TLS夹具/说明；搬运清单194文件尺寸/hash复核通过，无需目标机器源码仓库、Qt SDK或VS，仍需PowerShell7.2+。工具包自身目录运行Package/Flow均退出0：导入2.647秒、缺核心修复2.166秒（build/validation_kit_package）；GUI主链路17.449秒、同目录重开1.940秒（build/validation_kit_flow），两种独立客户端HTTPS、原代理精确还原、QML错误0/D3D11/托盘及owned核心清理通过。原生步骤均未超时，已有/源包内目标两种拒绝通过；Host/Proxy沿用上表历史有限证据，本轮未重跑。用户暂无额外环境，此项只证明工具可脱离源码目录运行及本机功能，不能将干净机器或OS事件行改Pass。

P3-14 Review，当前环境仍缺两项，G3不得Pass。GUI强杀不承诺立即恢复系统代理，服务恢复能力仍NotAvailable；跨用户SVC-05、真实TUN、实际高DPI/多屏/读屏及G2汇总各按原任务验收。原始JSON/日志与测试目录只保留build/，安装服务未替换。本轮产品改动仅部署核心解析；未改变生产页面资源生命周期，沿用已有短测的有限结论，不增加无关资源/协议矩阵。

## 系统代理冲突修复

2026-10-26：显式setSystemProxy(true)遇conflict且非active时以当前外部快照重建before，再申请租约；损坏租约不自动接管，仍持有active不走该路径，后台恢复不变。用户主动开关即明确重新开启意图。

cmake --build build/dev --target proxy_lease_test recovery_test及两项测试退出0（0.42/22.73秒），覆盖开关重建基线/释放恢复新快照。该次全量CTest退出8，provider_stream_test既有错误导致16项NotRun，另有UI环境失败；后续CI修复单列，不改写此轮。原生PRECONFIG/GUI主链路未在此修复重测，MVP-05维持当前Stale。

## 服务连接回收与普通服务恢复

2026-10-31当前受信根服务定向验收Pass：未取得owner的连接3秒绝对期限与16KiB握手预算，超限55ms断开、受信/非受信四连接约3秒全部回收，正常owner仍存活；首个start大于16KiB合法配置成功。实测发现并修复客户端析构时socket信号访问已销毁成员，最终Windows Qt6.12.0/MSVC增量构建及7/7回归Pass29.83秒。

真实普通服务客户端/owned核心/SCM服务分别强杀后旧核心回收、查询clean且可重新启动；服务暂停17秒后首请求ServiceTimeout、后续同会话同PID Ready且lost=false。重启后身份拒绝、协议重放、控制器/配置隔离、固定命令和同用户Busy通过，10秒三指标采样完整。原生UAC更新成功，已安装服务与构建SHA256一致，最终服务Running且无测试核心残留。详见[命令、摘要和证据](../DEVLOG.md#2026-10-31--服务连接回收与普通服务恢复验收)。

用户暂不关闭第三方Meta TUN，因此本轮仅tun=false，Meta适配器保持原状；真实TUN故障、网络清理、连续短时失败上限及跨用户仍NotRun，P3-25/28/29保持Review。SVC-01/03/04/06/07定向证据对当前服务有效，SVC-02旧清单身份矩阵仍Stale，不用本轮更新成功替代完整受信根安装矩阵。

## 跨平台静态边界

2026-10-31接口补验：Unsupported服务客户端补齐共享层已调用的hadSession()，固定返回false；Windows Qt6.12.0/MSVC构建该真实后端并直接调用接口的契约测试1/1 Pass。见[修复记录](../DEVLOG.md#2026-10-31--非windows服务会话接口补齐)；该结果不能代替Linux/macOS原生完整程序链接或系统验收。

2026-10-29，P3-13：Windows Qt6.8.0/MSVC工作区完成静态核对和Windows回归。该结论不代表Linux/macOS原生构建或真实系统能力通过；移动端VPN、后台与触摸均未验。[开发记录](../DEVLOG.md#2026-10-29--p3跨平台静态兼容)保留失败与原始日志位置。

| 核对项 | 修正与有限结论 |
|---|---|
| 共享层和公开头 | DesktopRuntime改持ICredentialStore/ISystemProxyLease/IDesktopResources/IDesktopProcess，infra/desktop_runtime_factory负责组装；EngineSession公开头仅持进程职责接口。共享业务及infra公开头不再包含Windows凭据/代理/进程实现或SDK。 |
| CMake/系统库 | storage/platform/engine按WIN32选择Windows或Unsupported源，服务/pipe_http仅Windows构建，SDK库PRIVATE。Windows核心专项、代理租约、恢复和publication仅Windows注册；手动平台CI执行其余已注册共享/UI/Unsupported测试。headless保持Core/Network，未引入Quick/Qml/Gui/Widgets。 |
| 路径/身份/能力 | 核心文件名和冻结摘要由平台描述提供，应用使用Qt相对路径及逻辑资源标识。PID/创建时间仅留桌面诊断和所有权核验；凭据与代理快照格式由适配器持有。未实现平台明确Unsupported，缺失内存为未知；不创建明文替代、不启动进程、不写代理或打开服务IPC。 |
| 导航/翻译/生命周期 | 八功能经注册表投影及独立嵌入工厂，不引用侧栏/底栏/StackView。页面及托盘按能力禁用代理/TUN，英文源/中文原因齐备。unique_ptr单一所有权、QObject回调context及generation保留；Unsupported命令排队报错，在close/销毁后取消。 |
| 防回退检查 | check_platform_boundaries已接Windows仓库CI，检查共享include/infra公开头/功能壳边界；4类违规反例拒绝、职责接口正例通过。选源、链接、路径和生命周期仍需人工核对，该脚本不等于全平台分析器。 |

Windows最终主程序/服务增量构建和qmllint退出0，保留既有Overlay/模型/QtQuick.Dialogs静态警告。14/14定向回归62.08秒Pass（p3_20261029T145205），覆盖storage、contracts、proxy_lease、recovery、Unsupported、publication、localization、UiState、service_protocol、smoke、键盘、中英文可访问性及host_lifecycle；最终依赖组装下真实冻结Mihomo主链路1/1 Pass112.46秒（p3_20261029T145519）。首轮recovery因未发起网站测试却断言旧结果Stale而Fail，补本地待完成请求后保留原合并/冲突/清理断言通过。递归目录副作用断言补强后Unsupported独立复验Pass0.09秒。

Windows headless增量构建/5项测试Pass6.51秒（build/ci_headless/logs/p3_platform_20261029T150655），不代替Linux/macOS原生构建。一次10.171秒worker采样（p3_20261029T150931）专用工作集/总工作集/私有提交为2134016/18337792/3563520→2224128/18833408/3657728字节；重复需求共享采样、撤销后5.1秒不再发布，再申请可恢复，代理写入0。无GUI/核心/辅助进程，本轮未计入独立空闲安装服务；仅核对本次采样适配生命周期，不是完整客户端或长期资源结论。

未运行：Linux/macOS原生工具链、Keychain/Secret Service、各平台受管核心/代理/服务/采样系统验收及移动宿主。非Windows各能力仍Unsupported，须P4逐项实现/验收。此次不改变P3-25/27 Review、G1 Stale、G2/G3 NotRun或MVP0/16，不更新安装包或已安装服务。

## 阶段范围与待补项

G2汇总Windows本机订阅→真实转发→安全停止恢复的16项必要条件，一条有效运行可支撑多项。设计1.9将干净普通用户包、一次真实换网/唤醒、一次GUI整体强杀/恢复、运行中隐藏/重建及一次实际外部代理变更交给P3-14/G3。错误处理/所有权/秘密保护和代表性反例仍必需，范围调整不产生Pass。

P2-11/16/17已完成当前本机证据汇总，MVP Pass=16/16，见[G2逐项验收](#g2本机mvp逐项验收)。真实GUI订阅/节点/模式/系统代理/两种独立客户端HTTPS转发及退出重开通过，定向反例和单场景资源记录齐全；G1仍Stale，因此G2/G3整门保持NotRun。干净机器、实际OS环境及TUN不由本机最小闭环代替。

G3目标为Windows日常使用。P3-25已在本机完成UAC安装、普通GUI固定IPC/控制器隔离、启动重放和客户端/服务强杀清理；跨用户争用未验，任务Review，代理恢复能力NotAvailable，详见[权限服务验收](../DEVLOG.md#2026-10-29--p3权限服务)。真实TUN、干净普通用户环境与必要GUI宿主恢复仍待验。P3-13共享静态兼容核对通过，范围见[静态边界](#跨平台静态边界)；Linux/macOS原生/系统集成归P4，CI编译成功不表示受管内核启动已实现，移动仅远期规划。当前规则见[构建说明](../../baseline/build_and_package.md)，发行见[CI与Release核验](20261027_github_release.md)。

设计1.12的任务映射补齐：配置编辑/草稿冲突/完整回退由P3-17承接，下载路径/重定向/条件缓存由P3-18承接；原大任务拆分至P3-19—26，字体/DPI/键盘及G1补验由P3-27承接。provider导入展开与运行时内核刷新仍须分别验收；本次仅复核计划，未新增产品证据或改变上述阶段结果。具体范围见[任务台账](../TASKS.md)及[复核记录](../DEVLOG.md#2026-10-28--p3任务范围与依赖复核)。
