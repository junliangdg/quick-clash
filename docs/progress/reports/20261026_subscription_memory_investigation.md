# 订阅更新后内存增长调查

日期：2026-10-26；执行者：Codex；范围：本机 Qt 6.8.0，约58节点，运行内核时更新订阅，再回首页观察内存。工作区交付，未提交。

## 当前结论与边界

真实实例的异常增长已确认。相同PID 20008约26秒内工作集由166.74 MB升至770.79 MB，其中字体文件与Windows字体缓存的驻留页由27.14 MB升至504.03 MB，解释约79%的新增工作集。主要来源已定位到GUI字体使用路径；尚未锁定具体触发文字/操作，不能将剩余私有驻留增长全部归因于字体，也不能宣称修复。公开夹具未复现同量级增长，不替代真实实例证据。另一机器Qt6.9的输入/二进制/驱动尚未对齐，未执行版本对照。

按用户最新要求恢复原符号图标，移除VectorIcon自绘组件；保留完整字体配置传递、纯文本和诊断改动，恢复版增量构建/lint及3项相关UI测试通过，专项Review。此前矢量版构建、5项测试及短测保留为历史，对当前版的资源数字Stale；未将GDI设为默认，真实异常修复验收仍NotRun。

首页资源显示由 `DesktopRuntime::pollFeatures()` 每5个1秒tick采样，显示晚约5秒不证明分配在5秒后发生。订阅忙碌重试为5秒；隐藏卸载默认5秒但要求后台窗口和设置启用，不能解释前台订阅页的持续增长。`collectGarbage()`清理磁盘历史文件，不是QML垃圾回收。

## 真实实例的内存分布

只读采样同一客户端PID 20008：20:38:06.517与20:38:32.397，十进制MB。用户自行操作更新订阅并返回首页，采样间没有逐操作时间戳，不据此判定增长的准确瞬间。

| 指标 | 前 | 后 | 增量 |
|---|---:|---:|---:|
| 工作集 | 166.74 | 770.79 | 604.04 |
| 私有提交 | 204.87 | 227.33 | 22.46 |
| 私有驻留页合计 | 68.24 | 178.81 | 110.57 |
| 字体文件/FontCache驻留页 | 27.14 | 504.03 | 476.90 |

两次均有163个字体/字体缓存映射：主要变化是已映射文件的页面成为驻留，而非新增163个映射。增长后包含mingliub.ttc 37.11 MB、NotoSerifSC-VF.ttf 25.13 MB、simsunb.ttf 21.90 MB、msjh.ttc 21.41 MB、msyh.ttc 19.71 MB以及两份各约16.78 MB的FontCache映射，部分几乎整文件驻留。字体映射的PrivateResident为0，属于可共享页，不计入专用工作集硬目标；不代表一定正在被别的进程共享。

专用驻留增长110.57 MB，但私有提交仅增加22.46 MB，说明不能把这110.57 MB等同于新增未释放堆对象；先前已提交页变为驻留也会造成此差异。私有区的对象/调用栈尚未采集，不能排除独立业务分配。工作集长期维持高值也不能单凭一次更新证明持续泄漏。

证据：[前](subscription_memory_evidence/live_maps_20008.json)、[后](subscription_memory_evidence/live_after_20008.json)、[只读采样脚本](subscription_memory_evidence/inspect_memory_maps.ps1)。脚本调用VirtualQueryEx/GetMappedFileNameW/QueryWorkingSetEx，只保存文件基名和页计数，无内存内容、订阅链接或转储；非原子采样，分项和进程瞬时总值有小差异。PrivateResident按有效页且Shared位为0计算，不冒充堆归属分析。

## 初始公开夹具实测

当前build/dev主程序，Qt6.8.0；本机HTTPS公开夹具，56节点/12001规则/508300字节，未变更新与有变更新/自动应用，客户端单独采样，不含Mihomo，无系统代理/TUN。每列为阶段峰值，十进制MB。

| 渲染/阶段 | 专用工作集MB | 工作集MB | 私有提交MB |
|---|---:|---:|---:|
| Windows软件/更新前 | 44.28 | 127.00 | 120.16 |
| Windows软件/有变更新 | 47.82 | 130.54 | 123.62 |
| Windows软件/更新后5秒 | 45.06 | 128.06 | 120.81 |
| D3D11 basic/更新前 | 69.92 | 169.23 | 217.17 |
| D3D11 basic/有变更新 | 72.36 | 171.67 | 219.39 |
| D3D11 basic/更新后5秒 | 70.10 | 169.42 | 216.60 |

运行命令：经 `scripts/native_process.ps1` 的 `Invoke-NativeStep` 调用 `build/dev/quickclash.exe --subscription-memory-test`，TimeoutSeconds=45；PATH指向Qt6.8.0。软件运行设置 `QT_QUICK_BACKEND=software`；默认D3D11运行设置 `QSG_INFO=1`，日志确认RTX4070Ti/D3D11/basic。两次退出0，未变更新保持核心PID、有变更新Ready/代次改变、error为空。

证据：[软件运行](subscription_memory_evidence/gui_windows_software_baseline.log)、[D3D11运行](subscription_memory_evidence/gui_d3d11_baseline.log)，旁附命令与退出码JSON。首轮沙箱内offscreen探针退出20（settings未就绪）、旧runtime_test退出1（store.open失败）仅为失败尝试，不计产品验收；正常Windows权限下上述GUI探针通过。

## 定向探针补齐

既有探针扩为58节点，更新后订阅页观察6秒，再回首页观察6秒；`--probe-unicode`在有变更新时引入中文与旗帜/Emoji节点名，`--probe-missing-glyph`引入未分配字符U+0378。仅测试探针代码变化，不调整产品内存策略或强制裁剪工作集。Windows增量构建quickclash/runtime_test退出0（build_probe_final、build_missing_glyph日志）；此前链接LNK1104由运行中程序占用，用户关闭后已成功。

| 58节点/D3D11场景 | 更新前专用/工作集/提交MB | 回首页6秒阶段峰值MB | 结果 |
|---|---|---|---|
| 中文、旗帜、Emoji | 78.92 / 178.72 / 238.26 | 75.99 / 176.10 / 229.11 | 退出0，未复现大增长 |
| U+0378缺字 | 77.32 / 176.98 / 243.14 | 68.21 / 171.49 / 219.23 | 退出0，出现DirectWrite ADMUI3Lg字体失败警告，未复现大增长 |

证据：[Unicode](subscription_memory_evidence/gui_unicode_home.log)、[缺字](subscription_memory_evidence/gui_missing_glyph.log)。两者均确认未变更新保留内核、有变更新成功应用；这两份早期探针没有首页文本断言，后续补齐如下，均不能据此排除其他字体回退条件。

无GUI业务对照`runtime_test --update-memory-probe --probe-unicode --probe-missing-glyph`退出0：更新前专用/工作集/提交5.97/31.01/7.77MB，更新峰值10.22/35.28/13.73MB，更新后6秒6.79/31.88/9.30MB。[业务对照](subscription_memory_evidence/business_unicode_missing.log)。这是相同公开夹具的业务路径证据，不是用户真实订阅的无GUI复现。

首页文字核对首轮退出21：[日志](subscription_memory_evidence/gui_visible_unicode_missing.log)。检查放在停止内核之后，导致显示状态可能已清空；且两个选项合用时先替换node_0，缺字替换不再命中。此轮不计显示验证Pass。探针已修正为停止前核对选中节点及可见QQuickItem文本，组合选项将缺字附加在Unicode节点上；回首页滚动至资源卡以贴近用户操作。

最终GUI短测退出0，20.8秒：[日志](subscription_memory_evidence/gui_home_resource_text.log)。中文界面，节点名使用截图同类文字`🇯🇵日本高速02|CTCU|0.5x`加U+0378，确认`updatedNodeSelected=true`、`homeOutboundTextObserved=true`；回首页6秒阶段峰值专用/工作集/提交73.56/173.70/226.61MB（更新前74.95/174.69/235.97MB）。可见QQuickItem的text属性包含完整目标节点；此断言不统计每个字形是否绘制成功。DirectWrite仍报告ADMUI3Lg字体失败，未复现数百MB增长，因此不能认定普通旗帜/单个缺字足以触发真实异常。

最终同输入无GUI对照退出0，14.2秒：[日志](subscription_memory_evidence/business_home_fixture_final.log)。更新前专用/工作集/提交5.61/30.65/7.35MB，更新峰值10.84/35.90/14.07MB，更新后6秒6.87/31.94/9.40MB；选中目标节点一致。无GUI没有Home阶段，home字段0表示未采样，不代表进程内存0。

最终构建`cmake --build build/dev --target quickclash runtime_test`退出0：[日志](subscription_memory_evidence/build_home_probe.log)。Qt6.8.0/MSVC2022 x64 Release，VS18开发环境；quickclash.exe SHA256=`52CFEA52607DCCB0EF283CFE122E7DACD3BF87085C6622F32CDD81C95936C896`。运行经native_process.ps1，PATH加入C:/Qt/6.8.0/msvc2022_64/bin，GUI参数`--subscription-memory-test --probe-unicode --probe-missing-glyph --probe-language=zh-CN`，业务参数`--update-memory-probe --probe-unicode --probe-missing-glyph`；每条日志旁JSON保存精确参数/时间/退出码。

既有CTest：qml_smoke通过；ui_subscriptions/ui_subscriptions_zh失败，输出offscreen字体目录缺失和不支持raise，不能计通过。此前DEVLOG已有相同环境问题记录。[CTest](subscription_memory_evidence/ctest_gui.log)。原生Windows中文订阅UI探针另测退出82，对应初始订阅卡数量断言，stderr为空：[原生记录](subscription_memory_evidence/subscription_native_zh.log.json)。因此不能将这两项失败全部归因于offscreen字体，也未独立验证其基线归因；该失败不在本轮字体调查中修改。

最终构建后qml_smoke再次通过（1/1，0.48秒）：[记录](subscription_memory_evidence/ctest_final_smoke.log)。最终进度检查退出0，历史dist断链未再出现；源码命名（36目录/165文件）、C++格式及diff检查退出0。未改设计，不触发设计生成。上述检查不替代真实异常场景的修复验收。

## 机制判断与尚未完成的验证

Qt6.8默认Windows字体数据库在启用DirectWrite3时使用[QWindowsDirectWriteFontDatabase](https://github.com/qt/qtbase/blob/v6.8.0/src/plugins/platforms/windows/qwindowsintegration.cpp#L479)。缺字时[QFontEngineMulti::glyphIndex](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/text/qfontengine.cpp#L1799)遍历候选字体并按需加载/保留引擎，字体数据库populateFamily会创建字体face；这些路径可解释大量字体被触及及缓存保留，是待验证的机制，尚无真实异常分配栈证明具体调用链。仓库未发现订阅更新路径显式枚举/导入全系统字体，UI启动只选择已安装Microsoft YaHei。

[Qt6.8回退列表](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/text/windows/qwindowsdirectwritefontdatabase.cpp#L410)与[Qt6.9](https://github.com/qt/qtbase/blob/v6.9.0/src/gui/text/windows/qwindowsdirectwritefontdatabase.cpp#L344)存在差异：6.9先加familiesForScript；这支持优先调查版本/字体回退兼容性，不证明6.9修复了本缺陷。本机只有6.8.0，未运行6.9，不可混用其他版本DLL冒充对照。

可以确认：真实客户端工作集增长存在，约79%新增驻留页为字体及Windows字体缓存；公开夹具业务处理未出现同量级增长。不能确认：准确触发文字/控件、剩余私有页对象归属、是否持续多次增长或字体缓存平台期、Qt6.9因果。普通Unicode/缺字短测通过不能证明真实配置无问题，私有提交小幅增长也不能证明所有内存合理。

后续修复的验收入口：保留相同客户端/订阅/OS字体和操作顺序，在默认Qt Quick渲染及Qt字体路径上增加字体加载/分配栈证据，优先用同机独立Qt6.8/6.9构建对齐验证，再决定Qt版本或生命周期修复。GDI对照保留为历史诊断，不作为跨平台产品方案；没有将GDI、禁用字体合并或裁剪工作集设为产品默认。

## 字体管理与自有图标优化

订阅YAML提供Unicode文字，不含字体文件/字体注册代码；解析、存储文字不会因字符本身加载字形。渲染节点/组/订阅名称、日志或其他外部文字时，主字体缺字才可能进入字体回退。应用原先导航/测速/展开也用Unicode符号，并非使用了下载的特殊字体。

进一步检查[Qt6.8 qfontdatabase.cpp](https://github.com/qt/qtbase/blob/v6.8.0/src/gui/text/qfontdatabase.cpp)：`QPlatformFontDatabase::fallbacksForFamily`在脚本筛选前遍历字体家族并调用`ensurePopulated()`；外层把应用首选列表与平台回退结果立即拼接。因此`setApplicationFallbackFontFamilies()`只改优先级，不能当白名单或保证跳过系统字体遍历；给QFont设置多家族列表也可能提前构建回退链。没有为节省内存全局设置NoFontMerging，以免任意语言/Emoji变成缺字。上述源码支持机制假设，仍没有真实异常调用栈。

管理弹窗夹具：在现有公开58节点/12001规则场景更新前调用真实subscriptions_page.manage，保持中文、旗帜及U+0378，并回首页滚动资源卡。[增量构建](subscription_memory_evidence/build_management_probe.log)退出0；两次均退出0，选中节点/首页可见文本断言为true，仍未复现大增长。阶段峰值十进制MB：

| 修改前字体后端/D3D11 | 更新前专用/工作集/提交 | 有变更新专用/工作集/提交 | 回首页专用/工作集/提交 |
|---|---|---|---|
| 默认DirectWrite | 75.96 / 177.41 / 233.07 | 83.91 / 188.67 / 238.82 | 73.81 / 179.00 / 223.77 |
| GDI | 55.39 / 133.06 / 211.67 | 59.12 / 136.87 / 216.35 | 51.79 / 129.93 / 200.19 |

证据：[默认](subscription_memory_evidence/gui_management_baseline.log)、[GDI](subscription_memory_evidence/gui_management_gdi.log)。GDI回首页节省约22.02MB专用/49.06MB工作集；该模式仍出现DirectWrite字体face警告，不能简单称为完全绕过DirectWrite。结果仅限公开夹具，不能证明已修复真实477MB字体驻留。

此前矢量版实现（已由用户要求撤回）：新增`qml/components/vector_icon.qml`，用QtQuick.Shapes自有24px路径绘制导航、测速及展开图标；`ActionButton.iconName`复用图标并保留原有工具提示/可访问名称；连接排序箭头也改矢量。宿主传完整QFont到ApplicationWindow，继续使用已安装Microsoft YaHei/系统默认及正常字体回退；首页选中名称明确PlainText，不更改节点ID/原文。默认图形/字体后端不变。

本轮Windows增量构建`quickclash/ui_state_test`及最终`quickclash/quickclash_qmllint`退出0：[首次](subscription_memory_evidence/build_vector_icons.log)、[最终](subscription_memory_evidence/build_vector_final.log)。最终二进制SHA256=`F1FBB841196B02014B649C4C5B29857135AB14682AF9B18420E6F747B5362C46`。qml_smoke/ui_responsive/ui_runtime_lists/ui_state/localization共5/5通过、4.31秒：[CTest](subscription_memory_evidence/ctest_vector_icons.log)。qmllint仍有RuntimeList/模型类型、首页Layout及QtQuick.Dialogs/Overlay等既有代码警告；退出0不等于无警告。此前订阅UI初始卡数量失败未修复或重新归因，不能计为全套GUI测试通过。

修改后使用相同公开夹具各单轮约20.6/20.7秒；默认模式额外在启动时抓取隔离首页截图，故不把与旧版差值全部归因于代码。每阶段值为峰值，不是退出前瞬时值：

| 本轮模式 | 更新前专用/工作集/提交MB | 有变更新专用/工作集/提交MB | 回首页专用/工作集/提交MB |
|---|---|---|---|
| 默认DirectWrite+D3D11 | 55.70 / 130.59 / 210.98 | 59.91 / 134.87 / 220.07 | 66.24 / 164.75 / 213.73 |
| GDI+软件绘制 | 26.05 / 84.53 / 100.40 | 30.17 / 88.81 / 106.14 | 31.12 / 92.21 / 106.03 |

证据：[默认](subscription_memory_evidence/gui_vector_default.log)、[GDI+软件](subscription_memory_evidence/gui_vector_gdi_software.log)，命令/时间/退出码见各自JSON；后者设置进程环境`QT_QUICK_BACKEND=software`并传`-platform windows:fontengine=gdi`。两次均退出0，选中目标节点与首页可见文本断言为true，未变更新不重启、有变更新成功应用，仍有ADMUI3Lg警告，未复现数百MB增长。隔离首页[截图](subscription_memory_evidence/vector_home.png)确认导航路径图标可见、中文及按钮布局正常；截图是启动空首页，不冒充运行后节点字形的视觉验收。

对照的无GUI业务更新后为6.87/31.94/9.40MB，未测Home阶段；GDI+软件前台GUI仍约多24.25MB专用驻留/60.26MB总工作集，不能承诺与无GUI相同。软件绘制可能改变CPU成本、文字/Emoji表现和复杂场景效果，本轮没有CPU/帧/长时或真实订阅验收，默认后端未变。此前可选启动建议现已撤回，只保留测试证据。欲释放整个图形/字体运行时，需评估独立可退出UI进程；现有同进程隐藏卸载无法保证Qt/驱动/字体全回收。

本轮设计生成/-Check、check_progress、check_source_layout（36目录/166文件）、C++格式和git diff --check退出0；不替代产品验收。历史真实异常与111MB私有驻留归属仍未关闭，专项Review、真实修复NotRun，阶段门不改变。

历史说明曾列出GDI及GDI+软件启动步骤；按用户最新跨平台要求，已从[Windows使用说明](../../baseline/windows_user_guide.md)撤下，不作为当前使用方案。没有改默认配置或创建全局环境变量；原始参数/实测保留以便复核历史。

## 按用户要求恢复符号图标

用户判断特殊符号/Emoji不是原因，要求改回原符号而不要自绘。已恢复侧栏/底栏的FeatureDescriptor.glyph、测速闪电、代理组展开三角及连接排序箭头；ActionButton恢复原Text内容，删除VectorIcon源文件及CMake注册。完整QFont传递、首页PlainText及诊断改动保留，未改默认字体/图形后端。

恢复范围与原代码差异已核对：CMake、ActionButton、代理页、连接页均回到本轮图标修改前内容，main.qml仅保留完整字体传递差异。此前矢量图标版短测数字对恢复版Stale，保留日志但不当当前资源证据；没有证据将异常归因于特殊符号或Emoji。本轮为撤回视觉修改，不扩展性能矩阵。

用户关闭旧实例后，Windows增量构建`quickclash/quickclash_qmllint`退出0：[日志](subscription_memory_evidence/build_restore_symbols.log)，既有lint警告保留。qml_smoke/ui_responsive/ui_runtime_lists共3/3通过、4.18秒：[CTest](subscription_memory_evidence/ctest_restore_symbols.log)。恢复版`build/dev/quickclash.exe`SHA256=`BEBC674690B504ED3FDEDE055A949A72D87549860C42FD147B64D841D656E5C6`。

设计生成/-Check、check_progress、check_source_layout（36目录/165文件）、C++格式与diff检查退出0。真实异常修复仍NotRun；未重测资源、不引用旧矢量版数字说明恢复版收益，之前订阅UI失败不自动关闭。

## 跨平台渲染方案纠正

用户明确要求用Qt方式渲染，撤回Windows专有GDI+软件组合的使用建议。已核对app/main.cpp、QML及相关源码：主界面仍为Qt Quick；未设置fontengine=gdi、未强制QT_QUICK_BACKEND=software、未用QQuickWindow::setGraphicsApi锁定图形API。Windows现有basic循环/帧延迟兼容设置位于Q_OS_WIN中且保留外部覆盖，不接管原生绘制；字体通过QFontDatabase/QApplication及QML传递。

字体后端和场景图绘制是两个层次：GDI是Qt Windows字体实现选项，软件适配是Qt Quick自身提供的跨平台能力；默认硬件场景图经RHI使用各平台图形后端。各平台底层仍使用系统能力，跨平台性由Qt抽象实现，不要求所有平台使用同一个原生API。此前把这组Windows对照推进为产品低内存方案不合适，现仅保留历史测试。

本次只修订稳定设计、使用说明及调查下一步，源码和二进制不变；沿用恢复符号图标后的有效构建/3项UI测试，不重跑产品构建。后续按默认Qt Quick路径对齐Qt版本、追踪字体加载与私有页生命周期；真实修复/跨平台原生验收仍NotRun。设计生成/-Check、check_progress、check_source_layout及git diff --check均退出0。

## 切页与Qt软件渲染对照（验证中）

用户新增场景为启动约60MB专用工作集，切换代理/订阅后接近200MB。当前真实PID17520操作后只读映射采样：总工作集786.45MB、私有提交243.10MB、所有映射私有驻留合计193.64MB，其中匿名私有区域188.01MB；163个字体/FontCache映射驻留504.03MB。该次未采启动前数值，不把它与不同PID的历史启动值相减。证据为live_page_17520.json与live_page_sample_17520下10秒采样。只读类别采样仍不能给出私有对象或调用栈归属。

此前订阅探针只显示2成员代理组，且未实际进入代理页，不能代表用户切页场景。补既有runtime_ui_probe中的--page-memory-test：无SettingsService/内核/网络，仅Qt界面接收公开58节点、3组同成员数据，首页→1组58控件→3组174控件→订阅→首页→代理重入→首页；记录20ms事件循环采样峰值/阶段终值、CPU时间、对象数及旧页面QPointer销毁断言。保留原符号、Qt默认字体后端、相同窗口/DPR；只用QT_QUICK_BACKEND=software比较Qt自身的软件场景图，未修改产品默认。实现已具备，构建/短测NotRun。

增量构建首次失败为新探针Windows API结构指针类型，修正后退出0，lint既有警告保留。首次GPU运行退出102：仅遍历QObject子树没有计入Repeater视觉委托，不能当页面缺失/内存泄漏证据；改为去重遍历QObject与QQuickItem两棵树，最终构建退出0。原始失败与修正日志分别为build_page_probe、build_page_probe_fixed、page_gpu、build_page_visual_count。

最终默认GPU与Qt软件各单轮约16.1秒退出0，Qt6.8.0/同字体Microsoft YaHei/1120×760/DPR1.25；rendererInterface分别为D3D11(4)/Software(1)，QSG日志确认NVIDIA RTX4070Ti/basic与Loading backend software。以下为阶段终值，十进制MB：

| 阶段 | GPU专用/工作集/提交 | Qt软件专用/工作集/提交 | 页面对象/节点控件 |
|---|---|---|---|
| 空首页 | 57.25 / 147.53 / 199.18 | 39.43 / 112.24 / 114.58 | 217 / 0 |
| 代理1组58节点 | 79.93 / 170.52 / 228.20 | 47.52 / 120.75 / 122.66 | 1494 / 58 |
| 代理3组174节点控件 | 114.84 / 205.45 / 264.24 | 60.97 / 134.24 / 136.39 | 4162 / 174 |
| 订阅 | 114.11 / 206.39 / 265.28 | 53.05 / 127.91 / 130.35 | 295 / 0 |
| 回首页3秒 | 115.19 / 207.50 / 271.47 | 53.58 / 128.50 / 131.63 | 217 / 0 |
| 代理重入后再回首页 | 115.88 / 208.19 / 272.86 | 54.80 / 129.71 / 133.53 | 217 / 0 |

证据page_gpu_final.log/page_software_final.log及各自JSON。代理委托约23个对象/节点量级，3组会创建重复成员控件；这是可进一步优化的GUI成本，58节点本身未复现193MB专用驻留。回首页对象数恢复且旧页面QPointer为空，支持无旧页面/节点对象残留；不能据此排除Qt堆/JS/字形缓存或驱动内部保留。GPU回首页比软件多61.61MB专用驻留和139.83MB提交，说明后端/文字渲染路径对本场景内存有实质影响，未定位具体缓存调用栈，不把所有差值归为GPU显存或驱动泄漏。

20ms事件循环采样会漏掉同步创建瞬时峰值；阶段CPU时间仅包括观察/滚动窗口，不含此前同步页面创建和进程启动，计时分辨率约15.625ms。软件某些滚动阶段CPU更高，不能宣称帧率、完整操作CPU或长期无增长通过。真实PID的大量字体驻留仍未在公开夹具复现，需相同真实输入的软件模式复测。

据对照增加可选--software-rendering参数，在首个窗口前调用Qt QQuickWindow::setGraphicsApi(Software)，跳过仅D3D11适用的帧延迟设置；无参数默认不变，原QML/符号和字体不变，非Windows也走同一Qt API。设计04/使用说明已同步；此独立修改实现已具备，参数路径构建/验证NotRun。

参数版本构建退出0（build_software_option.log），无QT_QUICK_BACKEND环境变量时运行--page-memory-test --software-rendering成功选择Software(1)，QSG确认Loading backend software；16.1秒退出0，空首页专用38.20MB、3组59.73MB、回首页52.27MB、再重入回首页51.11MB。证据page_software_option.log及JSON；属于新增参数有效性检查，不增加旧环境变量对照的重复统计权重。最终exe SHA256=FEF22A78CD7A9745B9CB0AB8F3CEFA0AB248EB5FFCF3E57048AE66FDA9BFBB01。qml_smoke/ui_responsive/ui_runtime_lists 3/3通过、4.16秒，见ctest_software_option.log。原先订阅UI失败未修改/重测，不宣称全套通过。

隔离软件首页software_option_home.png已核对：原符号导航、中文、按钮和Canvas曲线可见；这是空首页截图，不代表实际运行节点/全部页面视觉验收。没有启动用户订阅、改系统代理或启用TUN；真实软件模式复测待用户打开后只读采样。

真实PID17520的随后10秒EX2采样92条：专用工作集175.68MB、总工作集768.73MB、私有提交243.60MB，各项在该窗口内不变。前述非原子映射采样私有驻留合计193.64MB来自逐页Shared位统计，不能替代EX2专用工作集或混用瞬时值。真实程序在短观察窗保持高占用，不足以证明持续泄漏；本次产品选项已验证，实际异常修复仍NotRun。代理页完全创建各展开组所有成员的成本值得进一步做可见区复用，但尚未修改布局或承诺其单独解决真实增长。

### 用户实际订阅的软件模式复测

用户按说明启动参数版并完成代理→订阅→首页操作，PID8740。只读Win32_Process核对命令行可读且含--software-rendering参数名称，仅输出布尔及参数名、未保存命令行全文；参数路径实际Software选择已由上方隔离测试确认，此附着实例未注入查询QSG API。最初带边界正则的检测返回false，随后子串和参数名提取确认存在，不能把初次检测当未启用软件的证据。D3D11 DLL仍可加载，DLL出现本身不证明Qt场景图使用D3D11。

live_software_8740.json非原子映射采样：工作集689.12MB、提交136.66MB、私有驻留合计127.62MB，163个字体/FontCache映射驻留490.90MB。live_software_sample_8740下10秒92条EX2采样：专用129.34—131.44MB、工作集690.97—693.07MB、提交137.63—139.70MB。与上一默认实例175.68/768.73/243.60MB相比，专用约少44—46MB、工作集约少76—78MB、提交约少104—106MB；来自两次用户操作后的不同进程，后台活动/测量时点未完全锁定，不当严格单变量性能基准。

真实场景说明：Qt软件模式缓解了部分占用，仍超过100MB专用目标；字体驻留约491MB，接近默认约504MB，软件渲染没有解决该部分异常。公开夹具软件回首页约54MB与真实约131MB存在显著差异，不能以公开夹具替代用户验收。需要继续定位真实文本渲染/字体回退触发与Qt内部私有分配，并用同机独立Qt版本构建比较；没有证据将其定为Emoji、订阅解析泄漏或GPU显存问题。当前交付为可选渲染路径与证据补齐，根因修复未关闭。

最终设计生成/-Check、check_progress、check_source_layout（36目录/165文件）、clang-format dry-run与git diff --check退出0；受影响3项UI测试通过。仅文档追加实际结果，不重复构建；无提交、无默认后端切换、无工作集裁剪。
