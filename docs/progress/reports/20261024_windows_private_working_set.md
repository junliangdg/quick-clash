# P0/P1 Windows 当前验收与专用工作集修复

> 历史运行证据保留。用户随后要求减少验证/性能工作；设计1.6及 [范围重评](20261024_development_first.md) 已将G1改为原型行为/单轮短测/工程预留，P1-06 Done、G1 Pass。本文下方的长时/帧/GPU阻塞描述反映修订前要求，不是当前阻塞；相应专项仍未运行。

日期/执行者：2026-10-24 / Codex。基于 HEAD 1ee26e64f2304e830ab4c7db73aad74ede50bc31 的未提交工作区，保留此前全部改动；没有提交、推送或触发远端 CI。本文为当前结论，旧报告与失败原始数据保留。

P0 的 Windows 首版范围已完成，G0 Pass。P1 的宿主、模块、双主题、代表页面、键盘与生命周期已运行；最新部署版双主题六场景短测全部满足专用工作集 100 MB，成本阶梯已找到并消除整份字体加载造成的额外驻留。G1 仍 Blocked：正式预热/重复/长场景、滚动帧时间与 GPU 数据尚不完整。订阅—代理产品闭环属于 P2，不能把核心控制面探针称为产品转发。

## 任务与构建

| 任务 | 当前交付及实际验证 | 限制 |
|---|---|---|
| P0-04 | 官方 Mihomo v1.19.33；认证、名称编码、组选择/模式读回、重载、重启选择重置、owned PID 回环监听、Job 回收、零网络能力 AppContainer 正反校验 Pass | [完整核心汇总](core_probe/20261024_isolated_bounded/summary.json)；生产适配与转发由 P2 完成 |
| P0-06/08 | Windows Release、QML cache/lint、7/7 CTest、独立 headless 1/1 Pass | 自有 MSVC /W4 /WX 保留；只豁免生成缓存的 C4702；headless 不依赖 Quick/Widgets/Network |
| P0-07 | Windows 本地构建/启动、无 UI 测试与基础 CI 配置完成 | 按用户 2026-10-24 指示，Linux/macOS 原生和 CI 实际执行移至 P4-06；未验证兼容性 |
| P0-09 | 部署绝对 prefix、随包 Windows 插件、移除开发 Qt PATH 后 smoke Pass | 干净普通用户完整离线包、VC runtime 和发行裁剪仍在 P2-16 |
| P1-01/02 | 单实例 IPC/锁、真实托盘、首次关闭确认、无托盘退路、八模块投影/路由、独立摘要嵌入通过 | 移动仅校验路由/投影边界，未实现底栏 |
| P1-03 | 浅/深主题、圆角贴片、首页与虚拟列表、中文字体、窄窗/多 DPR 截图、键盘导航/筛选/关闭弹层通过 | 代表原型；真实业务状态在 P2/P3，实际跨屏/OS 字体缩放和屏幕阅读器在 P4/P5 继续验证 |
| P1-04 | 隐藏/最小化释放需求、5 秒实验卸载、状态恢复、当前部署版 100 次重建通过 | 同步创建耗时不是首帧/首个真实数据时间 |
| P1-05 | 双主题六场景、多轮采样、原始 CSV/JSON、三项内存、CPU/句柄/线程、环境/模块/哈希与失败退出已运行 | 不包含帧时间/GPU/自动滚动或真实业务；采集器具备不等于 G1 全部验收 |
| P1-06 | 成本阶梯、字体修复、新口径三轮实测与未来组件预算记录 | 正式性能矩阵待补；G1 尚不通过 |

环境：Windows NT 10.0.26300.0 x64、Ryzen 7 5700X、RTX 4070 Ti；CMake 4.3.0、Ninja 1.12.1、MSVC 19.50.35727.0（VS 2026 18.4.1）、SDK 10.0.26100.0、Qt 6.8.0 msvc2022_64、yaml-cpp 0.9.0、clang-format 21.1.2。yaml-cpp 0.9.0 在该组合完整构建成功，无全局降低 CMake 策略。

最新构建日志：build/p1_windows/logs/20261024T033601115Z_11960bff97d14d2ca20cb1f12dc9042f/。7 项为 contracts、ui_state、single_instance、ui_prototype、qml_smoke、ui_keyboard、host_lifecycle；[JUnit](p1_resources/tests.xml) 已保存。独立 headless 日志：build/p0_headless/logs/20261023T185532673Z_dd8cb580c8d3426a94624fdfacd64853/，其后未改领域/配置实现。

最新部署日志：build/p1_windows/logs/deploy_20261024T033934947Z/。dist/p1_validated/bin/quickclash.exe SHA-256 **f2019bd7ca1927c9980fd964ac5858db3de37749e016953793e1e3a5b48a27ca**。BUILD_TESTING 开启，键盘探针和 Qt6Test DLL 仅在此配置加入；当前实测包含它的开销。windeployqt 仍提示 dxcompiler/dxil 和 VCINSTALLDIR 警告，本机 smoke/D3D11 可运行；不当作干净机器发行验收。

## 核心隔离与生命周期

tests/integration/core_isolation_probe.cpp 创建本次唯一、零网络能力 AppContainer。先验证父进程可连接自有回环端口，再验证子进程非阻塞连接在 1 秒界限内受阻；同一官方 Mihomo 字节在隔离环境中正例 -t 返回 0、反例返回 1、无新增文件。仅修改自有临时目录 ACL，结束清理 profile/目录。早期超时失败保存在 core_probe/20261024_isolated，完整运行在 [isolated_bounded](core_probe/20261024_isolated_bounded/summary.json)。未写系统代理。

原 Windows D3D11 threaded 的 100 次重建句柄由 755 增至 953。修复 unloadTimer 重复延长、关闭 persistent graphics/scene graph 后仍有增长，因此 Windows 未显式指定 QSG_RENDER_LOOP 时选 basic，保留 D3D11；其他平台默认不改。调用者覆盖循环需重测。

当前部署版 [100 次重建日志](p1_resources/lifecycle_current.log) 与 [最终诊断](p1_resources/lifecycle_current.json)：句柄 **779→779**、窗口 QObject 每次 **133**，同步创建20—29 ms，引擎 generation 最终101、QML错误0。每次销毁旧QPointer、释放需求并恢复路由/筛选。真实Windows [托盘日志](p1_resources/tray_current.log) exit0，包含首次确认、隐藏/需求释放、延迟卸载与重建；鼠标点击所有原生菜单项未逐项手工验收。同步创建毫秒数不等于可交互首帧；默认仍关闭实验卸载。

## 字体成本与修复

新增 bench/qt_cost_probe.cpp 和 bench/measure_qt_cost.ps1，分别测 QApplication、空 QQuickWindow、Basic 控件及默认/系统别名/应用字体，固定参考窗口、DPI 与 D3D11 basic。它是归因实验，不替代完整 UI。系统别名 Microsoft YaHei UI 在参考机解析为 Arial；原先 addApplicationFont 加载整份 msyh.ttc 能显示中文，但多占用约 40–50 MB 专用工作集。

| 成本阶段 | 默认字体专用工作集峰值 B | 加载整份 TTC 峰值 B |
|---|---:|---:|
| QApplication | 13,991,936 | 63,213,568 |
| 空 QQuickWindow | 35,717,120 | 76,029,952 |
| Basic 控件 | 47,730,688 | 92,856,320 |

[成本原始汇总](p1_resources/20261024_qt_cost/summary.json) 与 p1_resources/20261024_font_families 下诊断/图片表明，已安装 **Microsoft YaHei** 可直接正确解析、显示中文。因此产品只选用该已安装字体，缺失时回退系统 GeneralFont；不拷贝/下载 OS 字体、不加载整个 TTC。字体逐层注入 QML 控件，保留 SansSerif 风格与双主题可读性。

## 当前内存实测与预算

100 MB = 100,000,000 字节，仅约束客户端必要进程同一采样时刻的 **专用工作集之和**。采集使用 K32GetProcessMemoryInfo / PROCESS_MEMORY_COUNTERS_EX2.PrivateWorkingSetSize，API 不支持/失败则采集失败。WorkingSetSize 和 PrivateUsage 分别记录总工作集与私有提交，作为诊断，不触发 100 MB 失败。当前只有原型 GUI 一个客户端，无服务/解析/更新辅助；未来须加入聚合，Mihomo 单列。没有裁剪工作集或主动交换/压缩。

当前 [环境与二进制哈希](p1_resources/20261024_current/environment.json)、[12 场景汇总](p1_resources/20261024_current/summary.json) 和同目录 CSV/日志已保存。每场景 3 秒预热、10 秒稳定段、100 ms 采样、一次；窗口 1160×780 逻辑像素，实际 DPR 1.25、逻辑 DPI 96、D3D11 basic，回放 500 节点/1,000 连接。**12/12 执行 Pass、12/12 短测内存 Pass**。各指标峰值独立求取，不能相加；隐藏/卸载列包含启动峰值，稳态另见汇总。

| 场景 / 主题 | 专用工作集峰值 B（门槛） | 总工作集峰值 B（诊断） | 私有提交峰值 B（诊断） |
|---|---:|---:|---:|
| settings / light | 49774592 | 136519680 | 190386176 |
| settings / dark | 51118080 | 138711040 | 191311872 |
| home / light | 54525952 | 141119488 | 192864256 |
| home / dark | 54849536 | 143859712 | 194600960 |
| proxies / light | 50204672 | 136884224 | 180891648 |
| proxies / dark | 49672192 | 136278016 | 179314688 |
| connections / light | 51089408 | 138133504 | 180129792 |
| connections / dark | 49905664 | 136151040 | 178454528 |
| hidden / light | 53088256 | 140066816 | 193826816 |
| hidden / dark | 52846592 | 139640832 | 192278528 |
| unloaded / light | 53182464 | 140320768 | 192507904 |
| unloaded / dark | 52834304 | 139915264 | 191778816 |

此前新口径 [首轮](p1_resources/20261024_private_working_set/summary.json) 设置浅/深为 108,232,704 / 109,981,696 B，Fail；字体修复后 [上一部署版](p1_resources/20261024_system_font/summary.json) 与当前部署版均 12/12 短测 Pass。旧二进制及旧双指标门槛目录保留历史，不作为当前失败，也不由旧指标倒推新口径。

当前最高 54,849,536 B 距硬门槛约 45.2 MB；这不是未来业务实测。初始重新分配建议：已有 UI 包括 Qt/托盘/字体/模型按 60 MB 约束，网络/解析暂留 12 MB、仓库/有界日志和差异 8 MB、服务与更新暂存 10 MB、波动余量 10 MB。互斥归因、不重复计数；P2/P3/P4 每加真实组件重测，不假设 16 MiB 原文可无条件放入预算。

正式缺口：设计 13 的 ≥120 秒预热、每主题/场景 ≥5 次及长场景，稳定滚动帧 P95/P99、连接周转、GPU 分配、真实恢复首帧待实施/测量。参考客户端未统一版本/口径比较，不宣传节省比例。短测会漏掉采样间隔内尖峰，也不足以宣称 CPU/长期稳定通过。

## 视觉与键盘

最新 p1_visual/system_font 下已核对 home_light、home_dark、home_narrow、settings_150、connections_200、proxies_100 图片/诊断：两主题、720×560 窄窗、中文字体、滚动列表与贴片可读，page_ready=true、qml_errors=0。QT_SCALE_FACTOR 与 Windows 原缩放相乘，文件名不是实际缩放百分比：实际 DPR 分别为 1.5625、1.5625、1.5625、1.875、2.5、1.25；不称为原生 100/150/200% OS 文字缩放测试。

[Windows 键盘日志](p1_visual/system_font/keyboard_windows.log) 与 ui_keyboard CTest 通过：Tab 找代理导航，Space 激活，Tab 进筛选，逐键输入/清空经防抖进入状态，Ctrl+W 打开无托盘安全关闭弹层，Esc 取消仍保留窗口。QTest 只在 BUILD_TESTING 加入；真实多显示器移动、OS 字体缩放、屏幕阅读器与未交付业务操作仍按 04 的后续阶段验证。

## 接续

P1-06 补齐正式资源/滚动帧/GPU 测量，并冻结 G1 路线；当前不是资源超限阻断，而是证据不足。G1 保持 Blocked，正式通过后关闭 P1-06 并依赖推进 P2。P0 不再被后置 Linux/macOS/CI 阻塞。进度/命名/文档检查不代替产品验收。

交付核对：设计19分册已重新生成合并版并通过-Check；check_progress、check_source_layout（28目录/67文件）、C++ clang-format --dry-run --Werror、PowerShell全部自有脚本语法解析和git diff --check通过。12个资源场景与部署exe SHA一致性复核通过。完整clang-tidy、实际OS文字缩放和正式性能缺口未冒称通过。
