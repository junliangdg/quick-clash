# 依赖与许可基线

P0 记录来源和义务；项目自身许可等待真实发布选择。P5-02 完整核对第三方清单、对应源码与包内容。

| 依赖 | 版本/方式 | 许可与分发动作 |
|---|---|---|
| Qt Core/Gui/Network/Qml/Quick/QuickControls2 | 6.8.0，动态链接候选，Basic | 按实际模块 LGPL-3.0/GPL-3.0/商业许可核对；开源发行提供许可、Qt 源码获取及动态替换/调试权利，保留 Qt 内第三方 notices；工具不统一当 LGPL |
| yaml-cpp | 0.9.0 固定 release，静态候选 | MIT，保留版权及完整许可；[LICENSE](https://github.com/jbeder/yaml-cpp/blob/yaml-cpp-0.9.0/LICENSE) 已从校验包读取 |
| Qt Svg | 6.8.0，动态链接；本地图标SVG解码 | 随Qt插件部署，按实际Qt Svg模块许可保留源代码获取与动态替换义务；不在headless引入GUI依赖 |
| Lucide导航/收展/仪表盘图标 | 1.8.0，本地24个SVG子集（八导航、两个收展、14个仪表盘图标）；24×24画布，描边调整为1.75 | [上游](https://github.com/lucide-icons/lucide/tree/1.8.0)，导航/收展从本机固定版本包提取，仪表盘来自同标签icons目录；ISC及包内Feather MIT说明完整保留于[许可原文](../../resources/licenses/lucide_isc.txt)，安装到licenses/；不携带JS图标库或图标字体 |
| Simple Icons网站图标 | 16.0.0，Google/YouTube/GitHub三个原始SVG，运行时按主题着色 | [固定来源](https://github.com/simple-icons/simple-icons/tree/16.0.0/icons)，CC0-1.0完整原文保留于[许可文件](../../resources/licenses/simple_icons_cc0.txt)，安装到licenses/；仅用于识别测试目标，不表示关联或背书，商标权不由CC0授予 |
| 灵猫Q品牌 | QuickClash自有SVG、明暗托盘SVG及多尺寸ICO | 按用户选定提案绘制，不使用Qt或其他Clash客户端品牌素材；源资源在resources/icons/brand，ICO由开发工具Node/sharp生成，工具不随客户端分发 |
| Mihomo | v1.19.33，独立官方预编译，未改 | GPL v3；保留 [LICENSE](https://github.com/MetaCubeX/mihomo/blob/v1.19.33/LICENSE)、版本和源码获取；仅附 GitHub 链接不自动证明满足全部源码义务，P2-16/P5-02 核对 |
| CMake/Ninja/LLVM | 4.3.0/1.12.1/21.1.2，开发工具 | 不随客户端打包；分别 BSD-3-Clause、Apache-2.0、Apache-2.0 with LLVM exceptions，实际再分发时保留许可 |
| MSVC/SDK | VS 2026 18.4.1，SDK 10.0.26100.0 | 工具不打包；runtime 按 Microsoft 可再分发许可部署 |
| 概念预览 | QuickClash自有HTML/CSS/JS、系统字体及Unicode符号 | 无外部UI框架/字体/图标下载；理念参考与使用边界见[预览说明](../reference/README.md) |
| Geo | meta-rules-dat 2026-10-24固定快照，GeoIP/GeoSite dat | 上游GPL-3.0许可原文随包；来源/大小/数据和许可摘要见release_matrix.json。latest地址可变，冻结摘要决定可接受字节；动态更新与完整公开分发源码义务仍待P3-20/P5-02 |

本机 C:/Qt/Licenses/LICENSE 已读取，实际部署仍需按收集的模块与内含组件补全。摘要证明固定字节相符，不是上游签名或完整审计结论。
