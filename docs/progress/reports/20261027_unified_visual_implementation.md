# 方案C产品统一实施与验证

日期：2026-10-27。任务：P3-16。负责人：Codex。

用户确认将方案C应用到实际控件与八页面，允许按概念预览调整侧栏、仪表盘和卡片，保留已有功能；实时流量改为最近60秒、1Hz、60个点。

当前状态：方案C共享控件、八页面、Light/Dark和首页布局迁移的历史构建/10项回归及26张截图结论保留。2026-10-30按用户选定的第一套替换为圆角线性SVG，Windows Qt6.8增量构建/lint、6/6现有回归与独立部署通过；清Qt PATH的D3D11/125%DPI原生26张截图及两张明暗图标轨核对通过，设置页单轮10秒资源采样完成。当前导航证据取代相关旧符号视觉证据（旧图标截图Stale），未受影响的历史证据保持适用；具体日志/采样范围见[导航图标记录](../DEVLOG.md#2026-10-30--导航圆角线性图标)。设计预览不作为产品验收证据，G1及真实OS可访问性待补条件保持原状态。

## 设计基准

设置页原型数据入口清理Pass（设置页定向范围）：删除回放卡片及三条专用翻译，保留开发回放；Windows增量构建/lint及六项现有UI/状态/本地化回归通过。ui_prototype在修改前后均出现ReplayList孵化/销毁失败，G1-C仍Stale；旧设置页含原型卡片截图对此展示Stale，本轮未新增原生截图或资源测量。命令、产物及对照见[验收记录](../DEVLOG.md#2026-11-01--设置页移除原型数据入口)。

2026-11-01复选框主题统一Pass（样式定向范围）：设置页两个登录选项复用ThemedCheckBox，现有滑动开关保留；Windows Qt6.12增量构建/lint及4项现有UI回归通过，旧登录选项默认样式截图Stale。本轮未新增原生截图或资源测量；命令、产物和限制见[验收记录](../DEVLOG.md#2026-11-01--复选框主题统一)，不提升G1整门。

2026-10-30灵猫Q品牌及独立收展交付Pass：深海蓝/冰青猫头Q、窗口/EXE九档ICO及明暗单色托盘资源同源，品牌不承担点击，面板按钮独立控制宽窗和图标轨。Windows Qt6.8增量构建/lint、7/7现有回归及居中补正4/4、D3D11/125%DPI原生明暗/宽窄截图、188项完整包和清开发环境导入/修复检查通过；最终EXE内9档PNG与源ICO逐字节相等。旧紫底Q及顶部交互相关视觉由当前证据替代。离线预览/静态图同步、单轮10秒资源完成，细节见[本轮记录](../DEVLOG.md#2026-10-30--灵猫q品牌与侧栏收展)。额外原生tray-test在实验性卸载阶段出现proxy_summary/theme销毁期QML错误，修改前后部署包同样exit11，G1-C记Stale并待定位；不将品牌交付当完整托盘生命周期Pass或真实系统明暗切换实测。

方案C概念预览于2026-10-27核对：八页面/Light/Dark、系统跟随、字段/筛选及弹层焦点；6档宽度×2主题×4代表页共48项无溢出，74组对比度检查通过。只覆盖浏览器预览，不替代Qt产品验收；稳定视觉规范和概念图在设计17。早期PowerShell5.1误读UTF-8改用pwsh后通过，对比度/分数像素断点问题修正后重验。

## 1. 实现范围

- ThemeTokens统一中性底面、蓝紫强调、文字层级、圆角10/7、间距16及状态色；共享按钮、输入/多行输入、开关/复选、下拉/数字、进度、提示、对话框、焦点和状态底面覆盖八页面。
- 侧栏1000—1199宽192、至少1200宽208，600—999为64宽图标轨，低于600为仪表盘/代理/订阅/设置四个底部入口；设置中的更多页面和次级返回保留。默认1000×640，最小360×480。
- 导航注册使用本地圆角线性SVG，侧栏/图标轨/底栏共享；24×24画布、20px显示与1.75描边，Qt控件icon统一着色，无图标字体或远程资源。历史符号截图仅保留原版本记录；当前核对见上述DEVLOG。
- 页头右上角显示正常/异常及真实过渡阶段；错误详情持续可见。“连接与系统”移除重复就绪，保留版本、运行时间和三项资源信息。窄窗主题选择从设置进入。
- 当前订阅与流量卡同行等高，统一标题图标和12px辅助文字；订阅名称20px、已激活标记、用量/进度和更新/到期信息。流量图例使用固定色圆点，速率24px半粗、单位12px。
- UiState固定保留60点、约1Hz最近60秒；75样本滚动覆盖、去重、切页/主题保留、重新申请需求继续以及隐藏/代次清零已测。Canvas线性连点防过冲，仅数据/外观/尺寸变化重绘。
- 页面继续使用原业务意图、模型、虚拟化与需求生命周期；本次没有重写订阅、节点选择、网络/端口等业务协议。订阅探针改用QQuickItem视觉树遍历，适配Qt6.9复用委托不在QObject后代树中的情况，产品委托所有权不变。

## 2. 构建与验证

环境：本机Windows x64、Qt6.9.0 msvc2022_64、Visual Studio 18 Community / MSVC14.51、Release。原生截图DPR1.25，图形API枚举4（Direct3D11），字体由宿主完整QFont注入，Microsoft YaHei及系统回退。工作区未提交，产品包dist未重新发布。

最终exe SHA256：339a5f945b2958e3428242ab9a4aacc51bfb2731281a96ef1ebbe27132d02028。

以下保留当时的命令、结果与关键证据，重复构建/测试日志已清理。原生探针当时检查26张截图，现精选4张；captures.json保留原始几何/状态及历史文件名，不表示全部图片仍随仓库提供。

| 检查 | 实际结果 | 证据 |
|---|---|---|
| Windows增量构建quickclash/ui_state_test/localization_test | Pass，最终链接退出0 | 本页构建命令及exe摘要 |
| localization、ui_state、qml_smoke、ui_keyboard、ui_visual、ui_responsive、ui_runtime_lists、ui_subscriptions、ui_subscriptions_zh、host_lifecycle | 10/10 Pass，18.67秒 | [JUnit](evidence/visual_tests.xml) |
| 最终数字/单位间距及圆点Layout约束修正 | qml_smoke/ui_visual/ui_responsive 3/3 Pass，7.46秒 | 本页筛选命令与当时结果 |
| 原生Qt Quick界面 | Pass，26张截图；八页面×两主题、菜单/确认/错误态、360/800/1000/1200宽度 | [captures.json](evidence/visual_captures.json)及下方4张代表图 |
| QML静态检查 | 退出0；保留6条既有模型元数据/var函数/Loader类型警告，新增布局警告已修正 | 本页保留当时结果；无原始日志 |
| 两份测量脚本 | PowerShell AST语法Pass；仓库内无旧预算字段消费者，取消绝对字节失败条件 | 代码diff；未按新策略重新运行资源采集 |
| 设计/预览生成及一致性、进度/源码命名、C++格式和diff | Pass；19章设计生成/-Check、预览生成/--check、进度与源码布局检查通过（178文件），受影响C++格式及git diff --check通过 | 对应脚本退出0；不代替产品测试验收 |

构建调用仓库本机开发环境，等价命令为cmake --build build/dev --target quickclash ui_state_test localization_test --parallel 4。测试使用ctest --test-dir build/dev --output-on-failure并按上述名称筛选；CTest UI环境为offscreen/software，仅用于行为断言。另用默认原生GPU启动quickclash.exe --visual-test --probe-language=zh-CN --visual-output=…/native，独立核对实际曝光、根内容尺寸、截图像素、主题颜色和菜单/模态层真实可见。

原生探针同时核对端口输入17990和网址草稿在主题变化后保留、页面/引擎不重建、主按钮文字前景正确、订阅与流量卡等高、60点历史以及错误→正常状态恢复。截图是公开合成界面数据，不访问用户订阅、启动内核或修改系统代理。

实际截图：[Light首页](evidence/light_home.png)、[Dark首页](evidence/dark_home.png)、[窄窗](evidence/dark_home_360.png)、[异常态](evidence/dark_runtime_error.png)。

验证过程中修正了QML onAccent命名被识别为信号处理器而未得到颜色的问题，统一改为accentForeground；修正重复font赋值/绑定循环及订阅探针遍历。原生探针隐藏启动后须等待窗口实际曝光，否则根内容/遮罩尺寸为零，属于探针取证问题；已加曝光及几何断言。末次链接曾因用户普通实例占用产物报LNK1104，用户正常退出后重试通过。初始失败原因保留在本报告，原始日志已移出版本管理，未以最终Pass覆盖中间失败事实。

## 3. 资源测量与策略

资源短测：Ready首页10秒、91条样本，客户端专用工作集峰值115.45 MB，工作集215.75 MB、私有提交153.57 MB；内核专用8.59 MB、工作集29.45 MB、私有提交66.51 MB，正常退出且owned内核已回收。原始证据见[资源摘要](evidence/visual_memory.json)。运行时采用旧100MB门槛，摘要Fail及退出1原样保留；用户随后明确移除硬门槛，当前结论仅为完成采样，不重新标成内存优化Pass。

资源策略已同步设计1.11、开发约定、使用说明与当前验收：客户端持续追求小内存占用，Qt Quick保持默认RHI/GPU渲染，不设跨机器统一的固定MB门槛。Windows继续记录三项指标，必要客户端辅助进程计入、内核单列，以同环境基线及有界生命周期评价。测量脚本取消绝对数值判失败，使用Sampled表示采集完成；采集/API/执行/生命周期错误仍失败。脚本语法与引用核对Pass；不重跑资源场景来改写历史结果，不以策略调整关闭既有字体/内存调查。

历史测量调用bench/measure_runtime_windows.ps1，参数为-Executable build/dev/quickclash.exe、-Seconds 10；旧输出目录见[来源清单](evidence/evidence_manifest.json)。后续采样的-OutputDirectory使用build/measurements/下的新目录。对应exe摘要db903b09c050c83afbb38a473c762759158b25165e2cee8fbe0b8f1e4f029ad9，早于最后的图标/状态/间距补充；该数字是本轮过程样本，不是最终构建内存基线或优化收益。没有辅助客户端，官方owned核心单列；[cleanup.json](evidence/visual_cleanup.json)确认客户端退出0、owned内核已回收。

## 4. 结论边界与接续

产品视觉迁移范围可验收；不将本轮检查提升为G1/G3整阶段Pass。2026-10-29 P3-27补验如下：已补实际控件名称、字体注入及键盘差异，OS文本设置、多屏/DPI迁移和屏幕阅读器仍NotRun；全控件状态组合、所有真实网络/服务/TUN链路、长时/帧/GPU专项未由本报告覆盖。旧字体驻留调查仍Review/修复NotRun，不因本次短测下降或移除数字门槛关闭。

## P3字体与可访问性补验

2026-10-29，P3-27 / Codex / Review。Windows x64 Release、Qt6.8.0、Microsoft YaHei，单屏M27T6S/DPR1.25，默认RHI的Direct3D11（API枚举4）。工作区未提交，最终exe SHA256为F581A251FA00F569FCAD2D803619CC59A5664E9F03DF4E28CDB7AAA5FA610AF5；未更新安装包或安装目录。详细失败、修复与命令见[DEVLOG](../DEVLOG.md#2026-10-29--p3字体与可访问性)，原始输出在忽略的build目录。

| 条件 | 结果 | 依据与限制 |
|---|---|---|
| Windows文本缩放接入及共享控件 | Pass（实现/字体注入） | 只读TextScaleFactor、设置通知更新字体，字号/控件行高、组标题、数字框与端口换列同步；14/21/32逻辑px×360/1000宽八路由无QML错误；字体变化不重建引擎，卸载后按当前字体重建。未修改OS设置。 |
| 键盘、控件名称与模态 | Pass（探针范围） | 最终13/13受影响回归28.51秒；原生探针4.661秒退出0。711条名称记录（含重复场景，非711个唯一控件）覆盖580按钮/开关等、75下拉、12数字框、7多行及37单行输入；Tab/Shift+Tab、编辑器Ctrl+Tab退出、箭头/空格、Escape、Enter默认Cancel、焦点返回及只读预览不逃出模态通过。 |
| 原生字体/代表布局 | Pass（单屏125%DPI） | 8张设置/代理/订阅截图核对；双主题、三档字体、长中英名称换行/省略及滚动；组标题重叠修复。build/p3_accessibility_complete保留截图、results.json与日志；不复制批量图片至reports。 |
| 资源短测 | Sampled | 本轮早期等效场景单轮10.016秒，专用工作集/总工作集/私有提交字节96600064/195112960/241266688→77705216/176226304/213762048；无核心和本探针辅助进程，既有空闲服务未参与且未聚合。末次名称/键盘和局部几何修复不重复采样，无同版本改前基线，不宣称优化收益、峰值或长期有界。原始build/p3_accessibility_native_2/results.json。 |
| 真实Windows文本设置、高DPI/多屏迁移、读屏 | NotRun | 仅单屏125%DPI，字号为应用字体注入；QAccessible名称查询不等于Windows读屏实际结果。2026-10-30接续尝试真实设置窗口时Computer Use应用授权超时，未改系统值；用户暂无额外环境，待相应环境补验。 |
| G1整门 | Stale | G1-A/C/D/E有效历史边界保留；G1-B尚缺上述真实环境条件，P3-27 Review，G2/G3仍NotRun。 |

最终行为测试为ctest --test-dir build/ci_qt680 --output-on-failure并筛选localization/ui_state/qml_smoke/ui_keyboard/ui_accessibility及_zh/ui_visual/ui_responsive/ui_runtime_lists及_zh/ui_subscriptions及_zh/host_lifecycle；JUnit与命令日志在build/p3_accessibility_final_tests。原生调用quickclash.exe --accessibility-test --probe-language=zh-CN --accessibility-output=…/build/p3_accessibility_complete。早期名称审计曾因匹配QML类名而空跑或仅覆盖ItemDelegate，相关名称证据Stale；最终按Qt控件基类并强制5个代表objectName校验，真实发现并补齐自定义按钮/开关的缺失名称。标准Cancel曾有焦点但Return未激活，已修共享按钮；保留失败事实，不以最终Pass覆盖。
