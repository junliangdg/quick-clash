# QuickClash

[English](README.md) | [简体中文](README.zh-CN.md)

**A cross-platform Mihomo client built with C++20 and Qt Quick.**

QuickClash is designed for the three desktop platforms Windows, Linux, and macOS; Android and iOS remain future host targets. It uses the official precompiled Mihomo core to provide local proxy capabilities and brings subscriptions, configurations, the core, proxy groups, connection status, and available system network controls into one interface. The Qt Quick shell adapts its navigation to the window width, from a full sidebar to bottom navigation. The project focuses on clear status feedback, responsive layouts, and predictable, reclaimable resource lifecycles.

> **Development status**: The current build is version `0.1.0`. All 16 Windows local MVP conditions have passed, while P3/G3 day-to-day use validation is still in progress. No stable release is available yet, and Linux and macOS have not been natively built or functionally validated.

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/reference/quickclash_minimal_dark.png">
    <img src="docs/reference/quickclash_minimal_light.png" alt="QuickClash UI design preview" width="1100">
  </picture>
</p>

<p align="center"><sub>UI design preview. Open the <a href="docs/reference/quickclash_minimal_preview.html">offline interactive concept page</a> to explore the eight pages and key control states.</sub></p>

## Main capabilities

- **Subscriptions and configuration**: Supports standard HTTPS subscriptions and local Mihomo YAML import. Source, name, update schedule, and automatic-apply policy are editable. Configuration overrides, version rollback, conditional caching, bad-update retention, and provider snapshot/runtime modes each have dedicated entry points.
- **Core and proxies**: Bundles the official precompiled Mihomo core, with version and digest verification before launch, readiness checks, controlled restarts, failure compensation, and crash backoff. The Proxies page shows groups and nodes, runs manual or batch latency tests, and switches between Rule, Global, and Direct modes.
- **System integration (Windows)**: On Windows, current-user system proxy settings are tracked with a lease and restored on exit, restart, and conflict. TUN is installed and run through a dedicated privileged service. Port conflicts, proxy conflicts, adapter naming, and external TUN scenarios all have explicit errors and fallback paths.
- **Runtime visibility**: The dashboard shows live traffic, the current egress, network tools, and runtime status. Connections, Rules, and Logs provide filtering, inline details, and bounded retention; client and core memory are reported separately.
- **Interface and responsive layout**: Eight pages: Dashboard, Proxies, Subscriptions, Connections, Rules, Logs, Tests, and Settings. The app supports light, dark, and system themes; Simplified Chinese, English, and system language; keyboard navigation; and DPI scaling. The same Qt Quick shell switches from a labeled sidebar to an icon rail and then to bottom navigation as width decreases; the minimum window size is `360×480`. This compact layout is window adaptation, not mobile support.
- **Security and recovery**: On Windows, sensitive subscription data is encrypted with the current user's DPAPI; secure storage and system integration for Linux and macOS remain to be implemented. Downloads and file reads are size-limited, and configurations are activated only after validation by the official core. System proxy, settings storage, configuration transactions, and diagnostic export all preserve previous values on failure.

## Current status

| Scope | Status |
| --- | --- |
| Windows x64 | Native implementation and current validation platform; P3/G3 day-to-day delivery is in progress |
| Windows local MVP | All 16 G2 local validation conditions pass; the stage gate remains `NotRun` in the [validation checklist](docs/progress/CHECKLISTS.md) |
| Linux / macOS | Planned desktop targets; shared boundaries and candidate build configuration are retained; native builds, system integration, and functional validation are all `NotRun` |
| Mobile | Future Android and iOS host targets; architecture boundaries only; no mobile project, runtime, or validation |
| Distribution | No stable release yet; `continuous` in [GitHub Releases](https://github.com/junliangdg/quick-clash/releases) is a CI snapshot of the default branch, not a formal release |

Compact bottom navigation is a responsive desktop window layout. It does not indicate Android or iOS support.

Validation is still pending for real TUN operation, the privileged service, and a clean standard-user environment. We are also investigating font and memory residency issues after subscription updates. Screen-reader support, full accessibility, and some host-environment scenarios do not yet have final conclusions. See the [task ledger](docs/progress/TASKS.md) and [validation checklist](docs/progress/CHECKLISTS.md) for current status and evidence.

## Build from source

The currently validated build environment is Windows x64, PowerShell 7.2+, CMake 3.24+, Ninja, the Visual Studio MSVC x64 toolchain, and Qt 6.8 or newer. The frozen reference environment is Qt `6.8.0 msvc2022_64`. Linux and macOS retain candidate build configuration, but native build and functional validation have not been completed.

Run these commands from the repository root:

```powershell
# Fetch version- and SHA-256-pinned yaml-cpp, Mihomo, and Geo assets
pwsh -NoProfile -File scripts/fetch_dependencies.ps1

# Configure, build, run QML lint, and run affected tests
pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.8.0/msvc2022_64

# Launch the development build
build/dev/quickclash.exe
```

Network access is required for the initial dependency fetch; cached assets are still verified against their digests. After changing Qt kits or compilers, or moving the repository, use a fresh build directory or pass `-Fresh` to the build script. See [Build and offline packaging](docs/baseline/build_and_package.md) for dependencies, deployment, packaging, and CI.

Documentation or repository-structure changes can run these checks independently:

```powershell
pwsh -NoProfile -File scripts/build_design_docs.ps1 -Check
pwsh -NoProfile -File scripts/check_progress.ps1
pwsh -NoProfile -File scripts/check_source_layout.ps1
pwsh -NoProfile -File scripts/check_platform_boundaries.ps1
git diff --check
```

These scripts validate design documents, task tracking, source naming, and platform boundaries. They do not replace real builds, functional tests, or performance validation.

## Project layout

```text
app/        Application host, single-instance handling, tray, probes, and service entry points
src/        Domain, application, infrastructure, platform adapters, and presentation
qml/        Pages, shared components, and window shell
resources/  Icons, translations, metadata, and third-party licenses
tests/      Unit, integration, contract, visual, and script tests
docs/       Design, progress, baseline, and reference material
scripts/    Dependency fetching, builds, checks, packaging, and releases
```

## Documentation

| Document | Contents |
| --- | --- |
| [Design documents](docs/design/README.md) | Product scope, architecture, data and interface contracts, platform strategy, and acceptance criteria |
| [Development progress](docs/progress/README.md) | Status rules, task ledger, stage checklists, development log, and report index |
| [Windows user guide](docs/baseline/windows_user_guide.md) | Operation, recovery, and known limitations of the current development build |
| [Build and offline packaging](docs/baseline/build_and_package.md) | Toolchain, dependencies, builds, tests, deployment, packaging, and CI |
| [UI design preview](docs/reference/quickclash_minimal_preview.html) | Offline interactive concept page |
| [C++ and source conventions](docs/development/cpp_style.md) | C++20, Qt lifetime management, naming, testing, and documentation synchronization |
| [License baseline](docs/baseline/licenses.md) | Sources and obligations for Qt, Mihomo, Geo, icons, and other dependencies |

## Support scope and limitations

- Input must be standard Mihomo YAML; Base64 subscription payloads and share links are not converted automatically. Supported fields are defined by the bundled `metadata/config_policy.json` and runtime capability diagnostics. The core accepting a configuration does not guarantee that every remote node is reachable.
- The system proxy applies to WinINet settings for the current Windows user. WinHTTP, software that manages its own proxy, and programs that ignore the system proxy are not covered by this switch. Real network takeover and failure recovery for TUN remain subject to the validation ledger.
- Resource use varies with Qt, graphics drivers, fonts, DPI, OS version, and actual workload. The project does not impose a fixed cross-machine memory threshold, and a single short test cannot prove long-term CPU, power, or memory stability.
- Platforms other than Windows currently have only static boundaries and candidate configurations. They should not be considered supported. This includes Linux and macOS; Android and iOS are future host targets, not current mobile support.

## License and feedback

QuickClash is released under the GNU General Public License version 3 only (`GPL-3.0-only`). See [LICENSE](LICENSE) for the complete terms. Bundled third-party components such as Qt, Mihomo, and Geo remain under their own licenses; their notices and source/distribution obligations are documented in the [license baseline](docs/baseline/licenses.md). When distributing binaries, provide the corresponding source required by GPLv3.

Please report issues and suggestions through [GitHub Issues](https://github.com/junliangdg/quick-clash/issues). Remove subscription URLs, credentials, node names, and other sensitive details from logs before sharing them.
