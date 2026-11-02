# 开发任务台账

本文件只维护当前任务、负责人、依赖、证据和下一步，不记录逐次开发或修改流水。过程记录见[开发日志](DEVLOG.md)，详细验证见[报告索引](reports/README.md)，验收状态见[清单](CHECKLISTS.md)。

## 专项状态

优化优先级不复用仓库阶段任务ID，也不计入下表阶段任务数。

| 专项 | 实现状态 | 负责人 | 产品验证 | 证据/下一步 |
|---|---|---|---|---|
| 仪表盘当前订阅显示修复 | Done | Codex | Pass（Windows定向范围） | [记录](DEVLOG.md#2026-11-02--仪表盘当前订阅显示修复)：修复下拉绘制及页面重建/模型更新时的选择和详情同步；Windows增量构建/lint、11项不同定向测试通过，真实个人配置重启未单测 |
| 界面语言设置与即时切换 | Done | Codex | Pass（界面语言定向范围） | [验收记录](DEVLOG.md#2026-11-01--界面语言设置与即时切换)：设置→外观新增跟随系统/English/简体中文，保存后持久化并即时刷新QML、导航、页面标题、C++派生状态和托盘，不重建QML引擎或网络内核；旧设置缺字段兼容`system`，非法值进入恢复。Windows构建/lint、语言/存储/状态4项与现有UI4项回归、原生语言及托盘探针通过；新版`build/language_settings_bin`。系统语言切换后重启及跨平台NotRun |
| 设置页移除原型数据入口 | Done | Codex | Pass（设置页定向范围） | [验收记录](DEVLOG.md#2026-11-01--设置页移除原型数据入口)：移除回放卡片及三条专用翻译，保留开发回放与既有测试；Windows增量构建/lint及6项现有回归通过，ui_prototype修改前后同样失败，保留G1-C限制；新版build/settings_cleanup_bin，旧含原型卡片截图Stale |
| 复选框主题统一 | Done | Codex | Pass（样式定向范围） | [验收记录](DEVLOG.md#2026-11-01--复选框主题统一)：设置页两个登录选项复用ThemedCheckBox，保留滑动开关及原有状态/命令绑定，窄窗标签可换行；Windows增量构建/lint及4项现有UI回归通过，新版build/dense_pages_bin；旧登录选项默认样式截图Stale |
| 连接规则日志高密度表格 | Done | Codex | Pass（Windows定向范围） | [验收记录](DEVLOG.md#2026-11-01--连接规则日志高密度表格)：连接新增链路/下载/上传列，整行点击详情且行尾不设重复查看按钮；规则保序表格，整行展开、无查看列，提供者保留更新；日志分列与行内展开；共享虚拟表格固定表头、窄窗水平滚动。Windows构建/lint、8项定向回归与60张原生双主题/宽窄截图通过；旧三页布局截图Stale，双语可访问性探针仍停在未修改的订阅URL输入框，三页读屏NotRun |
| 订阅地址查看复制与导入自动命名 | Done | Codex | Pass（Windows定向范围） | [验收记录](DEVLOG.md#2026-11-01--订阅地址查看复制与导入自动命名)：默认隐藏、按需显示/复制完整URL及迟到隔离；空名取最终响应标题/文件名，手填与更新名称优先。Windows构建/lint、11项回归、布局修订4项复验及8张原生宽窄双主题截图通过；新版build/subscription_details_bin |
| 手动测速组上下文与跨组同步修复 | Done | Codex | Pass（Windows定向范围） | [验收记录](DEVLOG.md#2026-11-01--手动测速组上下文与跨组同步修复)：组内及嵌套出口使用组目标，同节点/同目标跨组共享并去重，不同目标排队/取消隔离；Windows构建/lint、13项不同回归和10秒三指标短测通过。新版build/delay_context_bin；真实服务/TUN、大订阅与长稳未验 |
| 节点测速显示样式统一 | Done | Codex | Pass（显示范围） | [验收记录](DEVLOG.md#2026-11-01--节点测速显示样式统一)：代理页、仪表盘及规则出口下拉当前值/选项共用DelayLabel；名称左、延迟右，选中不覆盖延迟颜色。Windows构建/lint、9项现有UI回归与59张原生双主题/宽窄截图通过；业务与长稳边界沿用共享状态专项 |
| 全客户端代理状态与内核延迟同步 | Done | Codex | Pass（Windows定向范围） | [验收记录](DEVLOG.md#2026-11-01--全客户端代理状态与内核延迟同步)：共享内核/手动延迟及组链/目标投影，代理页、仪表盘与出口下拉一致；Windows构建/lint、14项不同回归及会话修订5项复验、最终53张原生截图和10秒三指标短测通过。真实服务/TUN、大订阅缓存资源与长稳NotRun |
| 登录自启与受管更新 | Done | Codex | Pass（Windows定向范围） | [实施记录](DEVLOG.md#2026-11-01--登录自启与受管更新)：当前用户登录注册/隐藏、Geo同提交双库及计划下载后确认应用、官方内核兼容清单下载/校验/切换/失败回退完成；Windows构建/lint、13项回归、清单修订4项复验、原生宿主/视觉及10秒三指标通过。当前清单仅v1.19.33；真实注销重启、TUN在线更新、公网产品下载及跨版本NotRun，在线签名清单后置P4 |
| 仪表盘按钮精简 | Done | Codex | Pass（展示范围） | [验收记录](DEVLOG.md#2026-10-31--仪表盘按钮精简)：移除收起/显示曲线、查看代理、详细测试及订阅重复状态角标，曲线固定显示并忽略旧收起偏好；Windows增量构建/lint、10项现有回归与51张原生截图通过，既存Qt警告与整门限制保留 |
| 流量图刻度与页头展示精简 | Done | Codex | Pass | [验收记录](DEVLOG.md#2026-10-31--流量图刻度与页头展示精简)：上限移至纵轴上方、自动KiB/MiB/GiB速率；主题仅在设置、运行错误统一页头提示；独立Windows构建/lint、7项回归和51张原生截图通过，错误前后PageHost几何不变 |
| 流量曲线高低峰连续性 | Done | Codex | Pass | [验收记录](DEVLOG.md#2026-10-31--流量曲线高低峰连续性)：到达即发布、抗锯齿/圆角及孤立点显示；Windows构建/lint、5项回归、极端峰谷像素检查和双主题D3D11截图通过；真实内核短测最大样本年龄205→16ms，60点及隐藏释放保持 |
| 实时流量跨页与最小化连续记录 | Done | Codex | Pass（定向范围） | [验收记录](DEVLOG.md#2026-10-31--实时流量跨页与最小化连续记录)：显示/最小化共用约1Hz流和60点双曲线，隐藏/卸载释放；Windows构建/lint、8项定向回归及真实owned核心跨页/最小化/隐藏卸载与10秒三指标短测通过。旧切页停流/最小化清空证据Stale；服务模式与长期CPU/功耗未验 |
| 仪表盘视觉层级与图标 | Done | Codex | Pass（视觉定向范围） | [交付记录](DEVLOG.md#2026-10-31--仪表盘视觉层级与图标)：统一图标标题、关键值/说明分层、延迟/结果配色和三站离线图标；Windows构建/lint、localization及8项UI通过，45张原生截图与10秒三指标短测完成。全页双语可访问性Fail于未修改的订阅URL密码输入框名称，已单列；既存8条Qt颜色警告保留，不提升整门 |
| 仪表盘运行概况紧凑卡 | Done | Codex | Pass（定向范围） | [修订记录](DEVLOG.md#2026-10-31--运行概况内核信息与详情去重)：内核名称/版本与内存、时长并列，800/480断点四/两/单列，移除重复的QuickClash进程详情；Windows构建/lint、4项受影响CTest和39张原生截图通过。旧布局截图Stale，既有采样/生命周期与10秒短测证据保留；既存Qt警告及长稳/服务模式限制不变 |
| 仪表盘五卡与网络工具 | Done | Codex | Pass（定向范围） | [验收记录](DEVLOG.md#2026-10-31--仪表盘五卡与网络工具)：五卡响应式布局、订阅操作/真实流量累计、手动IP双路径/双地址族和三站测试、本地显示偏好完成；Windows构建/lint、12项定向测试、双主题31张原生截图及真实Ready首页10秒三指标短测通过。原连接与系统布局/折叠不采样证据Stale，由运行概况专项替代。公网IPv6在当前环境TLS失败；Qt6.12其他页面既存颜色警告保留，不提升G1/G2/G3整门 |
| 代理页节点滚动性能修复 | Done | Codex | Pass（短测范围） | [验收记录](DEVLOG.md#2026-10-31--代理页节点滚动性能修复)：节点网格由滚动时整体销毁/重建改为固定行池重映射，并移除逐节点浮层；Qt6.8 D3D11/1000×640/DPR1.25、500节点×3组6秒单组CPU 5203→1406ms、中位帧132.1→15.2ms，三组CPU 5016→2172ms、中位帧82.4→15.5ms；18秒滚动中位15.4/17.8ms，控件33个且离页释放。GPU利用率未单采，不代表G5长稳/字体专项通过 |
| 代理页测速并发与超时调优 | Done | Codex | Pass（定向范围） | [实施记录](DEVLOG.md#2026-10-31--代理页测速并发与超时调优)：服务模式32并发、直连6并发、内核探测2秒、服务IPC delay 5秒、请求在途上限40；独立构建目录增量构建及runtime/service_protocol定向测试通过，整套4项环境/既有失败单列。真实服务32并发、真实节点质量分级与长时资源未验 |
| 开发目录主界面启动修复 | Done | Codex | Pass（启动范围） | [修复验收](DEVLOG.md#2026-10-31--开发目录主界面启动修复)：Windows构建自动部署QML/插件及动态DLL、相对qt6.conf；清Qt环境4/4回归、普通主界面/托盘隐藏后重开、独立部署启动与服务status通过；G1-C旧回放问题仍保留 |
| ActionButton启动加载契约修复 | Done | Codex | Pass（启动范围） | [修复验收](DEVLOG.md#2026-11-02--actionbutton启动加载契约修复)：移除Qt6.9 IconLabel不存在的defaultIconColor赋值，修复普通启动QML整页加载失败而仅留托盘；Windows增量构建、正常启动MainWindowHandle/标题核对、隔离诊断Visible/page_ready/qml_errors=0及qml_smoke/host_lifecycle/ui_prototype 3/3通过。G1-C原生tray卸载及整门状态不变 |
| TUN便携目录持久服务替换 | Done | Codex | Pass（普通服务范围） | [替换与验收](DEVLOG.md#2026-10-31--tun便携目录持久服务替换)：旧保护目录迁移、普通权限启动/更新、共享Geo与坏摘要拒绝、移动重授权/失败回退/卸载、普通服务三类强杀与17秒暂停恢复通过；Windows构建、10项回归及最后启动过渡修订的8项回归、普通服务探针与静态检查通过；真实TUN/系统重启与跨用户未验 |
| 服务连接回收与非Windows接口补齐 | Done | Codex | Pass（普通服务范围） | [验收记录](DEVLOG.md#2026-10-31--服务连接回收与普通服务恢复验收)：3秒未拥有连接回收/16KiB握手预算、Unsupported hadSession及客户端析构修复；Windows构建/7项回归、当前受信根服务的连接回收、客户端/内核/服务强杀、17秒暂停后的单请求超时恢复通过。用户暂不关闭第三方TUN，真实TUN故障注入及Linux/macOS原生仍NotRun |
| 端口冲突与TUN名称/路由保护 | Done | Codex | Pass（定向范围） | [实施记录](DEVLOG.md#2026-10-30--端口与tun冲突处理)；Windows构建/lint、10项回归及最终runtime补验通过，端口候选确认、第三方Meta反例和GUID/LUID清理策略完成，188项完整包清环境核验通过；正式安装未更新，实际服务/TUN创建转发与清理NotRun，仍归P3-04/25 |
| 内核启动监听与服务安装入口修复 | Done | Codex | Pass | [修复记录](DEVLOG.md#2026-10-30--内核监听与服务安装入口修复)；Windows构建/7项回归、完整包和实际受保护更新通过，个人配置事务保存7892/9091并Ready，普通GUI服务握手/核心控制通过，便携包明确提示安装入口；实际TUN路由/DNS仍归P3-04，跨用户归P3-25 |
| 灵猫Q品牌与侧栏收展 | Done | Codex | Pass | [实施记录](DEVLOG.md#2026-10-30--灵猫q品牌与侧栏收展)；程序/窗口/托盘同源品牌、独立收展及EXE九档图标资源完成；Windows构建/lint、7项回归及居中修正4项、原生截图和188项完整包核对通过。额外tray回放卸载失败在修改前后均可复现，单列G1-C Stale，不冒充完整生命周期Pass |
| 导航圆角线性图标替换 | Done | Codex | Pass | [实施记录](DEVLOG.md#2026-10-30--导航圆角线性图标)；八个本地SVG统一侧栏/图标轨/底栏，Windows增量构建/lint、6/6回归、独立部署与原生双主题/宽窄截图通过；单轮10秒资源采样完成 |
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
| P0-08 | P0 | W03 | 领域类型/Operation/接口与无 UI 夹具测试 | Done | Codex | P0-03, P0-04, P0-06 | [当前验证](reports/windows_validation.md#工程与内核)、[DNS依赖回归](DEVLOG.md#2026-10-28--p3内部dns) | 领域层仅Core；headless配置校验现用Network地址类型，无Quick/Qml/Gui/Widgets，5项回归Pass |
| P0-09 | P0 | W02, W18 | 构建/部署/内核获取与 P2 离线包方案 | Done | Codex | P0-02, P0-06 | [当前验证](reports/windows_validation.md#工程与内核) | 构建/部署脚本执行通过；脱离开发 Qt PATH 启动通过，普通用户完整产品包仍在 P2-16 |
| P1-01 | P1 | W04 | 单实例/托盘/窗口宿主 | Done | Codex | P0-06, P0-08 | [当前验证](reports/windows_validation.md#工程与内核) | IPC、无托盘关闭退路、真实 Windows 托盘关闭确认/隐藏/恢复探针通过 |
| P1-02 | P1 | W05 | 模块注册/导航投影/路由与嵌入边界 | Done | Codex | P0-05, P0-08, P1-01 | [当前验证](reports/windows_validation.md#工程与内核) | 八页面/双主题自动加载、路由/嵌入/导航投影通过；移动只验接口边界 |
| P1-03 | P1 | W05 | 双主题与代表页面回放基础 | Done | Codex | P1-02 | [原型功能证据](reports/windows_validation.md#原型生命周期与资源)、[当前视觉基准](reports/20261027_unified_visual_implementation.md#设计基准) | 原型功能与资源证据按版本保留；视觉结论Stale，产品视觉迁移/双主题与实际字体验收归P3-16 |
| P1-04 | P1 | W04, W05 | 显示/隐藏/卸载重建与需求释放 | Done | Codex | P1-01, P1-02 | [当前验证](reports/windows_validation.md#原型生命周期与资源)、[流量修订](DEVLOG.md#2026-10-31--实时流量跨页与最小化连续记录) | 当前最小化只保留轻量流量，隐藏/卸载全部释放；定向宿主与真实核心通过。旧100次句柄779→779、QObject133为历史证据，G1-C回放卸载限制仍保留 |
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
| P2-11 | P2 | W17 | Windows通知接入与有界合并核对 | Done | Codex | P2-07, P2-09 | [条件核对](DEVLOG.md#2026-10-29--windows通知与离线包补证) | 接口/IP/连通性/唤醒接入及生命周期已核对；当前recovery的20→1/PID不变/即时Stale/退出忽略和runtime离线节点/监听、有界请求证据有效；真实OS事件由P3-14补验 |
| P2-12 | P2 | W20 | 可配置单目标 HTTPS 测试 | Done | Codex | P2-06, P2-09 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 自设目标DPAPI保存、当前入口HEAD/12秒/并发1、拒redirect/无直连回退 |
| P2-13 | P2 | W20 | 受控远端代理/HTTPS 端到端夹具 | Done | Codex | P2-05, P2-06, P2-07 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 独立GET经owned入口/域名CONNECT到受控TLS服务，远端记录断言通过；本机夹具范围 |
| P2-14 | P2 | W13, W20 | 系统代理端到端/重复租约/端口迁移 | Done | Codex | P2-08, P2-09, P2-13 | [P2交付](reports/windows_validation.md#订阅与代理功能) | 真实PRECONFIG HTTPS/重复开启/迁移及原快照恢复通过，PAC/冲突另有注入适配器证据 |
| P2-15 | P2 | W07, W08, W09, W11, W13 | 首次失败/事务中断/核心崩溃/强杀恢复 | Done | Codex | P2-07, P2-09, P2-10, P2-14 | [G2恢复开发](reports/windows_validation.md#恢复与本地包) | 首次关键失败/停止打断/worker强杀→Job及真实WinINet下次租约/事务恢复、manifest前后、连续四次受管恢复及系统客户端回归通过；非全故障矩阵，真实OS事件独立归P2-11 |
| P2-16 | P2 | W18 | Windows本机完整离线包与依赖/启动核对 | Done | Codex | P0-09, P2-03, P2-04, P2-07, P2-10, P2-12, P2-14 | [当前包验收](reports/windows_validation.md#恢复与本地包) | 当前完整包187文件hash/x64/CRT/官方核心/Qt/QML/Schannel齐备；清开发Qt环境后中文空格路径导入Ready及缺核心修复入口Pass，说明随包；干净机器归P3-14 |
| P2-17 | P2 | W06, W19, W20 | G2本机16项MVP证据与单场景资源汇总 | Done | Codex | P2-11, P2-12, P2-13, P2-15, P2-16, P2-21, P2-22, P2-23, P1-05 | [16项逐条件验收](reports/windows_validation.md#g2本机mvp逐项验收) | 真实GUI订阅/节点/模式/系统代理、两种独立客户端HTTPS及重开通过；6/6定向回归、原生worker强杀恢复、最终包及10秒三指标完成，MVP16/16 Pass。G1 Stale仍约束G2整门NotRun；继续P3环境项/TUN前置 |
| P2-18 | P2 | W05 | 窄窗竖屏布局与导航投影 | Done | Codex | P2-06 | [响应式行为](reports/windows_validation.md#订阅与代理功能)、[缩放修复](reports/windows_validation.md#订阅与代理功能) | 360×480、600断点和路由/状态保留已有技术证据；当前四入口与统一视觉由P3-16统一验收 |
| P2-19 | P2 | W05, W15 | 英文源文案/中文翻译/系统与显式语言选择 | Done | Codex | P2-18 | [语言设置验收](DEVLOG.md#2026-11-01--界面语言设置与即时切换)、[历史基础](reports/windows_validation.md#订阅与代理功能) | Qt词表内嵌及系统首选序/英文回退；设置页可持久化选择跟随系统/English/简体中文，保存后即时刷新QML、导航、派生状态与托盘且不重建引擎。Windows构建/lint、语言/存储/状态4项和现有UI4项回归、原生语言及托盘探针通过；系统语言切换重启、跨平台NotRun |
| P2-20 | P2 | W11 | 标准HTTPS订阅链接协商/内容识别/错误说明 | Done | Codex | P2-07 | [本轮交付](reports/windows_validation.md#订阅与代理功能) | 默认UA/Accept、头覆盖/空白、误标MIME与坏更新保留通过；4个私有样本仅识别，配置能力待扩展 |
| P2-21 | P2 | W09, W10 | Hysteria2/Trojan/VLESS/VMess基础协议与常用TLS/Reality传输 | Done | Codex | P2-20 | [协议交付](reports/windows_validation.md#订阅与代理功能) | 逐协议/嵌套字段、官方隔离-t/Ready/选择重启、身份与脱敏通过；未测新增协议公网握手 |
| P2-22 | P2 | W09, W10 | url-test/fallback组与能力化展示 | Done | Codex | P2-21 | [协议交付](reports/windows_validation.md#订阅与代理功能) | Selector/URLTest/Fallback能力映射、健康检查跳过REJECT、嵌套/引用保护与界面通过 |
| P2-23 | P2 | W09, W12 | 协议/组/规则/DNS具体不兼容原因与中英文提示 | Done | Codex | P2-22, P2-19 | [协议交付](reports/windows_validation.md#订阅与代理功能) | 最多32项安全结构诊断、七类不兼容与字段错误、中英文词表187条完成 |
| P2-24 | P2 | W05 | 缩放跨导航断点时消除D3D11秒级等待并稳定壳布局 | Done | Codex | P2-18 | [缩放修复](reports/windows_validation.md#订阅与代理功能) | 默认basic/D3D11关闭帧延迟等待对象；4/4检查与真实窗口160步通过，页面/需求保留，最终包Ready首页专用工作集60.75MB；完整帧/转发组合未测 |
| P2-25 | P2 | W09, W10 | 自动组离线/嵌套出口与新鲜度补全 | Done | Codex | P2-22 | [开发与验收](DEVLOG.md#2026-10-31--代理自动组离线嵌套与新鲜度补全) | 离线url-test/fallback current为空，automatic/sources投影、父Selector嵌套出口递归、组读取失败stale保留及refresh_groups、等待/来源/刷新UI完成；Windows增量构建与定向测试Pass。真实provider远端刷新、健康检查故障转移及GUI目视NotRun |
| P3-01 | P3 | W10 | 代理组独立展开、节点选择与延迟测试 | Done | Codex（接续Zoo） | P2-06, P2-21, P2-22, P3-10, P3-16 | [开发与验收](DEVLOG.md#2026-10-28--p3代理与测速) | 空展开恢复、500节点视口/键盘定位、最新选择读回、设置目标权威、排队/取消/过期完成；Windows构建/lint与定向测试Pass，字体/缓存增长仍按专项跟踪；动态provider身份归P3-19；滚动性能与测速并发调优见专项状态 |
| P3-02 | P3 | W11 | 订阅用量/到期/告警与计划策略补证 | Done | Codex | P2-07, P3-11 | [开发与验收](DEVLOG.md#2026-10-28--p3订阅用量与告警) | 严格解析/未知合计/采集时间、90%/耗尽及7天/过期告警完成；Windows构建/lint和15项定向测试Pass，含重开/同内容更新/调度合并与中英文UI；资源归P3-19/20 |
| P3-03 | P3 | W12 | 连接快照/详情/筛选与单条/批量关闭 | Done | Codex | P2-03, P2-06, P3-16 | [开发与验收](DEVLOG.md#2026-10-28--p3连接详情与关闭) | 完整有界快照/稳定详情/结束保留、时间/超限暂停重试及关闭计数完成；Windows构建/lint与最终6项定向测试Pass，真实关闭保留新连接，默认GPU1000连接短测及离页释放Pass；日志与规则分别P3-21/22 |
| P3-04 | P3 | W14 | Windows TUN设置/会话迁移/真实转发与清理 | Blocked | Codex | P3-25, P3-08 | [实现与证据复核](DEVLOG.md#2026-11-02--p3实现与台账复核)、[历史TUN实测](DEVLOG.md#2026-10-31--服务会话超时与服务预检收口) | 开关/服务预检、串行迁移、失败补偿及GUID/LUID清理保护已有实现，历史服务已有用户开关/HTTP204/关后普通模式记录，不是未开始；P3-25身份与跨用户验收未关闭，便携服务替换后真实TUN证据仍NotRun。待前置关闭并补授权拒绝、迁移失败/Uncertain、非显式代理的受控节点转发及路由/DNS清理；不凭旧服务证据标Done |
| P3-05 | P3 | W15 | 设置分类/生效状态与破坏性操作交互 | Done | Codex | P2-01, P2-02, P2-10, P2-12, P3-16 | [开发与验收](DEVLOG.md#2026-10-28--p3设置生效状态) | 请求/保存/实际生效独立投影、网络多项合并及捕获代次确认完成；真实内核事务与失败回退Pass，重开状态及最终4项UI回归Pass；备份P3-23、诊断P3-24、P4更新独立 |
| P3-06 | P3 | W20 | 有限网站测试/显式路径与可选IP边界 | Done | Codex | P2-06, P2-12, P2-13, P3-16 | [开发与验收](DEVLOG.md#2026-10-28--p3网站测试) | 三路径/逐跳系统解析、3跳HTTPS安全链/整体12秒及64KiB、Cookie隔离、实际错误/选择上下文与历史失效完成；Windows构建/lint、13项回归及最终runtime100.84秒Pass；IP未配置隐藏，资源采样P3-26 |
| P3-07 | P3 | W06, W19 | G3 Windows日常使用交付与必要验收汇总 | Todo | 未分配 | P2-17, P3-01, P3-02, P3-03, P3-04, P3-05, P3-06, P3-08, P3-09, P3-12, P3-13, P3-14, P3-16, P3-17, P3-18, P3-19, P3-20, P3-21, P3-22, P3-23, P3-24, P3-25, P3-26, P3-27, P3-29 | — | 逐项汇总G3-A—E、F02编辑/回退及SUB/CFG/NET/RES等条件，恢复G1有效结论并完成G2；保留NotRun/Stale事实，复用有效证据，只补功能差异/必要短测，不扩大其他平台矩阵 |
| P3-08 | P3 | W09, W15 | fake-ip/redir-host DNS、解析/过滤/缓存与回环防护 | Done | Codex | P2-23, P3-10 | [开发与验收](DEVLOG.md#2026-10-28--p3内部dns) | 合法fake-ip子网/规范化回环/bootstrap及策略字段完成；官方受控解析/filter/policy、缓存命中/清理重查和模式覆盖Pass，Windows6项及headless5项Pass；外部DNS与P3-04联合验收 |
| P3-09 | P3 | W09, W10 | TUIC及已声明传输字段/load-balance策略核对 | Done | Codex | P2-21, P2-22, P2-23 | [开发与验收](DEVLOG.md#2026-10-28--p3协议与负载均衡) | 字段/认证互斥、TUIC原生UDP及负载均衡无单一当前成员完成；官方v4/v5和三策略加载/能力/拒手选，Windows4项及headless5项Pass；不代表公网全协议握手 |
| P3-10 | P3 | W09, W10, W11, W20 | 代表订阅的受管Geo/最小DNS兼容及真实节点HTTPS链路 | Done | Codex | P2-07, P2-21, P2-22, P2-23 | [订阅闭环](reports/windows_validation.md#订阅与代理功能) | S3导入56节点/3组，VLESS全局HTTPS200/1MiB/TLS及所选节点链通过；Geo缺失/损坏拒绝、Windows包及单轮短测通过；完整资源P3-19/20及DNS P3-08继续开发 |
| P3-11 | P3 | W10, W11, W18, W20 | Windows日常订阅调度/自动应用/节点筛选与可用包 | Done | Codex | P3-10, P2-08, P2-09 | [日常链路](reports/windows_validation.md#订阅与代理功能) | 定时更新/退避/自动应用/补偿与筛选完成；WinINet转发/精确恢复及私有56节点VLESS HTTPS/更新后转发通过；dist/p3_daily，完整G3仍待后续 |
| P3-12 | P3 | W02, W03, W19 | G3 跨平台定位、三/五平台路线与验收边界 | Done | Codex | — | [方案调整](reports/windows_validation.md#阶段范围与待补项) | 设计1.8与任务/清单同步；仅文档方案交付，未改变G2/G3或产品兼容结论 |
| P3-13 | P3 | W02, W03, W16 | 跨平台静态兼容核对与Windows耦合收敛 | Done | Codex | P3-12 | [静态边界验收](reports/windows_validation.md#跨平台静态边界) | 职责接口/infra组装、按平台选源/私有SDK库及明确Unsupported完成；路径/导航/翻译/取消核对，Windows14项回归及最终真实runtime、headless5项与10秒采样Pass；Linux/macOS原生与移动系统能力NotRun归P4 |
| P3-14 | P3 | W04, W13, W17, W18 | Windows普通用户包与有限OS/GUI宿主集成补证 | Review | Codex | P1-04, P2-11, P2-15, P2-16 | [宿主补证](reports/windows_validation.md#p3普通用户宿主补证) | 当前包普通权限/中文空格路径/缺核心修复、真实GUI运行隐藏重建及转发、整体强杀/Job清理/同目录重开、独立进程实际WinINet冲突/新基线释放/原快照还原Pass；独立验收工具包Package/Flow本机Pass。用户暂无额外环境，干净机器及真实换网或唤醒仍NotRun |
| P3-15 | P3 | W01, W05 | 统一视觉规范与双主题离线概念预览 | Done | Codex | — | [设计与预览验证](reports/20261027_unified_visual_implementation.md#设计基准) | 规范/预览/旧参考清理及浏览器、对比度、生成/一致性、进度/命名/diff检查通过；产品迁移见P3-16 |
| P3-16 | P3 | W05 | 共享组件、八页面与双主题产品迁移 | Done | Codex | P3-15 | [实施与验证](reports/20261027_unified_visual_implementation.md) | Windows构建、10项测试及末次布局3项回归通过；本机125%DPI原生GPU的26张界面/弹层/错误态截图核对，含360/800/1000/1200宽；60点流量、预览符号/文字及右上运行状态完成；固定MB门槛已移除，完整功能/高DPI矩阵按原任务继续 |
| P3-17 | P3 | W09, W11 | 配置原文/覆盖编辑、草稿冲突与版本回退 | Done | Codex | P2-05, P2-07, P2-23, P3-16 | [开发与验收](DEVLOG.md#2026-10-28--p3配置编辑) | 原文/覆盖分离、持久草稿冲突/副本/脱敏比较、历史选择/摘要及失败补偿完成；Windows构建/lint、8项回归、真实runtime107.41秒及最终编辑专项11.17秒、headless5项Pass；24秒GPU文本释放通过但Qt缓存峰值仍高，未关闭内存专项 |
| P3-18 | P3 | W11 | 订阅下载路径/安全重定向与条件缓存 | Done | Codex | P2-07, P3-11 | [开发与验收](DEVLOG.md#2026-10-28--p3订阅下载兼容) | 三路径持久化/实际路径、安全5跳、认证/Cookie隔离、ETag/Last-Modified与304损坏/缺失恢复完成；Windows构建/lint、真实runtime及最终下载/双语UI回归Pass，整链截止/超限/停止与删除迟到通过；worker短测有三指标，G3仍待汇总 |
| P3-19 | P3 | W09, W11 | 受管proxy/rule-provider与RULE-SET生命周期 | Done | Codex | P2-23, P3-10 | [开发与验收](DEVLOG.md#2026-10-28--p3提供者生命周期) | 快照单资源缓存/摘要与更新、运行时use/RULE-SET热刷新、独立缓存/停止GC、固定服务命令白名单、动态身份不恢复及备份fallback完成；Windows构建/lint、完整runtime/专项/双语UI、headless5项及10秒短测Pass。真实安装服务/TUN归P3-25；按用户要求停止，不启动后续任务 |
| P3-20 | P3 | W09, W11 | Geo手动更新/不可变快照切换与回退 | Done | Codex | P3-10 | [开发与验收](DEVLOG.md#2026-10-28--p3受管geo更新) | 同摘要严格复用、双库探测修复、下载/校验取消、随包历史及离线恢复完成；Windows构建/lint与最终4项测试Pass，含候选启动失败补偿；受控真实冻结资源验证不代表公网可用性或TUN支持 |
| P3-21 | P3 | W12 | Core/App/Service日志、暂停/跟随与脱敏导出 | Done | Codex | P2-10, P3-16 | [开发与验收](DEVLOG.md#2026-10-28--p3日志与导出) | 2000条/4MiB及服务中转双预算、截断/缺口、100ms发布、三来源和冻结隐私预览完成；Windows主程序/服务构建及8项回归、最终4项UI/筛选和headless5项Pass，GPU2000行短测离页释放Pass；真实安装服务联验归P3-25 |
| P3-22 | P3 | W12 | 有序规则/来源与提供者查看/刷新页面 | Done | Codex | P3-19, P3-16 | [开发与验收](DEVLOG.md#2026-10-29--p3规则与提供者页面) | 有效序号/来源及有限信息、RULE-SET数量/时间、快照/运行时区分、逐项结果/串行刷新、分类失败保留及覆盖编辑入口完成；Windows构建/lint、11项回归及完整runtime、最后5项复测和10秒释放短测Pass；真实安装服务/TUN与G3另行验收 |
| P3-23 | P3 | W09, W15 | 敏感本地配置备份/预检与恢复为新配置 | Done | Codex | P2-02, P3-17 | [开发与验收](DEVLOG.md#2026-10-28--p3配置备份恢复) | 原文/覆盖与离线展开两范围、固定预检副本/令牌、官方整批校验及取消清理完成；Windows构建/lint、5项定向与完整runtime127.56秒Pass，10秒预览/取消短测有界；明文敏感备份及10MiB导出上限已说明 |
| P3-24 | P3 | W07, W15 | 脱敏支持包/无内核修复与存储保留配额 | Done | Codex | P2-01, P2-10 | [开发与验收](DEVLOG.md#2026-10-29--p3支持包与存储修复) | 固定字段支持包冻结预览/秘密反例、只读归档修复/中断保护/失败回退、引用安全软配额与测速凭据保留完成；Windows构建/lint及最终9项回归/真实runtime Pass，10秒162轮预览取消有界；归档不降级新版schema、不清理保护数据 |
| P3-25 | P3 | W14 | Windows权限服务安装/身份/固定IPC与控制器隔离 | Review | Codex | P2-03, P2-05, P2-08, P2-09 | [最新普通服务验收](DEVLOG.md#2026-10-31--服务连接回收与普通服务恢复验收)、[原验收](DEVLOG.md#2026-10-29--p3权限服务) | 当前受信根服务UAC更新、身份拒绝/连接回收、协议重放、控制器/配置拒读、客户端/内核/服务强杀清理与重新启动Pass；17秒暂停后单请求超时保留会话/PID。SVC-02旧清单身份证据Stale，安装身份矩阵转P3-28；跨用户SVC-05仍NotRun，同用户Busy不替代；SYS-05明确NotAvailable。真实TUN归P3-04，在线升级归P4 |
| P3-26 | P3 | W20 | 系统/客户端进程组与内核资源分层采样 | Done | Codex | P2-03, P3-16 | [开发与验收](DEVLOG.md#2026-10-28--p3资源采样) | owned创建时间/SCM身份、三指标分层聚合/校验核心单列、失败清旧值/部分状态与共享采样释放完成；Windows构建/lint、7项回归、真实内核106.00秒及headless5项Pass，10秒短测通过；真实安装服务联验归P3-25 |
| P3-27 | P3 | W05, W19 | Windows字体/DPI/键盘可访问性与G1补验 | Review | Codex | P3-16 | [当前复核](DEVLOG.md#2026-11-02--p3实现与台账复核)、[G1补验](reports/20261027_unified_visual_implementation.md#p3字体与可访问性补验) | 文本缩放/名称声明/键盘/默认取消/焦点返回及字体重建已有实现；当前Qt6.12产物ui_keyboard等回归Pass，但中英文ui_accessibility均Fail于subscription_url密码输入框名称为空，旧名称审计Pass对当前版本Stale。实际OS文本设置、高DPI/多屏迁移与读屏NotRun；ui_prototype/host_lifecycle本轮Pass，原生tray卸载仍待核对，G1不提升 |
| P3-28 | P3 | W14, W18 | 权限服务受信客户端根身份与一次性安装入口 | Review | Codex | — | [替换与验收](DEVLOG.md#2026-10-31--tun便携目录持久服务替换)、[历史入口](DEVLOG.md#2026-10-31--原生服务安装入口) | 首次UAC直接注册便携目录服务并持续信任其更新；不复制服务/内核/Geo到Program Files。自动启动，普通GUI可启停已注册服务，同目录update免UAC。旧注册迁移/旧载荷清理、移动后拒绝及重新授权、根外拒绝、坏镜像注册回退、卸载后文件保留已通过普通服务实测；共享官方核心和选定Geo、坏摘要拒绝通过。完整根/链接/权限矩阵、取消UAC与并发安装仍未验；新方案真实TUN、系统重启恢复及SVC-11 NotRun，P3-04范围不提升 |
| P3-29 | P3 | W14 | Windows TUN瞬断误判与短时失败恢复上限 | Review | Codex | — | [修复记录](DEVLOG.md#2026-10-31--tun瞬断误判与恢复上限) | 复用P3-28已交付的服务会话与退出诊断通道，不依赖其跨用户/受信根验收。修复服务每次status把ready但tun=false、请求超时或无对象当作内核退出并主动停核的问题：现仅ready=false判定退出，状态失败记Unknown，TUN Missing/Unknown独立观测并连续3次防抖；连续3次短时（<60s）TUN失败后暂停自动重开TUN、保留普通代理并报TunRecoveryExhausted，用户显式TUN操作重置；真实退出码/UTC时间/脱敏2KiB stderr写入应用日志。补充：会话超时只约束连接与握手，单请求超时不关闭已验证会话或停核；握手返回serviceBuild摘要，与随包服务载荷不一致时请求一次受控update；每次显式开启TUN都执行服务预检，持久化tunDesired不跳过；清理不确定只阻止服务模式，未建立会话的退出不产生Uncertain；TUN 开关在操作期间保持目标位置并显示正在启动/正在关闭，用户实测双向无回弹。Windows构建通过（quickclash及core_health/service_protocol/engine_startup/ui_state/runtime_test，服务二进制未变）；用户本人多次开关真实服务TUN，核心随开关预期重建，未出现CoreExited/ServiceTimeout；验证时TUN开启，适配器QuickClash-a880b33f（198.18.0.1）、默认路由metric 0、DNS 198.18.0.2，经TUN的HTTP探测返回204；关闭后适配器移除并由普通内核接管；连续短时失败后的TunRecoveryExhausted与TUN-04/TUN-05完整验收仍NotRun；2026-10-31[普通服务实测](DEVLOG.md#2026-10-31--服务连接回收与普通服务恢复验收)已通过三类进程强杀及17秒暂停后的超时保留会话/PID，真实TUN故障仍按用户选择NotRun |
| P4-01 | P4 | W16, W18 | Linux代理/托盘/服务与平台发行集成 | Todo | 未分配 | P3-07 | — | P4-06中Linux原生基线就绪后逐项实现，记录GNOME/KDE/X11/Wayland及安装/更新范围；不等待macOS完成 |
| P4-02 | P4 | W16, W18 | macOS菜单栏/代理/helper与平台发行集成 | Todo | 未分配 | P3-07 | — | P4-06中macOS原生基线就绪后逐项实现，记录多网络服务/签名/公证/授权与安装更新；不等待Linux完成 |
| P4-03 | P4 | W17 | 三平台网络/电源矩阵 | Todo | 未分配 | P4-01, P4-02 | — | 运行休眠、换网、时钟/会话变化用例 |
| P4-04 | P4 | W18 | Windows正式安装器/签名/应用与内核更新恢复 | Todo | 未分配 | P3-07 | — | Windows 版本完成来源、防重放、schema与独立回退；不依赖Linux/macOS，其他平台发行集成归P4-01/02 |
| P4-05 | P4 | W06, W19 | G4 三平台 Beta/功能与资源矩阵 | Todo | 未分配 | P4-03, P4-04, P4-06 | — | 正式支持范围与各平台真实负载证据齐全 |
| P4-06 | P4 | W02, W16 | Linux/macOS 原生空壳与多平台 CI 实际运行 | Todo | 未分配 | P0-07 | [当前验证](reports/windows_validation.md#工程与内核) | 从 P0-07 后置；按冻结工具链原生构建/启动、无 UI/QML 测试与 CI run 留证，不阻塞当前 G3 里程碑 |
| P5-01 | P5 | W19 | 代表性持续运行/关键故障与升级回归 | Todo | 未分配 | P4-05 | — | 按发布风险选取持续运行与资源抽查；24/72小时、全组合、帧/GPU和压力专项不统一强制，出现问题再扩展 |
| P5-02 | P5 | W19 | 候选发布/许可源码/用户说明与 G5 | Todo | 未分配 | P5-01 | [许可实施记录](DEVLOG.md#2026-11-01--gplv3-许可与发布包) | 项目已采用GPL-3.0-only并加入根LICENSE；签名/来源、Qt/Mihomo/Geo完整对应源码、恢复说明与最终G5仍待P5-01 |
