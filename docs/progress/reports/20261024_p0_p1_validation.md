# P0/P1 Windows 实施与验收收尾

> 历史记录：本文下面的双指标资源失败与跨平台阻塞结论已被用户的新指标/Windows 首版范围及后续代码实测替代。当前以 [专用工作集修复与验收](20261024_windows_private_working_set.md) 为准；保留原始运行事实，不沿用为当前状态。

日期/执行者：2026-10-24 / Codex。基于 HEAD 1ee26e64f2304e830ab4c7db73aad74ede50bc31 的未提交工作区，保留此前全部改动；没有提交、推送或触发远端 CI。本文替代旧报告中的“最新阻塞”描述，旧失败记录保留。

工程已可在 Windows 完整编译、运行、部署，核心隔离契约已验证。P0-07 缺 Linux/macOS 原生证据；G0 仍 Blocked。P1 的宿主、模块边界、回放和采集实现已运行；资源超过 100,000,000 字节，G1 为 Fail。订阅—代理产品闭环属于尚未实施的 P2，不能将本次核心控制面探针当作转发功能。

## 实现与证据

| 任务 | 本次交付/核对 | 结果及限制 |
|---|---|---|
| P0-04 | 官方 Mihomo v1.19.33；认证、名称编码、选择/模式读回、热重载、重启选择重置、owned PID 的回环监听、Job 子进程回收、零能力 AppContainer 隔离校验 | Pass；[完整核心汇总](core_probe/20261024_isolated_bounded/summary.json)，不修改系统代理 |
| P0-06 | CMake/Qt Quick 类型导出、qmlcache、MSVC 严格警告、生成缓存 C4702 边界、关闭不适用的 AutoUic | Windows Release、lint、6/6 CTest Pass |
| P0-07 | 三平台 CI 配置已具备，Windows 本地通过 | Blocked；当前无 Linux/macOS 执行环境/远端 CI run，不虚构平台兼容 |
| P0-08 | domain/application/config 与有界 YAML 正反夹具 | UI 构建 contracts 和独立 headless 构建 1/1 Pass；headless 不依赖 Qt Quick/Widgets/Network |
| P0-09 | scripts/deploy_windows.ps1：绝对 install prefix、随包 Windows 插件、移除开发 Qt PATH 后启动，失败留证 | Pass；不等于普通用户干净机器、完整 P2 离线包或安装器验收 |
| P1-01 | 单实例 IPC/锁、托盘菜单/状态、首次关闭确认、无托盘最小化/退出退路、宿主三态 | 自动单实例、host_lifecycle 与真实 Windows 托盘宿主探针 Pass；鼠标点系统托盘菜单的全面手工检查未替代为已测 |
| P1-02 | 八模块轻量描述/路由、导航投影、受限上下文、独立代理摘要嵌入 | 单元与八页面/双主题加载 Pass；移动底栏尚未实现，仅保留边界 |
| P1-03 | 浅/深/系统主题、圆角贴片、首页、虚拟代理/连接列表、系统 CJK 字体回退 | 自动加载/截图 Pass；键盘完整操作、多 DPI/多屏与窄窗视觉覆盖不足，保留 Blocked |
| P1-04 | 页面需求隐藏释放、无跨引擎 QML 引用、轻量状态/几何恢复、100 次卸载重建 | 默认 Windows D3D11 basic 路径句柄/对象稳定，已修复实测线程渲染增长；见下文 |
| P1-05 | 原始 CSV/JSON、双主题六场景、私有提交+保守工作集、CPU/句柄/线程、模块和渲染环境、超限退出失败 | 脚本已执行；默认是探索性采样，不能作为正式帧率/完整性能准入 |
| P1-06 | 原型资源原始数据、未实现预算及路线评审 | Blocked；实测失败，不能冻结为可达标路线或展开大规模业务集成 |

工具链：Windows NT 10.0.26300.0 x64、Ryzen 7 5700X、NVIDIA GeForce RTX 4070 Ti；CMake 4.3.0、Ninja 1.12.1、MSVC 19.50.35727.0（VS 2026 18.4.1）、SDK 10.0.26100.0、Qt 6.8.0 msvc2022_64、yaml-cpp 0.9.0、clang-format 21.1.2。yaml-cpp 0.9.0 在此组合完整构建通过，无全局降低 CMake 策略。

最终构建目录 build/p1_windows，日志 logs/20261023T193020034Z_7806821bc68a41228d43bdcc5b0634cf/；6 项测试为 contracts、ui_state、single_instance、ui_prototype、qml_smoke、host_lifecycle。原始 [JUnit](p1_resources/tests.xml) 已复制到报告目录。独立 headless 日志在 build/p0_headless/logs/20261023T185532673Z_dd8cb580c8d3426a94624fdfacd64853/。这些是本机路径，清理 build 前保留报告中的原始证据。

## 核心隔离与失败恢复

新增 tests/integration/core_isolation_probe.cpp：仅创建本次唯一 AppContainer，无网络能力；父进程能连接自有回环端口，同配置子进程在 1 秒期限内不能连接。实际失败表现为超时 10060，不描述为 10013。使用完全相同的官方内核字节执行 -t，正例退出 0、坏 YAML 退出 1，目录文件数不增加；删除临时 profile，finally 删除自有会话目录。只对新建临时目录/文件授予 SID 权限，不修改全局网络/防火墙或安装内核服务。

子进程 CREATE_SUSPENDED → 绑定 KILL_ON_JOB_CLOSE Job → ResumeThread；显式继承输出句柄，限时回收。首次隔离探针因阻塞 connect 超时失败，证据保存在 core_probe/20261024_isolated/；改为非阻塞、有界 select 后通过。不能只用“没有新文件”证明网络隔离。

## 生命周期结果与修正

原默认 Qt 6.8 D3D11 threaded 路径在 100 次重建中，句柄从 755 增至 953（每轮约 2），窗口子 QObject 恒为 133；[原始记录](p1_resources/lifecycle_100.json)。主动处理 DeferredDelete 和关闭 persistent graphics/scene graph 均不能单独消除该增长。

对照：离屏软件 313→313；Windows 软件 723→723；D3D11 basic 776→776。Windows 现在在调用者未设置 QSG_RENDER_LOOP 时默认 basic，保留 D3D11 后端；其他平台保持 Qt 默认。最终默认路径 [100 次记录](p1_resources/lifecycle_final.json) 中句柄 754→754、窗口子 QObject 133，单轮同步创建最大 38 ms。此数字不等同首帧/首次可交互时间；尚无全程显存/内存趋势或长稳结论。调用者覆盖为 threaded 时仍须复测，不能沿用通过结果。

[托盘原始结果](p1_resources/tray_final.log) 与 [命令/退出码](p1_resources/tray_final.json)：首次关闭显示确认、确认后隐藏、记住选择、隐藏释放需求与 timer、5 秒后卸载、再次打开仅增加一次 engine generation 通过。无托盘路径由 host_lifecycle 验证：两种关闭请求均不消失、不误记确认；最小化释放、恢复重新申请需求，720×560 触发紧凑侧栏，重建保留路由/筛选/几何。

回放默认关闭；启用后 500 节点、1,000 连接、120 点曲线、按需 1 Hz。--replay-connections=10000 为明确探索档，其他数字拒绝，不将旧 10,000 连接采样冒充支持档。隐藏清空需求，静态代理列表和设置页不启动周期 timer，列表只创建可见 delegate。

Windows 优先使用系统 Fonts/msyh.ttc 中现有字体，不再沿用 Latin 字体回退；不存在时使用系统 UI 字体，不下载、不随包复制系统字体。实际解析见 [字体/页面诊断](p1_visual/font.json)。最终 [浅色](p1_visual/home_light.png) / [深色](p1_visual/home_dark.png) 截图可审阅；未实现操作明确禁用或标明阶段。

## 资源结论：G1 Fail

100 MB 指 100,000,000 字节。Windows 私有提交和保守工作集均须达标；必要客户端辅助进程聚合，Mihomo 独立。本次只有原型一个客户端进程，无服务/更新/解析辅助，已超限，不能靠分进程或选择较小口径规避。

所有短测均为 3 秒预热、10 秒采样、100 ms、每场景一次，非设计 13 的 120 秒预热/5 次/长场景正式验证；明确超限足以否定本机达标，不足以推断所有机器必然失败或 CPU 正式通过。1160×780 逻辑窗口，参考 DPR 1.25，逻辑 DPI 96。没有缩小窗口或强行裁剪工作集制造通过。

| 运行/后端 | 场景 | 私有提交峰值 B | 工作集峰值 B | 结论 |
|---|---|---:|---:|---|
| 初始 D3D11 threaded | 浅色设置 | 188,301,312 | 137,957,376 | Fail |
| 初始 D3D11 threaded | 浅色首页 | 193,994,752 | 144,187,392 | Fail |
| 软件兼容对照 | 浅色设置 | 111,976,448 | 110,260,224 | Fail |
| OpenGL 对照 | 浅色设置 | 314,515,456 | 261,115,904 | Fail |
| D3D11 basic 修正后，1,000 连接 | 浅色设置 | 190,799,872 | 142,725,120 | Fail |

原始目录：[初始双主题六场景](p1_resources/20261024_initial/summary.json)、[软件对照](p1_resources/20261024_software_comparison/summary.json)、[OpenGL 对照](p1_resources/20261024_opengl_comparison/summary.json)、[basic 设置页](p1_resources/20261024_final_settings/summary.json)、[最终字体版本六场景](p1_resources/20261024_final/summary.json)。最终目录按每轮写入，交付时以实际汇总为准；环境含 exe SHA-256、rendering environment，诊断含 Qt/font/backend/DPR/window，模块清单用于归因。旧 exe 的结果只作历史对照，不当作当前精确数字。

初始 D3D11 卸载后稳态仍为 130,826,240 / 116,654,080 字节；软件卸载后为 90,435,584 / 100,728,832，启动峰值仍超限。隐藏 5 秒卸载继续保持默认关闭的实验选项，不能据此宣称 100 MB 达标。曾观察到外部 RTSSHooks64 注入；本次不改用户设置，不能未经受控对照把超限归因于该模块。最新模块清单是否含注入以每轮原始文件为准。

路线：保留 Qt Quick UI，先在同一窗口/DPI/负载做 QApplication、空 QQuickWindow、Basic 控件、完整页面的成本阶梯，分析 Qt Widgets/托盘增量、字体/纹理、驱动及第三方注入；对实际瓶颈再决定原生托盘或精简组件。软件/OpenGL 均未解决本机目标，不默认切换为“更省内存”。独立 UI 进程只能改善隐藏释放，正常可见聚合上限仍适用。未完成对照前不声称基本壳固定成本或未来组件预算已获证实。

后续至少保留约 20 MB 给业务网络/解析、仓库、日志/连接差异、服务与更新暂存/临时峰值；这是待验证要求，不是当前可用余量。当前可见原型已超上限，新增组件可用预算为负。必须先将代表 UI 压到约 80 MB 工程目标，再用真实 P2 负载重新分配/验证；不能将原来的 80 MB 假设写成冻结实测。

## 运行与剩余验收

构建：scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64 -BuildDirectory build/p1_windows；部署：scripts/deploy_windows.ps1 -Destination dist/p1_validated。工具不在 PATH 时传 -CMake/-Ninja/-VsInstallPath，详见构建基线。

部署最初用相对 prefix 导致 Qt 6.8 qt.conf 失败，已改绝对路径；第一次 offscreen 启动失败因为生产部署只含 windows 插件，已改用随包 Windows 插件验证。仍有 windeployqt 的 dxcompiler/dxil、VCINSTALLDIR 警告，当前 D3D11 启动通过，干净环境 VC runtime/发行裁剪由 P2-16 验证。最新部署日志 build/p1_windows/logs/deploy_20261023T193135582Z/。

下一步：P0-07 在 Linux/macOS 执行 CI 对应命令并保存结果；P1-03 完成键盘筛选/焦点/关闭弹层、窄窗及多 DPI/字体大小视觉验收；P1-06 完成成本阶梯和资源削减，再运行正式重复场景/帧时间/显存/长期趋势。G0/G1 未通过前不大规模展开 P2。检查脚本通过仅证明台账/命名/生成文档一致，不代替产品验收。
