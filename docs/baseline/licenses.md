# 依赖与许可基线

P0 记录来源和义务；项目自身许可等待真实发布选择。P5-02 完整核对第三方清单、对应源码与包内容。

| 依赖 | 版本/方式 | 许可与分发动作 |
|---|---|---|
| Qt Core/Gui/Network/Qml/Quick/QuickControls2 | 6.8.0，动态链接候选，Basic | 按实际模块 LGPL-3.0/GPL-3.0/商业许可核对；开源发行提供许可、Qt 源码获取及动态替换/调试权利，保留 Qt 内第三方 notices；工具不统一当 LGPL |
| yaml-cpp | 0.9.0 固定 release，静态候选 | MIT，保留版权及完整许可；[LICENSE](https://github.com/jbeder/yaml-cpp/blob/yaml-cpp-0.9.0/LICENSE) 已从校验包读取 |
| Mihomo | v1.19.33，独立官方预编译，未改 | GPL v3；保留 [LICENSE](https://github.com/MetaCubeX/mihomo/blob/v1.19.33/LICENSE)、版本和源码获取；仅附 GitHub 链接不自动证明满足全部源码义务，P2-16/P5-02 核对 |
| CMake/Ninja/LLVM | 4.3.0/1.12.1/21.1.2，开发工具 | 不随客户端打包；分别 BSD-3-Clause、Apache-2.0、Apache-2.0 with LLVM exceptions，实际再分发时保留许可 |
| MSVC/SDK | VS 2026 18.4.1，SDK 10.0.26100.0 | 工具不打包；runtime 按 Microsoft 可再分发许可部署 |
| 用户参考图 | UI-REF-20261023-01 | 仅开发对照，不复制源码/素材入产品；见 [参考台账](../reference/reference_manifest.md) |
| Geo | meta-rules-dat 2026-10-24固定快照，GeoIP/GeoSite dat | 上游GPL-3.0许可原文随包；来源/大小/数据和许可摘要见release_matrix.json。latest地址可变，冻结摘要决定可接受字节；动态更新与完整公开分发源码义务仍待P3-02/P5-02 |

本机 C:/Qt/Licenses/LICENSE 已读取，实际部署仍需按收集的模块与内含组件补全。摘要证明固定字节相符，不是上游签名或完整审计结论。
