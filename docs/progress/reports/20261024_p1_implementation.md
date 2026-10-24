# P1 界面原型实施补丁与验证记录

> 历史记录：下文描述权限更新前的产物与检查。2026-10-24 后续重试已成功应用补丁并修订代码，当前状态和验证结果请看 [已落地报告](20261024_p1_applied.md)，不要重复应用补丁。

日期：2026-10-24。执行者：Codex。关联 P1-01—05；P1-06 尚未进入资源评审。基准为实际 checkout D:/repos/quick-clash 的未提交工作区快照，HEAD 为 1ee26e64f2304e830ab4c7db73aad74ede50bc31；此前 P0 修改保留。**本轮产物先保存为独立补丁，尚未写入该 checkout。只有应用补丁后才具备下述代码；本报告不表示 P1 或 G1 已完成。**

## 当前交付边界

迁移后的 checkout 已存在，旧目录不存在。本会话写权限仍指向旧目录，申请向新目录写入 P1 文件时自动审批服务返回 401 Unauthorized / 无可用账户凭据，动作未执行。没有改走其他工具写目标目录。可写可视化工作区保存 base/work 快照与 quickclash_p1.patch，供恢复目录权限后审查、应用；完整构建仍受此前执行环境及审批故障约束，本次没有绕过该约束构建副本。

补丁仅新增 P1 原型，不接入订阅、系统代理、TUN 或真实内核 API。首页与列表主动标注合成回放；无数据时使用未知/未接入说明，示例值不是运行结果。G0 保持 Blocked，G1 Blocked，MVP-01—16 保持 NotRun。

## 代码与设计对应

| 任务 | 补丁内容 | 尚缺验证 |
|---|---|---|
| P1-01 | app/single_instance.*：QLockFile 选主，用户范围本地 IPC，重复启动发送固定激活消息；app/application_host.*：QApplication / QSystemTrayIcon / QMenu 宿主，真实主界面仍为 Qt Quick；首次关闭确认，无托盘保留窗口并提供最小化/退出 | 原生编译链接、真实桌面重复启动、托盘可见性及无托盘操作 |
| P1-02 | FeatureRegistry 注册八模块与 proxy_summary 内容工厂；桌面投影独立于 resolve；PageHost 只创建当前页，EmbeddedHost 只加载声明内容，路由参数有类型/长度/允许键限制，外部 URL 不作为工厂 | 全部八路由运行、独立嵌入、多次切页/重建与错误路径 |
| P1-03 | 内置浅色/深色语义 ThemeTokens，默认跟随系统，显式偏好存于 ui.ini；左侧宽栏/窄轨，圆角平面贴片；首页与 500 节点/10,000 连接合成回放，C++ 按角色取值，ListView 复用 delegate，150ms 筛选合并 | 实际渲染与截图、720×560/1000×700/1280×800、键鼠/大字体/DPI；本轮尚无截图与视觉通过结论 |
| P1-04 | C++ 状态门面跨 QML 引擎存活；显示/隐藏/卸载/重建、最小化释放需求；按实例合并流，隐藏清空所有 token、停止回放 Timer；保存有界筛选/位置状态；隐藏 5 秒卸载为默认关闭的实验选项 | 100 次真实卸载循环、迟到回调、句柄增长/恢复耗时；未来业务命令仍需 P2 再验证 |
| P1-05 | bench/measure_windows.ps1：home/proxies/connections/hidden/unloaded 场景，原始 CSV、进程私有提交/工作集/单核 CPU/句柄/线程及生命周期 JSON；100,000,000 字节上限超限报错 | Release 可执行文件、原生资源运行、GPU/DPI/后端与完整构建环境归档 |

设置仅实现 UI 偏好；生产设置 schema/原子提交/写入失败反馈仍由 P2-02 交付。Qt Widgets 仅用于托盘菜单宿主，不绘制功能页面；Widgets 已纳入 Qt 模块清单，其内存计入客户端。托盘中的业务代理命令待 P2，当前只有打开、原型状态、设置和退出。

不支持页面显示解释与交付阶段，不提供可点击后伪成功的业务动作。移动端不创建底栏，只用导航投影与稳定路由证明“少量入口不删除模块”的契约。P1 的受限 group 上下文只接受 primary，P2 再绑定真实业务组；context 不允许传秘密、任意组件路径或 QObject。

## 自动验证与执行事实

| 项目 | 结果 | 说明 |
|---|---|---|
| Qt 6.8.0 qmlformat | Pass | 17 个 QML 文件解析/格式化 |
| moc / qmltyperegistrar | Pass | 四个 Q_OBJECT 头文件生成，导出只读 UiState / ReplayModel 类型信息供检查；不是 C++ 编译或链接 |
| Qt 6.8.0 qmllint | Pass | 基于真实 moc 元数据和静态工厂导出构造临时 lint 模块；最终无警告/输出，完整 CMake 生成模块仍待编译验证 |
| clang-format 21.1.2 | Pass | 新增/修改的 C++ 文件格式检查 |
| PowerShell Parser | Pass | 资源脚本语法检查；尚未运行采集 |
| 源码命名、进度、设计合并 | Pass | 28个目录/57个自有文件；台账P0=4 Done/5 Blocked、P1=5 Blocked/1 Todo；19分册合并与链接一致 |
| 原生构建 / CTest / 托盘交互 / 截图 / 资源测量 | NotRun | 不以静态工具或已写测试替代运行 |

新增 ui_state_test 覆盖路由输入/嵌入校验、精选导航仍可路由、主题持久化、实例隔离、有界回放、合并需求、隐藏释放和 100 次 token 生命周期。single_instance_test 使用子进程核对主实例收到一次激活。ui_prototype CTest 在 offscreen 软件渲染下切换八页/双主题并卸载重建 100 次，记录代际与需求清空，QML 类型/绑定错误导致失败。**上述测试已写，尚未编译/执行。** 软件渲染只用于 smoke，不能作为真实 GPU 或桌面视觉/性能证据。

## 权限恢复后的复现步骤

1. 在目标目录执行 git apply --check <quickclash_p1.patch>；审查差异后 git apply <quickclash_p1.patch>。补丁基于本轮开始时的未提交内容，工作区若变化需重新核对，不能强制覆盖。
2. 使用新构建目录，避免复用迁移前绝对路径：

    pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -BuildDirectory build/p1_windows

构建脚本会执行 lint 与 CTest。ui_state、single_instance、qml_smoke 和 ui_prototype 均需有真实退出码/JUnit 后再更新台账。手工打开 build/p1_windows/quickclash.exe --replay，核对八入口、双主题、缩放、筛选、关闭确认/托盘及 --no-tray 分支。

3. 使用实际 Release 程序与新的证据目录执行采集：

    pwsh -NoProfile -File bench/measure_windows.ps1 -Executable build/p1_windows/quickclash.exe -OutputDirectory docs/progress/reports/p1_resources/run_01

环境须另外保存 CMakeCache、Qt/编译器、屏幕 DPI/GPU/图形后端。脚本采集的启动峰值与稳态均保留；当前 P1 只有一个 GUI 进程，后续必要服务/更新进程必须聚合，不能沿用此单进程结果证明整个产品。采集不含滚动帧时间、第三方比较、三平台和真实网络，需分别补测。

## G1 资源决策与下一任务

尚无内存实测，不能宣称 ≤100 MB，也不能冻结自动卸载收益或继续路线。暂保留设计的 80 MB 工程工作预算 / 20 MB 波动余量；解析、业务/缓存、权限服务、更新等未实现成本按 13 的分项预留，P2/G3/G4 必须计入并复测。P1-06 保持 Todo，待真实原型采集、恢复延迟与句柄趋势后给出结论。

接续顺序：先恢复新 checkout 的写权限并应用本补丁；补 P0-06 / P0-08 原生构建与契约运行，再运行 P1 自动/手工/资源场景。P0-04 的 Job/端口归属/断网隔离和 P0-07 三平台缺口独立保留。G1 前置 G0 未通过，任何局部静态通过都不能勾选整个阶段。

最终静态工具原始输出与退出码/耗时见 [qmllint 日志](p1_static/20261024/qmllint_final.log)、[元数据](p1_static/20261024/qmllint_final.log.json)及同目录 moc/qmltyperegistrar 日志。最终 qmllint 退出0且无诊断；首次 lint 的警告已修正，初次工具 help 直接调用挂起后结束了本次进程，改由有界执行器后正常完成。

补丁最终已对实际 D:/repos/quick-clash 工作区运行 git apply --check --whitespace=error，退出0，未写入仓库；此前误对已修改的work副本检查和混合换行/多余末尾空行导致的失败已修正，未以失败记录冒充应用成功。
