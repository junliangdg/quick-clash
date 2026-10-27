# Windows 阶段验证摘要

本页合并2026-10-23—25的工程、原型、P2/P3功能与恢复证据，只证明各自版本的已测范围；当前状态以[任务](../TASKS.md)和[清单](../CHECKLISTS.md)为准，不从历史Pass推断当前产品通过。日常修改不再生成report。

## 工程与内核

早期基线HEAD为1ee26e64f2304e830ab4c7db73aad74ede50bc31及工作区补丁；Windows NT10.0.26300 x64、Qt6.8.0/MSVC19.50、CMake4.3.0、Ninja1.12.1、LLVM21.1.2、yaml-cpp0.9.0。Ninja是当时本机版本，当前CI安装1.13.2。依赖来源/摘要与许可的权威仍为[版本矩阵](../../baseline/release_matrix.json)和[许可台账](../../baseline/licenses.md)，配置范围见[配置策略](../../baseline/config_policy.json)。

- yaml-cpp0.8.0的CMake兼容声明与CMake4冲突，升级上游0.9.0后完整构建通过，无全局策略降级。
- AutoUic误扫描手写ui_state.h，Qt标准设置后关闭AUTOUIC，保留AUTOMOC/QML缓存。MSVC C4702豁免仅限Qt生成的qmlcache单元，自有代码仍/W4、/WX。
- Windows Release、QML cache/lint、7/7 CTest及独立headless1/1通过；headless不依赖Quick/Widgets/Network。完整目录部署、绝对prefix及移除开发Qt PATH后的启动通过，不能当作干净普通用户环境验收。
- 官方Mihomo v1.19.33认证、编码组名/选择读回、三模式、重载及重启选择重置通过；owned PID回环监听、挂起创建→Job绑定→恢复→owner退出回收通过，见[核心汇总](evidence/core_contract.json)。
- 零网络能力AppContainer先确认父进程可连接/子进程1秒内受阻，再用相同官方字节执行正例-t=0/反例=1、无新增文件，清理profile/临时目录；未改系统代理。早期阻塞connect超时，改为非阻塞有界select后通过。控制面不等于代理转发。

早期受限环境挂起及审批服务401没有产生构建证据，后来环境恢复后才取得上述结果。首版按用户要求收窄为Windows，非Windows原生和CI没有因此获得Pass。checkout已迁移为D:/repos/quick-clash；移动仓库后旧CMake/Ninja绝对路径不可复用，应新建构建目录，下载缓存按摘要复核。

## 原型生命周期与资源

Qt Quick承载八页面/双主题、路由与独立摘要嵌入，Widgets仅用于托盘菜单。单实例、首次关闭确认、无托盘退路、隐藏释放需求、卸载恢复及键盘检查通过；移动只验接口边界。

D3D11 threaded下100次重建句柄755→953，basic对照稳定；Windows未指定循环时采用basic，保留GPU。最终部署版100次句柄779→779、窗口QObject133、引擎generation101、QML错误0，同步创建20—29ms，见[生命周期数据](evidence/prototype_lifecycle.json)。不是首帧/可交互时延，外部覆盖渲染循环需重测。

加载整份msyh.ttc额外占约40—50MB专用工作集；直接选择已安装Microsoft YaHei并按系统字体回退后，参考部署版双主题六场景各一次（3秒预热/10秒采样/100ms、500节点/1000连接、DPR1.25）专用峰49,672,192—54,849,536B。[12场景原摘要](evidence/prototype_resources.json)仅证明当时原型。部署exe SHA256=f2019bd7ca1927c9980fd964ac5858db3de37749e016953793e1e3a5b48a27ca。

早期按总工作集/私有提交双指标100MB评定Fail，之后改专用工作集并修复字体；设计1.6移除长时/帧/GPU作为功能前置。当前已取消固定100MB门槛，三指标同时记录、必要辅助客户端计入、内核单列；历史Fail保留当时政策含义，不改Pass。原型通过不关闭[真实字体异常](20261026_subscription_memory_investigation.md)，当前视觉见[产品验收](20261027_unified_visual_implementation.md)。

## 订阅与代理功能

2026-10-24：HEAD3b12f63510afdb1a95e863e7852fb08e06aafef4及工作区补丁、Qt6.8.0/MSVC19.50。P2最终客户端SHA256=075d3f620f1c67e0df47551cec35597481a2cfb7ae052526fb6cf39bb4d21767；增量构建、10/10 CTest（24.20秒）、headless2/2、QML lint退出0，当时Qt Dialogs警告保留。

已运行本地/HTTPS导入→预检/官方隔离校验→选择/owned Ready→节点/模式→系统代理→受控HTTPS转发→停止恢复。仓库单写者、manifest前后恢复、DPAPI拒绝降级、代理租约幂等/迁移/冲突与补偿、坏更新留旧会话、端口失败/崩溃恢复有对应测试。源节点秘密仍在受限ACL原文/运行配置中，不能称整个数据目录密文。

独立WinINet PRECONFIG HTTPS通过本机受控CONNECT代理，退出后原快照精确恢复。[受控断言](evidence/g2_runtime.json)为后续恢复版本记录。回环TLS夹具证明协议出站，不替代公网跨机验收；产品未绕过证书校验或修改系统信任库。

| 后续范围 | 当时真实结果与限制 |
|---|---|
| 响应式/语言/订阅识别（P2-18—20） | 7/7测试19.54秒；英文源/中文资源、窄窗导航、UA/Accept及错误MIME识别通过。4私有样本能下载/识别不表示均可导入；旧视觉已被方案C替代。 |
| 基础协议/组/诊断（P2-21—23） | 7/7测试22.99秒；Hysteria2/Trojan/VLESS/VMess与select/url-test/fallback官方加载、健康检查/能力拒绝通过，未逐协议公网握手。 |
| 连续缩放（P2-24） | 4/4测试4.86秒；默认D3D11原生首页/代理各80步事件p95约14.9ms，不是呈现帧率。修正异步窗口resize空指针和帧等待，offscreen曾未暴露原生卡顿。 |
| Geo/DNS及代表订阅（P3-10） | 4/4测试23.03秒，固定Geo有效/缺失/损坏检查通过。S3第二候选VLESS全局HTTPS200、1MiB、正常TLS、所选节点链无DIRECT，见[匿名结果](evidence/subscription_e2e.json)/[Geo检查](evidence/geo_integrity.json)。首个Hysteria2未成功，不能推断原因/全节点可用。 |
| 计划更新/自动应用（P3-11） | 7/7测试42.43秒、原生WinINet44.39秒通过；内容不变不重启、有变应用、候选补偿、取消不复活、退避和意图持久化覆盖。公开/私有样本不代表全部订阅。 |

初轮失败曾涉及YAML反例引号、资源manifest schemaVersion、VLESS空flow过严及探针采样时机，修正后才获得对应结果，未将失败尝试计Pass。实际支持边界以配置策略和设计为准。

## 恢复与本地包

2026-10-25：P2-15关键恢复完成，recovery_test --native-system-proxy退出0（17.45秒）、runtime_test --native-system-proxy退出0（80.89秒）。首次端口/平台/落盘失败及外部冲突使用注入适配器；真实worker强杀后Job/端口回收、下次租约/未提交事务恢复使用真实WinINet。停止可打断退避，20次模拟网络事件合并1次，退出忽略迟到通知。GUI整体强杀、真实换网/休眠未测。

首轮6/7测试通过，恢复失败来自父夹具读后未关QFile，阻止子进程原子替换；关闭后通过。SDK头顺序错误修复后重建，产品存储规则未放宽。

本地验证包dist/g2_recovery共186文件，exe SHA256=9b26165aeebf4c913f2de75d73ad9ef133b04d00b3038357f8dba0c467ad5fac；依赖/摘要及清开发PATH启动通过。BUILD_TESTING开启、未签名、未安装服务/TUN，不是正式发行或干净环境证明。

10秒91样本Ready首页、公开DNS/Geo、无持续流量/系统代理修改：客户端专用/总工作集/私有提交峰63,504,384/160,784,384/209,506,304B，内核49,025,024/70,217,728/106,930,176B；正常退出且owned核心清理。[原摘要](evidence/g2_memory.json)不证明导入峰值、持续运行或当前版本。

## 系统代理冲突修复

2026-10-26：显式setSystemProxy(true)遇conflict且非active时以当前外部快照重建before，再申请租约；损坏租约不自动接管，仍持有active不走该路径，后台恢复不变。用户主动开关即明确重新开启意图。

cmake --build build/dev --target proxy_lease_test recovery_test及两项测试退出0（0.42/22.73秒），覆盖开关重建基线/释放恢复新快照。该次全量CTest退出8，provider_stream_test既有错误导致16项NotRun，另有UI环境失败；后续CI修复单列，不改写此轮。原生PRECONFIG/GUI主链路未在此修复重测，MVP-05维持当前Stale。

## 阶段范围与待补项

G2汇总Windows本机订阅→真实转发→安全停止恢复的16项必要条件，一条有效运行可支撑多项。设计1.9将干净普通用户包、一次真实换网/唤醒、一次GUI整体强杀/恢复、运行中隐藏/重建及一次实际外部代理变更交给P3-14/G3。错误处理/所有权/秘密保护和代表性反例仍必需，范围调整不产生Pass。

G2仍需核对P2-11/16证据、一次实际GUI导入/选择/模式/系统代理/转发/退出主链路和缺失反例，再由P2-17汇总。当前G2/G3 NotRun、MVP Pass=0/16，G1视觉变更后Stale；本页不重新评定。

G3目标为Windows日常使用，TUN/服务、干净普通用户环境与必要宿主恢复仍待验。共享静态兼容由P3-13负责，Linux/macOS原生/系统集成归P4，CI编译成功不表示受管内核启动已实现；移动仅远期规划。当前规则见[构建说明](../../baseline/build_and_package.md)，发行见[CI与Release核验](20261027_github_release.md)。
