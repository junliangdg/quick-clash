# QuickClash

[English](README.md) | [简体中文](README.zh-CN.md)

**一个使用 C++20 与 Qt Quick 构建的跨平台 Mihomo 客户端。**

QuickClash 的目标平台是 Windows、Linux 和 macOS 三个桌面平台，Android/iOS 作为未来宿主平台保留。它使用官方预编译的 Mihomo 提供本地代理能力，把订阅、配置、内核、代理组、连接状态和可用的系统网络控制集中到一个界面中。Qt Quick 应用壳会随窗口宽度调整导航，从完整侧栏切换到底部导航。项目重点关注清晰的状态反馈、响应式布局，以及可预期、可回收的资源生命周期。

> **开发状态**：当前为 `0.1.0` 开发版。Windows 本机 MVP 的 16 项条件已经通过，P3/G3 日常使用验收仍在进行；项目尚未发布稳定版，Linux 和 macOS 也还没有经过原生构建与功能验收。

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/reference/quickclash_minimal_dark.png">
    <img src="docs/reference/quickclash_minimal_light.png" alt="QuickClash 界面设计预览" width="1100">
  </picture>
</p>

<p align="center"><sub>界面设计预览。可打开 <a href="docs/reference/quickclash_minimal_preview.html">离线交互概念页</a> 浏览八个页面和主要控件状态。</sub></p>

## 主要能力

- **订阅与配置**：支持标准 HTTPS 订阅和本地 Mihomo YAML 导入，可修改来源、名称、更新计划和自动应用策略；配置覆盖、历史回退、条件缓存、坏更新保留以及 provider 快照/运行时模式均有对应入口。
- **内核与代理**：随包使用官方 Mihomo，启动前核对版本和摘要，支持就绪检查、受控重启、故障补偿与崩溃退避。代理页可查看组和节点、执行手动/批量测速，并在规则、全局和直连模式之间切换。
- **系统集成（Windows）**：在 Windows 上，当前用户系统代理使用租约记录并在退出、重启和冲突场景中恢复；TUN 通过独立权限服务安装和运行。端口占用、代理冲突、网卡命名和外部 TUN 都有明确的错误与退路。
- **运行可见性**：仪表盘显示实时流量、当前出口、网络工具和运行概况；连接、规则、日志页面提供筛选、行内详情和有界数据保留；客户端与内核的内存指标分栏统计。
- **界面与响应式布局**：提供仪表盘、代理、订阅、连接、规则、日志、测试、设置八个页面，支持浅色/深色/跟随系统主题、简体中文/English/跟随系统语言、键盘操作和 DPI 缩放。同一 Qt Quick 应用壳随宽度从带文字侧栏切换到图标轨，再切换到底部导航；最小窗口为 `360×480`。这些窄窗布局属于窗口自适应，不代表移动端支持。
- **安全与恢复**：在 Windows 上，敏感订阅信息使用当前用户 DPAPI 加密；Linux/macOS 的安全存储与系统集成尚待实现。下载和文件读取有大小上限，配置在官方内核校验通过后才切换；系统代理、设置存储、配置事务和诊断导出均按失败保留旧值处理。

## 当前状态

| 范围 | 状态 |
| --- | --- |
| Windows x64 | 已有原生实现，也是当前具备验收证据的平台；P3/G3 日常使用交付进行中 |
| Windows 本机 MVP | G2 的 16 项本机验收条件均已通过；阶段门仍按[验收清单](docs/progress/CHECKLISTS.md)记录为 `NotRun` |
| Linux / macOS | 规划的桌面目标平台；保留共享边界与候选构建配置，原生构建、系统集成与功能验收均为 `NotRun` |
| 移动端 | 未来 Android/iOS 宿主目标；仅保留架构边界，尚无移动工程、运行时或验收 |
| 发布渠道 | 暂无稳定版；[GitHub Releases](https://github.com/junliangdg/quick-clash/releases) 中的 `continuous` 是默认分支的 CI 快照，不等同于正式发行版 |

窄窗底部导航是桌面窗口的响应式布局，不代表已经支持 Android 或 iOS。

当前仍在补齐真实 TUN、权限服务和干净普通用户环境等验收，并继续调查订阅更新后的字体/内存驻留问题。读屏、完整可访问性以及部分宿主环境场景也尚未形成最终结论。详细状态和证据见[任务台账](docs/progress/TASKS.md)与[验收清单](docs/progress/CHECKLISTS.md)。

## 从源码构建

当前已验证的构建环境为 Windows x64、PowerShell 7.2+、CMake 3.24+、Ninja、Visual Studio 的 MSVC x64 工具链，以及 Qt 6.8 或更新版本。冻结的参考环境为 Qt `6.8.0 msvc2022_64`。Linux 和 macOS 保留候选构建配置，但尚未完成原生构建与功能验收。

在仓库根目录执行：

```powershell
# 获取版本与 SHA-256 已锁定的 yaml-cpp、Mihomo 和 Geo 资源
pwsh -NoProfile -File scripts/fetch_dependencies.ps1

# 配置、构建、运行 QML lint 和受影响测试
pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64

# 启动开发版
build/dev/quickclash.exe
```

首次获取依赖需要网络；已有缓存仍会按摘要复核。更换 Qt 套件、编译器或迁移仓库后，请使用新的构建目录，或向构建脚本显式传入 `-Fresh`。依赖、部署、打包和 CI 的完整说明见[构建与离线包基线](docs/baseline/build_and_package.md)。

文档或仓库结构改动可以单独运行以下检查：

```powershell
pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
pwsh -NoProfile -File scripts/check_platform_boundaries.ps1
git diff --check
```

这些脚本检查设计文档、任务台账、源码命名和平台边界，不代替实际构建、功能测试或性能验证。

## 项目结构

```text
app/        应用宿主、单实例、托盘、探针与服务入口
src/        领域、应用、基础设施、平台适配和表现层
qml/        页面、共享组件与窗口壳
resources/  图标、翻译、元数据和第三方许可
tests/      单元、集成、契约、视觉及脚本测试
docs/       设计、进度、基线和参考稿
scripts/    依赖获取、构建、检查、打包与发布
```

## 文档导航

| 文档 | 内容 |
| --- | --- |
| [设计文档](docs/design/README.md) | 产品范围、总体架构、数据与接口契约、平台策略和验收标准 |
| [开发进度](docs/progress/README.md) | 状态规则、任务台账、阶段清单、开发日志和报告入口 |
| [Windows 使用指南](docs/baseline/windows_user_guide.md) | 当前开发版的操作、恢复和已知限制 |
| [构建与离线包](docs/baseline/build_and_package.md) | 工具链、依赖、构建、测试、部署、打包和 CI |
| [界面设计预览](docs/reference/quickclash_minimal_preview.html) | 可离线打开的交互概念页 |
| [C++ 与源码规范](docs/development/cpp_style.md) | C++20、Qt 生命周期、命名、测试和文档同步约定 |
| [许可基线](docs/baseline/licenses.md) | Qt、Mihomo、Geo、图标及其他依赖的来源和义务 |

## 支持范围与限制

- 输入应为标准 Mihomo YAML；订阅中的 Base64 内容和分享链接不会自动转换。支持字段以随包 `metadata/config_policy.json` 和运行时的能力诊断为准，配置能被内核加载也不代表所有远端节点都可以连通。
- 系统代理作用于当前 Windows 用户的 WinINet 设置；WinHTTP、自行管理代理的软件，以及忽略系统代理的程序不受该开关保证。TUN 的真实网络接管和异常恢复仍以验收台账为准。
- 资源占用受 Qt、显卡驱动、字体、DPI、系统版本和实际负载影响。项目不设置跨机器统一的固定内存门槛，单次短测也不能证明长期 CPU、功耗或内存稳定性。
- Windows 以外的平台目前只有静态边界和候选配置，不能视为已经支持；其中 Linux/macOS 是后续桌面目标，Android/iOS 是未来宿主目标。

## 许可与反馈

QuickClash 以 GNU General Public License v3.0 only（`GPL-3.0-only`）发布，完整条款见 [LICENSE](LICENSE)。随包的 Qt、Mihomo、Geo 等第三方组件仍分别适用其自身许可，通知、源码和分发义务见[许可基线](docs/baseline/licenses.md)。分发二进制时，应按 GPLv3 提供对应的完整源码。

问题与建议请提交到 [GitHub Issues](https://github.com/junliangdg/quick-clash/issues)。反馈前请移除订阅地址、认证信息、节点名称和日志中的其他敏感内容。
