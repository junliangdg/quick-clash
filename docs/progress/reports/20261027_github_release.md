# Windows GitHub持续交付与干净发行包

- 日期：2026-10-27；Codex；关联P0-09/P2-16打包工具维护，后续P4-06/P5发行验收。
- 用户要求CI持续交付到GitHub Releases，dist只保留用户程序及必要依赖；已明确先发布Windows，Linux/macOS保留CI构建。
- 状态：Review；实现、本地Release构建、实际打包、发布编排模拟及最终回归/仓库检查通过；远端实测仍待执行。当前工作区补丁未提交或推送，实际远端发布NotRun。

## 实现与边界

- `.github/workflows/build.yml`在全部native验证成功后运行Windows发行任务；PR不发布。默认分支/其手动运行更新continuous预发行；v标签发布对应版本，连字符版本标记预发行。写权限仅发行job持有。
- release preset单独构建，Release/BUILD_TESTING=OFF；宿主/原型/键盘及已有UI探针编译期隔离。发行脚本拒绝测试构建，不从dev目录复制程序。
- `package_release_windows.ps1`通过CMake安装Qt/QML/CRT运行依赖，清开发PATH启动并检查页面/错误；加入锁定内核、Geo、许可、必要元数据和用户说明，检查多余内容后生成ZIP和SHA-256。诊断只在build日志目录。
- `publish_release.ps1`校验ZIP摘要，通过GitHub CLI更新continuous标签或使用已有版本标签，上传包及摘要；continuous重跑更新同名资产，发布前核对分支当前commit，跳过已经落后的运行，不创建每次commit一个Release。
- 当前未签名；Linux/macOS用户包、干净普通用户环境/TUN验收与应用内自动更新签名链不在本次实现范围。

## 验证

- 本机Qt6.9.0/MSVC19.51，CI仍锁定Qt6.8.0/Windows2022；本机验证不能代替CI参考环境。独立Release首次构建发现未隔离的回放测试变量，修复编译期边界后[构建通过](github_ci_evidence/release_build_final.log)。首次包检查拒绝Qt默认加入的vc_redist.x64.exe，改用CMake InstallRequiredSystemLibraries安装app-local CRT DLL并关闭Qt安装器部署，[重构建](github_ci_evidence/release_crt_build.log)和[最终打包](github_ci_evidence/package_final.log)退出0。
- 本地产物为dist/ci_release_final.zip（77,197,685字节），SHA-256为58fe68e4166a5499bfd6c4ff0e6cf9db3a9b765f3adee175e7405f6d6baa0ac2；仅供本次工作区验证，Version=local-validation、Commit为空，不冒充已提交版本。
- [包检查](github_ci_evidence/package_checks.json)：191项，190个清单文件逐一核验，外加清单自身；仅3个exe（客户端/服务/Mihomo），无QtTest/测试程序/符号/开发源码/日志/安装器。发行build.ninja不含测试探针或QtTest；dev测试构建在写入dist前被拒绝。
- [清开发PATH启动](github_ci_evidence/release_smoke.json)：page_ready=true、qml_errors=0、replay_running=false；诊断与安装日志在build/release/logs，包中没有诊断。此验证使用软件渲染进行部署检查，未改变产品默认GPU路径，也未安装服务或更改系统代理。
- [发布编排模拟](github_ci_evidence/publish_tests_final.log)覆盖首次continuous、重复资产替换、过期commit跳过、正式/RC标签、上传失败和坏摘要拒绝；只模拟gh，没有外部写入。新增简明随包说明resources/windows_readme.md，不复制开发流水用户指南。
- [最终Windows全目标增量构建](github_ci_evidence/dev_delivery_build.log)退出0；发行隔离后[开发回归JUnit](github_ci_evidence/delivery_tests.xml)22/22通过（65.93秒）。复用同一工作区此前runtime/publication/单实例3/3及headless5/5证据；25项dev测试均有本轮通过证据，没有宣称一次整套CTest通过。
- 11项[依赖获取回归](github_ci_evidence/fetch_tests_final.log)通过；最终工作流actionlint、PowerShell语法、全自有C++格式、设计生成/核对、进度/源码命名及diff空白检查通过。配置缺少Vulkan头的提示不阻断现用Qt Quick默认后端构建；QML既有6项警告保持记录。
- 远端发布未执行，不将本地打包等同GitHub Releases成功。
