# 窗口缩放与导航切换卡顿修复

2026-10-24 / Codex，P2-24 Done，依赖P2-18。本机Windows连续缩放的秒级阻塞已复现并修复，4/4受影响UI检查、真实窗口160步连续缩放及最终包一次Ready首页内存短测通过。保留此前全部工作区修改，没有提交或推送，未停止或覆盖用户正在运行的旧dist/p2_protocols_final实例。P2现20Done/2Review/2Blocked，G2仍Blocked。

## 1. 原因与修复边界

用户反馈在导航隐藏名称以及切换底栏时有明显卡顿。先排查到壳外层多级Layout、footer和导航文字隐式尺寸会在断点处重新协商；PageHost本身没有主动换页。改为明确壳几何并固定曲线绘制缓冲后，offscreen检查通过，但真实Windows仍出现秒级停顿，因此不能仅以布局解释或宣称修复。

真实渲染日志记录多次约1000ms的sync；本地调试栈显示GUI线程位于NtWaitForSingleObject → Qt6Gui → QRhi::beginFrame → QSGRenderLoop → QQuickWindow::event。结合Qt6.8本地D3D11行为及关闭等待对象的对照，主要阻塞来自帧延迟等待对象。basic循环原先为避免threaded的重建句柄增长而采用，该图形等待因此也会阻塞GUI线程。证据：[渲染耗时](p2_evidence/20261024/resize/resize_trace_stderr.log)、[本地栈](p2_evidence/20261024/resize/resize_stack_local.log)。

最终实现：

- app/main.cpp：Windows、basic、空/d3d11 RHI且无QT_QUICK_BACKEND和显式D3D选项时，在QApplication之前默认设置QT_D3D_MAX_FRAME_LATENCY=0。禁用帧延迟等待对象，保留D3D11 GPU、现代flip交换链和垂直同步；显式QT_D3D_MAX_FRAME_LATENCY或QT_D3D_NO_FLIP不覆盖，其他渲染循环/后端/平台不应用此默认值。
- qml/main.qml：壳按侧栏宽度、底栏高度和锚点确定页面矩形；导航按钮/图标几何不依赖文字隐式尺寸，隐藏名称不触发外层Layout协商。保留1000图标轨、600底栏及360×480最小窗，底栏仍为首页/代理/设置。当前PageHost保持同一实例，页面内部继续按Layout重排。
- qml/components/traffic_sparkline.qml：Canvas固定240×36有界绘制缓冲，场景图缩放；数据、主题和重新可见时更新，宽高变化不反复重绘/上传纹理。小曲线随贴片拉伸，独立大图表未来应另定绘制策略。
- app/keyboard_probe.cpp：扩展既有responsive探针，首页/代理各80步，1120→360→1120两轮跨两个断点，断言当前页面指针、路由、引擎及需求数量保留；输出事件处理耗时，包含QTest短等待。app/application_host.h补明确QSize头依赖。

网络、系统代理及内核生命周期实现未改。未重启用户内核或操作用户系统代理；最终Ready短测只管理自己的隔离实例与owned核心。

## 2. 构建与真实运行

Windows本地工作区补丁，未提交；Qt6.8.0 msvc2022_64、MSVC19.50、CMake4.3.0、Release/WX，构建目录build/p2_responsive，实际GPU NVIDIA GeForce RTX4070Ti。官方Mihomo v1.19.33字节不变。

| 检查 | 实际结果 | 证据 |
|---|---|---|
| 最终增量构建与quickclash_qmllint | Pass，退出0；既有Qt Dialogs重复类型/导入警告保留 | [build_final.log](p2_evidence/20261024/resize/build_final.log) |
| qml_smoke/ui_keyboard/ui_responsive/host_lifecycle | Pass，4/4，共4.86s | [输出](p2_evidence/20261024/resize/ctest_final.log)、[JUnit](p2_evidence/20261024/resize/ctest_final.xml) |
| 真实Windows默认渲染配置运行--responsive-test | Pass，退出0；无临时latency/no-flip覆盖，确认D3D11/非legacy flip/等待对象禁用 | [输出](p2_evidence/20261024/resize/final_native.log)、[运行元数据](p2_evidence/20261024/resize/final_native.log.json) |
| 首页80步 | 中位10.078ms，p95 14.925ms，最大15.297ms，页面/需求保留 | 同上 |
| 代理80步 | 中位10.069ms，p95 14.825ms，最大19.935ms，页面/需求保留 | 同上 |
| 完整包部署及清Qt开发PATH smoke | Pass；dxcompiler/dxil及VCINSTALLDIR部署警告保留，CRT由打包脚本显式复制 | [package.log](p2_evidence/20261024/resize/package.log) |
| 中文430×780逻辑尺寸首页 | Pass，截图已人工核对曲线、贴片与三个底栏；实际DPR1.25 | [截图](p2_evidence/20261024/resize/home_zh.png)、[捕获元数据](p2_evidence/20261024/resize/capture.log.json) |
| 包文件摘要 | Pass，180个文件大小和SHA-256逐一一致 | [清单](p2_evidence/20261024/resize/package_manifest.json)、[核对结果](p2_evidence/20261024/resize/package_check.json) |

事件处理耗时包含resize及QTest::qWait(1)的实际事件循环处理，不是显示器呈现帧时间，也不是持续转发下的缩放验收。真实窗口探针没有启动Mihomo流量负载；中文截图为明确的回放数据，不能证明代理转发。未增加新测试套件或重复完整网络/性能矩阵。

## 3. 最终包单轮内存短测

执行bench/measure_runtime_windows.ps1，最终包真实Qt Quick首页、仓库worker及官方owned Ready内核，本地公开测试夹具导入/选择，系统代理未修改。10秒、100ms采样间隔，共91样本。

| 进程范围 | 专用工作集峰值B | 总工作集峰值B | 私有提交峰值B |
|---|---:|---:|---:|
| 客户端进程组（无额外客户端辅助进程） | 60,751,872 | 154,664,960 | 203,542,528 |
| 独立Mihomo内核 | 7,761,920 | 28,037,120 | 63,447,040 |

客户端专用工作集约60.75MB，低于100,000,000B目标；另外两项仅诊断，内核单列。客户端退出0、owned内核已退出。原始证据：[summary.json](p2_evidence/20261024/resize/runtime_memory/summary.json)、[memory.csv](p2_evidence/20261024/resize/runtime_memory/memory.csv)、[cleanup.json](p2_evidence/20261024/resize/runtime_memory/cleanup.json)。

仅该短时Ready首页场景通过，不涵盖持续下载、导入峰值、隐藏重建、CPU/GPU、呈现帧率或长期运行。旧包相关UI/图形/资源结论对本包Stale；历史100次重建句柄数据保留，但未以新选项重跑该矩阵，当前只重跑受影响生命周期检查。未把旧结果直接当作新包通过。

## 4. 产物、失败历史与接续

完整包dist/p2_resize，入口dist/p2_resize/bin/quickclash.exe，须保留整个目录。build与dist的客户端SHA-256均为e61d250fcbb06ab09f5de7795a0763ae05b43c3db92b67600704f2f282b13f01；核心SHA-256为1cde41cfffd3de0aedad63c0a8515e1ce5eea507e8133aed0f637d316c93800a。BUILD_TESTING探针开启、未签名的本地验证包，干净普通用户环境仍未验收。渲染选项在进程启动前生效，需正常退出旧QuickClash再启动新包；启动新文件激活旧单实例不能加载修复。

失败/诊断历史未覆盖：baseline_native超时20秒；布局修复后的fixed_native退出29，之后诊断仍超时25秒，旧失败不能改写为通过。offscreen始终没有暴露主要D3D11等待。初次沙箱构建停在glob核对并取消，沙箱外构建成功；首轮调试器默认符号设置超时，本地离线符号栈成功。临时QT_D3D_NO_FLIP=1和QT_D3D_MAX_FRAME_LATENCY=0均消除等待，采用后者以保留flip呈现。原始记录均保存在p2_evidence/20261024/resize目录，不推广临时调试脚本为生产代码。

设计同步04/17及UI模块边界，TASKS/CHECKLISTS/DEVLOG更新P2-24。文档生成/-Check、进度/源码命名、三份C++格式dry-run、两份QML格式对比及git diff --check均Pass，记录见[delivery_checks.log](p2_evidence/20261024/resize/delivery_checks.log)、[C++退出码](p2_evidence/20261024/resize/cpp_format.log.json)、[QML结果](p2_evidence/20261024/resize/qml_format_check.json)。首次进度检查拒绝目录型证据链接，改为普通目录文本后通过，失败输出保留。沙箱内格式工具无输出后取消，沙箱外有界执行器确认真实退出0。文档检查不等于产品验收。

本轮未请求私有订阅，未将用户地址或token加入仓库。仓库文本（含证据，排除.git/build/dist/cache和二进制）的凭据型订阅查询值/长IPv4路径扫描为0匹配，见[privacy_check.json](p2_evidence/20261024/resize/privacy_check.json)；该启发式范围不宣称完整秘密审计。

后续仍为P2-11一次实际Windows换网/唤醒核对、P2-15关键客户端强杀/租约恢复、P2-16干净普通用户环境，随后P2-17/G2；本次不改变MVP-05/06既有系统代理/受控转发通过范围。Geo/fake-ip等按P3排期，不因本次修复新增长时或重复性能矩阵。
