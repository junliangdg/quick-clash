# QuickClash 界面参考与交互示意

当前界面基准为**极简桌面工具风**。完整设计以[17统一视觉系统](../design/17-界面参考与QtQuick落地.md)为准；本目录保存实际 Qt 界面的代表截图与可编辑的离线交互示意。根目录英文 README 使用英文界面截图，中文 README 和设计正文使用中文截图，共用一份双语 HTML；页面结构或主要操作变化时须一起同步。

直接用浏览器打开[离线交互示意](quickclash_minimal_preview.html)，默认英文、浅色；页首或通用设置可切换 English / 简体中文，也可切换 Light、Dark、跟随系统，浏览八个页面与“控件与状态”。语言切换保留当前页面、主题和字段草稿，覆盖静态标签、弹窗与动态演示反馈。首页使用当前订阅、实时流量、代理与网络、网络工具、运行概况五卡；代理包含搜索、定位、组测速和独立节点测速；规则包含自定义规则工作区及资源更新视图；设置使用通用、网络、内核与资源、数据与诊断、关于五类页签。窄窗使用仪表盘/代理/订阅/设置四个底部图标入口，其他页面由页头“其他”进入。

HTML 用于检查信息结构与代表性交互，浏览器原生控件、字体、滚动条和简化表单与 Qt 实现会有差异，不承诺逐像素一致或覆盖全部业务状态。实际外观以 Qt 截图和程序为准，交互及失败处理以设计契约为准。

展开侧栏的主导航采用15px文字，普通态Medium（500）与正文色、选中态DemiBold（600）与紫色文字及浅选中底色；底部设置入口同规格。普通图标保持20px与次要灰色，行高40、条目间距4。产品字号和尺寸随宿主文字比例缩放，HTML展示基准比例；图标轨和窄窗底栏保留纯图标形式。

“关于→使用须知”展示产品中英文正文及Mihomo（原Clash.Meta）命名；HTML仅供主动查看，不模拟产品首次启动的确认持久化。现有首页截图对此入口无覆盖，须知外观以产品为准。

| 文件 | 用途 |
|---|---|
| [quickclash_minimal_preview.fragment.html](quickclash_minimal_preview.fragment.html) | 可编辑的 HTML/CSS/JS 源；本地图标占位由生成脚本嵌入 SVG |
| [quickclash_preview_translations.json](quickclash_preview_translations.json) | 单份中英词表，含模板参数；生成英文初始内容，并由离线脚本切换语言 |
| [quickclash_minimal_preview.html](quickclash_minimal_preview.html) | 自包含离线页面，可直接打开，不依赖服务或外部资源 |
| [中文 Light 首页](quickclash_minimal_light.png) / [中文 Dark 首页](quickclash_minimal_dark.png) | 中文 README 与设计正文使用的宽窗截图；文件名保留以维持引用 |
| [English Light dashboard](quickclash_minimal_light_en.png) / [English Dark dashboard](quickclash_minimal_dark_en.png) | 英文 README 使用的宽窗截图 |
| [中文窄窗](quickclash_minimal_narrow_zh_cn.png) / [English narrow window](quickclash_minimal_narrow_en.png) | 对应 README 使用的深色桌面窄窗截图，展示卡片纵排与底部导航 |
| [生成脚本](../../scripts/build_ui_preview.py) | 以Python标准库生成离线页面，避免源/导出两份手工维护 |

## 同步维护

先按受影响 QML 页面及第17章核对结构，再修改 fragment 与翻译词表；导出 HTML 由脚本生成，不单独手改。fragment 保留中文文案，生成器输出英文初始 DOM，并内嵌中英词表，运行时共用同一套结构和交互；新增文案须同时补齐英文，模板的 `{0}` 等参数保持一致。主要布局变化同时更新中英文宽窗明暗图、窄窗图、对应图注与本页来源标识。间距或悬停等细小调整可集中同步；截图与 HTML 分别维护，生成 HTML 不会更新 PNG。

修改源后，在仓库根目录运行：

~~~powershell
python scripts/build_ui_preview.py
python scripts/build_ui_preview.py --check
pwsh -NoProfile -File scripts/build_design_docs.ps1
pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
~~~

图像可使用既有测试构建的隔离 `--visual-test --reference-only --probe-language=en-US --visual-output=<build下目录>` 入口生成，中文使用 `--probe-language=zh-CN`。`--reference-only`仅导出宽窗明暗首页和深色窄窗三张代表图，不执行完整视觉回归矩阵；核看各轮 `light_home.png`、`dark_home.png`、`dark_home_360.png` 及 `captures.json` 后，只精选 README 所需图复制到本目录。完整输出留在忽略的 build 下，禁止使用真实用户订阅或凭据截图。图像仅反映记录的渲染环境，不能替代原生 GPU、系统集成或资源验收。

Windows离屏导图须设置 `QT_QPA_PLATFORM=offscreen`、`QT_QUICK_BACKEND=software`、`QT_QPA_FONTDIR=C:/Windows/Fonts` 和 `QT_SCALE_FACTOR=1`，字体仅从本机已安装目录读取，不复制入库。进程成功退出不代表文字已经正确渲染；替换素材前须人工核看字体、布局和侧栏状态。

## 当前素材来源

- 同步日期：2026-11-07；对照当前工作区的 `qml/features`、`qml/components` 与设计正文，包含尚未提交的界面改动。
- 六张 PNG 来自同一 `0.1.0` 开发构建，Qt 6.12.0、DPR 1，offscreen/software 隔离视觉探针生成，无真实业务或网络接管。宽窗为 1000×640，窄窗为 360×640 逻辑像素；均只展示首页顶部视口，下方卡片需滚动查看。窄窗是桌面布局，不表示移动平台已实现。
- 英文图使用 `en-US` 界面；订阅、策略组和节点名称来自合成用户数据，不随界面语言翻译。中文图使用 `zh-CN` 界面。
- 截图源程序 SHA-256：`f819920dc863aaab8f60af9f74d196697789c4b4524dbd736cb0a0aa4f63169f`。中文原始输出位于忽略的 `build/navigation_typography_20261107/zh_fonts/`，英文位于 `build/navigation_typography_20261107/en_fonts/`；均通过`--reference-only`生成，包含15px主导航文字与500/600字重。长期保留的是本目录六张精选图及此来源说明。
- HTML 按相同页面结构独立编排，所有交互只改变当前文档；网页重新打开后回到英文初始样例，不持久化语言或表单。

预览使用公开假数据和本地交互。节点、域名、用量、时间及测试结果均为演示；不访问网络，不读取用户配置，不修改系统代理/端口/文件，不作为产品默认值、真实功能或资源验收证据。复制按钮仅显示反馈，不改写系统剪贴板。预览默认浅色，产品默认跟随系统，两者用途不同。

页面使用自有结构、CSS 和脚本、系统字体及 Unicode 辅助符号；导航、首页和站点标记使用与产品同源的本地 SVG，侧栏使用灵猫Q单色品牌。生成脚本直接嵌入资源，预览支持宽窗收窄/图标轨展开；无 UI 框架、图标字体、远程 CDN 或下载字体依赖，CSP 禁止网络连接。图标版本与许可见[许可台账](../baseline/licenses.md)。Linear 与 shadcn/ui 仅提供理念参考，未复制其代码、品牌或素材。产品落地仍为 C++20 与 Qt Quick；Windows 多尺寸 ICO 生成说明见[构建基线](../baseline/build_and_package.md)。
