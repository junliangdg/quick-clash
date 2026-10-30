# QuickClash 方案 C 概念预览

当前界面基准为**方案 C：极简桌面工具风**。完整设计以[17统一视觉系统](../design/17-界面参考与QtQuick落地.md)为准；本目录只保存该方案的可编辑预览及离线入口。

直接用浏览器打开[概念预览](quickclash_minimal_preview.html)，可切换Light、Dark、跟随系统，浏览八个页面，并通过“控件与状态”查看按钮、输入/下拉、开关/选项、选择/悬停/按下/焦点、禁用/处理中、错误、菜单及确认弹层。窄窗使用仪表盘/代理/订阅/设置四个底部图标入口，其他页面经设置进入。

| 文件 | 用途 |
|---|---|
| [quickclash_minimal_preview.fragment.html](quickclash_minimal_preview.fragment.html) | 可编辑的HTML/CSS/JS源；导航图标占位由生成脚本嵌入本地SVG |
| [quickclash_minimal_preview.html](quickclash_minimal_preview.html) | 自包含离线页面，可直接打开，不依赖服务或外部资源 |
| [Light概念图](quickclash_minimal_light.png) / [Dark概念图](quickclash_minimal_dark.png) | 同一首页的明暗主题静态预览，嵌入设计正文；布局更新时同步重新截取 |
| [生成脚本](../../scripts/build_ui_preview.py) | 以Python标准库生成离线页面，避免源/导出两份手工维护 |

修改源后，在仓库根目录运行：

~~~powershell
python scripts/build_ui_preview.py
python scripts/build_ui_preview.py --check
~~~

预览使用公开假数据和本地交互。节点、域名、用量、时间及测试结果均为演示；不访问网络，不读取用户配置，不修改系统代理/端口/文件，不作为产品默认值、真实功能或资源验收证据。菜单复制仅显示反馈，不改写系统剪贴板。预览默认浅色，产品默认跟随系统，两者用途不同。

页面使用自有结构、CSS和脚本、系统字体及Unicode辅助符号；导航及独立收展按钮使用与产品同源的十个Lucide圆角线性SVG，窗口和侧栏使用灵猫Q品牌。生成脚本直接嵌入SVG及品牌data URI，预览支持宽窗收窄/图标轨展开；无UI框架、图标字体、远程CDN或下载字体依赖，CSP禁止网络连接。图标版本与许可见[许可台账](../baseline/licenses.md)。Linear与shadcn/ui仅提供理念参考，未复制其代码、品牌或素材。产品落地仍为C++20与Qt Quick，导航通过本地SVG及Qt图标着色实现；Windows多尺寸ICO生成说明见[构建基线](../baseline/build_and_package.md)。
