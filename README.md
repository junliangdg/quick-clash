<p align="center">
  <img src="resources/icons/brand/qcat.svg" alt="QuickClash logo" width="96" height="96">
</p>

<h1 align="center">QuickClash</h1>

[English](README.md) | [简体中文](README.zh-CN.md)

**An independent cross-platform proxy client built with Qt Quick and powered by the Mihomo (formerly Clash.Meta) core.**

QuickClash brings subscriptions, proxy groups, routing rules, connections, and network controls into one interface. It uses the official precompiled Mihomo core without modifying it. The interface adapts from a full sidebar to an icon rail and bottom navigation as the window narrows.

> **Currently available as a development preview for Windows x64.** Linux, macOS, and mobile (Android/iOS) support is planned.

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/reference/quickclash_minimal_dark_en.png">
    <img src="docs/reference/quickclash_minimal_light_en.png" alt="QuickClash Qt Quick dashboard in English with synthetic demonstration data" width="1000">
  </picture>
</p>

<p align="center"><sub>Dashboard with demonstration data. Explore the <a href="docs/reference/quickclash_minimal_preview.html">offline interaction preview</a> in English or 简体中文.</sub></p>

<p align="center">
  <img src="docs/reference/quickclash_minimal_narrow_en.png" alt="QuickClash English dashboard in a narrow desktop window, with stacked cards and bottom navigation" width="360">
</p>

<p align="center"><sub>A narrow desktop window in dark theme, with stacked cards and bottom navigation.</sub></p>

## Main capabilities

- **Subscriptions and configuration**: Import HTTP/HTTPS subscriptions, local Mihomo YAML/JSON, or URI/Base64 node lists. Schedule updates, edit configurations, and roll back history. Failed downloads or validation retain the previous configuration.
- **Proxies and strategies**: Search nodes and groups, select a node manually, or use automatic latency selection, failover, and load balancing. Test individual nodes or whole groups without changing manual selections.
- **Custom routing**: Manage global and profile-specific rules, generate rule drafts from connections, and update proxy providers and rule sets.
- **Dashboard and diagnostics**: View live traffic, connections, logs, and resource use; compare IP addresses, check websites, and export diagnostics locally.
- **Windows integration**: System proxy, TUN, login startup, tray controls, and problem notifications. Normal exit restores QuickClash's system proxy changes while preserving later changes made by other software.
- **Core and data management**: Validate configurations with the official core, repair or switch compatible cores, update or roll back Geo resources, and back up local configurations and rules.
- **Appearance**: Light, dark, and system themes; English and Simplified Chinese; keyboard controls and layouts that adapt to window size and DPI.

## Getting started on Windows

Download the Windows x64 portable package from [Releases](https://github.com/junliangdg/quick-clash/releases). The `continuous` packages are unsigned development snapshots.

1. Extract the **entire** ZIP, launch `bin/quickclash.exe`, and read the usage notice. Keep all package directories together; user data is stored separately in the directory shown in Settings.
2. Open **Subscriptions → Add subscription** and enter a subscription URL or import a local file. The name is optional. The first valid profile starts automatically; system proxy and TUN stay off on first use.
3. On **Proxies**, select a member of a manual group, or let an automatic group choose. Use **Test latency** to check nodes. Select Rule, Global, or Direct mode from the dashboard or tray.
4. Use the local HTTP/SOCKS5 proxy address shown on the dashboard, enable **System proxy**, or enable **TUN**. First-time TUN setup requests Windows administrator authorization; ordinary proxy use does not require installing the service.
5. Closing to the tray keeps forwarding active; choose **Exit** to stop the session and restore proxy settings. Minimizing keeps a taskbar entry.

“Core ready” means the local core is running. Use IP queries and website checks to check connectivity through the selected path. See the [Windows user guide](resources/windows_readme.md) for operation, TUN setup, portable updates, and recovery steps.

### Usage notice

QuickClash does not provide remote proxy nodes, accounts, or subscription services. Supply configurations or subscriptions you are authorized to use and check that their sources are trustworthy. Enabling system proxy or TUN changes how this device accesses the network; learn the recovery steps and comply with applicable laws and regulations. Reopen the full notice from **Settings → About → Usage notice**.

### Custom strategies and rules

Use **Proxies → New strategy group** to create automatic latency, failover, or load-balancing groups. Preview candidates, then **Save and apply**. Select the group to use it for routing. Custom groups belong to a subscription entry: updating its source retains them, while deleting the entry removes them.

Use **Rules → Manage custom rules…** to edit global and current-profile rules, then **Confirm** to save and apply. In Rule mode, profile-specific rules run first, followed by global rules and the profile's original rules. **Resource updates…** manages the selected profile's proxy providers and rule sets.

## Known limitations

- **System proxy recovery**: This setting affects applications that follow the current user's Windows proxy settings. After a crash or forced termination, connectivity may be affected until you reopen QuickClash or recover the settings through **Windows Settings → Network & internet → Proxy**. Preserve later changes made by other software.
- **TUN**: Cross-user use, clean standard-user machines, reboot, and network/power changes still need validation.
- **Backups**: Configuration/rule backups can contain plain text credentials and are not password-encrypted. Export fails if an enabled profile-specific `RULE-SET` rule references a runtime provider absent from the offline snapshot.
- **Updates**: Application automatic updating is not available. Core management supports only the bundled compatibility list.
- **Custom groups**: A reported disappearance after changing a subscription source remains under investigation.
- **Resources and accessibility**: Subscription-related memory growth and long-term resource use remain under investigation. Screen readers, physical touch, and OS text scaling are not fully validated.

## Build from source

QuickClash uses C++20 and Qt Quick. On Windows x64, install PowerShell 7.2+, CMake 3.24+, Ninja, an MSVC x64 toolchain/Windows SDK, and a Qt 6.9+ MSVC kit with Qt Quick, Qt SVG, Linguist tools, and Qt Test.

Run these commands from the repository root:

```powershell
# Fetch version- and SHA-256-pinned yaml-cpp, Mihomo, and Geo assets
pwsh -NoProfile -File scripts/fetch_dependencies.ps1

# Adjust QtRoot to your installed kit
pwsh -NoProfile -File scripts/build_windows.ps1 -QtRoot C:/Qt/6.12.0/msvc2022_64

# Launch the development build
.\build\dev\quickclash.exe
```

The default build runs minimum smoke tests.

## License and feedback

QuickClash is released under the GNU General Public License version 3 only (`GPL-3.0-only`). See [LICENSE](LICENSE) for the complete terms. Bundled third-party components such as Qt, Mihomo, and Geo remain under their own licenses. When distributing binaries, provide the corresponding source required by GPLv3.

Please report issues and suggestions through [GitHub Issues](https://github.com/junliangdg/quick-clash/issues). Remove subscription URLs, credentials, node names, and other sensitive details from logs before sharing them.
