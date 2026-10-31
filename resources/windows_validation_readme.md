# QuickClash Windows 验收工具包

这是未签名的本地测试构建及验收夹具。将整个目录复制到 Windows x64 桌面后，以普通用户在工具包根目录运行以下命令。需要 PowerShell 7.2 或更新版本（也可用官方便携版本）；Windows 自带的 Windows PowerShell 5.1 不适用。该要求仅针对本工具包的验收脚本；产品自身的服务安装/更新入口不依赖 PowerShell。无需源码仓库、Qt SDK、Visual Studio 或编译工具。

默认检查完整包摘要、x64 依赖，清除开发 Qt 环境后，从中文空格路径导入本地配置并启动官方核心，再验证缺核心时的修复入口：

~~~powershell
pwsh -NoProfile -File tests/scripts/desktop_package_test.ps1 -PackageRoot './package' -Phase Package
~~~

其他独立阶段按需要运行，每次会新建证据目录：

~~~powershell
pwsh -NoProfile -File tests/scripts/desktop_package_test.ps1 -PackageRoot './package' -Phase Host
pwsh -NoProfile -File tests/scripts/desktop_package_test.ps1 -PackageRoot './package' -Phase Flow
pwsh -NoProfile -File tests/scripts/desktop_package_test.ps1 -PackageRoot './package' -Phase Proxy
~~~

- Host：真实托盘隐藏/卸载/恢复期间保持本地转发，强杀本轮 GUI 后检查 owned 核心回收和同目录重开。
- Flow：GUI 订阅/选节点/模式/偏好保存，独立显式代理客户端及 WinINet 系统代理客户端通过回环 CONNECT 上游访问 HTTPS，退出重开后再次核对。
- Proxy：独立进程实际修改绕过列表，检查外部冲突保留、显式重新开启后的新基线及释放。

Flow 和 Proxy 会临时修改当前用户 WinINet 系统代理。测试结束核对原快照恢复，外部后续修改不覆盖。测试期间该用户其他遵循系统代理的应用也会受影响。关闭本工具包内其他 QuickClash 测试实例后再运行；不需要安装或启停权限服务，不开启 TUN。

测试使用标记的隔离数据目录和公开测试证书/私钥，证书只由测试进程信任，不安装到系统。测试私钥不得用于真实服务。包内 package/readme.md 提供产品使用、限制及异常中断后的代理手工恢复说明。

原始证据默认写入本目录的 build/desktop_package_<UTC时间>/，也可用 -EvidenceRoot '<新的绝对目录>' 指定；已有目录拒绝覆盖。成功输出 PASS <阶段>，对应 result.json 中 passed=true，任意失败返回非零。保留失败目录及日志，不手动把失败标成通过。证据可能包含本机路径和标识，分享前复核。

validation_manifest.json 记录工具包交付时文件尺寸与 SHA256，package/metadata/package_manifest.json 保留原包清单并由测试入口验证。摘要用于发现搬运损坏，不构成签名或可信发布来源。

记录实际机器、Windows 版本、普通用户身份、是否安装 Qt/开发工具和运行阶段。当前开发机通过不代表干净机器通过；本工具包也不验证真实换网/唤醒、第二用户桌面争用、高 DPI/多屏迁移、实际读屏或 TUN。这些条件须另行实测。
