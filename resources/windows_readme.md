# QuickClash for Windows x64

[English](windows_readme.md) | [简体中文](windows_readme_zh_cn.md)

QuickClash is an independent graphical client based on Mihomo (formerly Clash.Meta). It uses the official precompiled core without developing or modifying it, and does not provide remote proxy nodes, node accounts, or subscription services. Supply configurations you are authorized to use from sources you trust. Learn how to stop network interception and recover connectivity before enabling system proxy or TUN, and comply with applicable laws and regulations.

This is an unsigned Windows x64 portable development build. Local Windows usage has been validated in limited scenarios; clean standard-user environments and remaining authorization and system-event scenarios still require validation. The file hashes in `metadata/package_manifest.json` detect changed files; they do not establish a trusted publisher or replace a signature.

## Getting started

1. Extract the **entire ZIP** and run `bin/quickclash.exe`. Keep all extracted directories together, including `bin/`, `core/`, `geo/`, `metadata/`, `licenses/`, and the Qt runtime directories. Do not move the executable on its own.
2. Read the first-launch **Usage notice**. **I understand** saves its version locally; ordinary upgrades do not require another acknowledgment. If saving fails, you can enter the app to recover, and the notice appears again at the next launch. Reopen it from **Settings → About → Usage notice**.
3. Open **Subscriptions** and add an HTTPS subscription URL or import a local Mihomo YAML file. The first valid profile is selected and its core starts automatically. System proxy and TUN stay off on first use. Failed downloads or validation keep the previous configuration.
4. Open **Proxies** to select a manual-group member or use an automatic group. Latency tests do not change manual selections. “Core ready” confirms the local runtime, not public internet access or that every remote node works.
5. Use the ready local HTTP/SOCKS5 address directly, enable **System proxy** for applications that follow Windows proxy settings, or enable **TUN** as described below. Theme, English/Simplified Chinese/system language, and window/tray behavior are available in Settings.

## Stopping and recovering connectivity

Closing to the tray keeps forwarding active. Minimizing retains the taskbar entry. Choose **Exit** from the tray to stop the owned core, clean up TUN, exit the privileged service process, and restore system proxy settings that still belong to QuickClash. Changes made later by other software are preserved. Exit keeps the saved system proxy and TUN preferences, service registration, and directory authorization.

**If the GUI crashes or is forcibly terminated, the system proxy may still point to a stopped local port and affect connectivity.** This applies in both ordinary and service modes. Reopen QuickClash to check and recover its saved proxy lease; it can start this recovery without internet access. Saved proxy/TUN intent may resume after reopening, so explicitly turn off the switches if you want interception to remain off. If reopening is unavailable or recovery fails, inspect **Windows Settings → Network & internet → Proxy**. Change only the manual proxy entry you can identify as QuickClash's local endpoint, preserving any settings subsequently changed by other software. A running service does not guarantee immediate system proxy recovery after GUI failure.

For a port conflict, open **Settings → Network** and edit the ports or use **Use available ports…** to fill the draft, then confirm with **Apply network settings…**. QuickClash does not silently change saved ports. Applying changes while the core runs can interrupt connections.

## TUN, service authorization, and portable updates

TUN requires the privileged service; ordinary system proxy use does not. The first TUN request prompts for Windows administrator approval to install the service from this package or authorize its current directory. Approval continues the TUN request; cancellation keeps ordinary mode. The GUI stays at normal user privilege. Service setup uses the executable's fixed actions and does not require PowerShell.

The service trusts the authorized location of `bin/quickclash.exe` and binds requests to the active console session and process identity. That trust also covers service files, runtime libraries, and later updates in the directory; it does not prove their contents are authentic. Use a trusted package in a directory that Everyone/Anonymous cannot write to. Moving or renaming the portable directory requires authorization again. The supported scope is a single active console user; remote desktop and cross-user operation have not been validated.

The service starts on demand and exits when QuickClash actually exits. On the next manual launch or login autostart, the app follows the saved switch preference: it attempts to restore TUN if previously enabled and keeps it off if explicitly disabled. The same authorized directory needs no further UAC prompt. Keeping the service registered does not enable TUN on its own at boot. QuickClash avoids conflicting adapter names; another VPN/TUN owning the default route must be disabled first. Local-route adapters such as WSL/Hyper-V can coexist. Uncertain cleanup retains recovery records and blocks another session; do not delete unrelated adapters.

Keep the GUI and service from the same package. Before replacing files in an authorized directory, exit QuickClash normally and wait for `quickclash_service.exe` to exit; replace the complete package and restart. You can leave the TUN preference enabled. Updating the same authorized directory does not require directory authorization again. If an older service stays resident, stop `QuickClashService` manually and confirm that its process has exited. The new service migrates an existing authorized automatic-start registration to on-demand startup. The portable directory is the source of program, service, core, and bundled Geo files; `%ProgramData%/QuickClash` holds protected authorization and private session/cleanup data.

Before deleting or moving an installation with a registered service, uninstall the service from an administrator terminal, with the current directory set to the extracted package root:

```powershell
.\bin\quickclash.exe --service-setup uninstall
```

This removes service registration and directory authorization while preserving program files and user data.

## Data, core, resources, and known limitations

Use **Settings → Data and diagnostics → Open data folder** to find the actual user data location. Re-extracting the program does not erase that data. Subscription addresses, headers, and test targets use current-user Windows DPAPI; node credentials, drafts, backups, and recovery archives can contain plain text secrets. Keep them private. A copied credential file cannot be decrypted under another Windows account.

Bundled `core/mihomo.exe` and Geo files are checked against frozen metadata; missing or invalid files block their use rather than being silently downloaded. Use **Settings → Core and resources** for compatible-core download/repair and Geo download, apply, and rollback. Core downloads require a separate apply confirmation. Scheduled Geo downloads leave a pending snapshot for confirmation; **Update Geo databases…** asks for confirmation before downloading, validating, and applying both databases together. Only the client's compatibility list is supported for core downloads, not arbitrary upstream versions. Applying resources can restart the core and interrupt connections. Keep the complete package available for recovery.

Custom strategy groups belong to their subscription/profile entry. Updating that entry or changing its URL retains them; deleting it removes its groups, and history rollback restores that version's definitions. Missing dependencies or name conflicts reject updates and preserve previous data. A reported disappearance after changing a subscription has not been reproduced or explained.

Configuration backups can contain plain text credentials and have no password encryption. Both backup scopes currently refuse a whole export if an enabled profile-specific `RULE-SET` depends on a runtime rule provider unavailable in the offline snapshot. Keep an independent copy of important source configurations; do not assume every runtime-provider configuration can be backed up offline.

For configuration or resource errors, read the displayed error, exit normally, and check the full extracted package before retrying. Damaged or newer-schema user data is preserved and may require explicit recovery; do not delete the data directory as a first troubleshooting step.

## Licenses

QuickClash is licensed under GPL-3.0-only; see [the project license](../LICENSE). Third-party components have their own licenses and notices in `licenses/`; [source obligations](../docs/baseline/licenses.md) describe the required source materials. This development package is unsigned. Public distribution still requires checking the complete corresponding-source materials for Qt, Mihomo, and Geo; upstream links and license texts alone do not complete that check.
