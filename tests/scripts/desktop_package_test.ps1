#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [ValidateSet('Package', 'Host', 'Proxy', 'Flow')][string]$Phase = 'Package',
    [string]$EvidenceRoot = ''
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Requires a native Windows desktop.' }
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskPackage = (Resolve-Path -LiteralPath $PackageRoot).Path
if (-not $EvidenceRoot) {
    $EvidenceRoot = Join-Path $taskRepo ('build/desktop_package_' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
}
$taskEvidence = [IO.Path]::GetFullPath($EvidenceRoot, $taskRepo)
if (Test-Path -LiteralPath $taskEvidence) { throw 'Use a new evidence directory.' }
$null = [IO.Directory]::CreateDirectory($taskEvidence)
. (Join-Path $taskRepo 'scripts/native_process.ps1')
$taskManifest = Get-Content -LiteralPath (Join-Path $taskPackage 'metadata/package_manifest.json') -Raw | ConvertFrom-Json
if ($taskManifest.schema_version -ne 1) { throw 'Unsupported package manifest.' }
$taskPrefix = $taskPackage.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
foreach ($taskFile in $taskManifest.files) {
    $taskPath = [IO.Path]::GetFullPath($taskFile.path, $taskPackage)
    if (-not $taskPath.StartsWith($taskPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Get-Item -LiteralPath $taskPath).Length -ne $taskFile.bytes -or
        (Get-FileHash -LiteralPath $taskPath).Hash.ToLowerInvariant() -cne $taskFile.sha256) {
        throw "Package size/hash/path mismatch: $($taskFile.path)"
    }
}
function Get-PeMachine([string]$Path) {
    $taskStream = [IO.File]::OpenRead($Path)
    $taskReader = [IO.BinaryReader]::new($taskStream)
    try {
        if ($taskReader.ReadUInt16() -ne 0x5a4d) { throw 'Missing MZ signature.' }
        $taskStream.Position = 0x3c
        $taskPeOffset = $taskReader.ReadUInt32()
        $taskStream.Position = $taskPeOffset
        if ($taskReader.ReadUInt32() -ne 0x4550) { throw 'Missing PE signature.' }
        return $taskReader.ReadUInt16()
    } finally { $taskReader.Dispose() }
}
foreach ($taskRelative in 'bin/quickclash.exe', 'core/mihomo.exe', 'bin/Qt6Core.dll', 'bin/vcruntime140.dll') {
    if ((Get-PeMachine (Join-Path $taskPackage $taskRelative)) -ne 0x8664) { throw 'Expected x64 package.' }
}
$taskMatrix = Get-Content -LiteralPath (Join-Path $taskPackage 'metadata/release_matrix.json') -Raw | ConvertFrom-Json
if ((Get-FileHash -LiteralPath (Join-Path $taskPackage 'core/mihomo.exe')).Hash.ToLowerInvariant() -cne
    $taskMatrix.mihomo.windows.executable_sha256) { throw 'Frozen official core mismatch.' }
foreach ($taskRelative in 'readme.md', 'readme_zh_cn.md', 'bin/Qt6Network.dll', 'plugins/platforms/qwindows.dll', 'licenses/quickclash_gpl_3.txt') {
    if (-not (Test-Path -LiteralPath (Join-Path $taskPackage $taskRelative))) { throw "Missing $taskRelative" }
}
function New-ProbeStore([string]$Name) {
    $taskStore = Join-Path $taskEvidence $Name
    $null = [IO.Directory]::CreateDirectory($taskStore)
    [IO.File]::WriteAllText((Join-Path $taskStore '.quickclash_desktop_probe'), "QuickClash isolated desktop probe" + [char]10)
    return $taskStore
}
$taskFixture = Join-Path $taskEvidence '本地 配置.yaml'
Copy-Item -LiteralPath (Join-Path $taskRepo 'tests/fixtures/mvp/desktop_loopback.yaml') -Destination $taskFixture
function Probe-Arguments([string]$Store, [string]$ProbePhase, [string]$Output) {
    return @('--desktop-test', "--desktop-root=$Store", "--desktop-phase=$ProbePhase",
        "--desktop-output=$Output", "--desktop-fixture=$taskFixture",
        "--desktop-flow-fixture=$taskEvidence/flow_fixture.json")
}
function Run-Probe([string]$Root, [string]$Store, [string]$ProbePhase) {
    $taskOutput = Join-Path $taskEvidence ($ProbePhase + '.json')
    $taskWorkingDirectory = if ($Phase -eq 'Flow') { $taskRepo } else { $taskEvidence }
    Invoke-NativeStep -Executable (Join-Path $Root 'bin/quickclash.exe') -Arguments (Probe-Arguments $Store $ProbePhase $taskOutput) -WorkingDirectory $taskWorkingDirectory -LogPath (Join-Path $taskEvidence ($ProbePhase + '.log')) -TimeoutSeconds 55
    $taskResult = Get-Content -LiteralPath $taskOutput -Raw | ConvertFrom-Json
    if (-not $taskResult.passed -or $taskResult.qmlErrors -ne 0) { throw "Probe failed: $ProbePhase" }
    return $taskResult
}
function Wait-CoreExit([int]$CoreId) {
    $taskClock = [Diagnostics.Stopwatch]::StartNew()
    while ($taskClock.ElapsedMilliseconds -lt 5000) {
        if (-not (Get-Process -Id $CoreId -ErrorAction SilentlyContinue)) { return }
        Start-Sleep -Milliseconds 50
    }
    throw "Owned core remains alive after GUI exit: $CoreId"
}
$taskEnvironmentKeys = @('Path', 'QT_QPA_PLATFORM', 'QT_QUICK_BACKEND', 'QSG_RHI_BACKEND', 'QSG_RENDER_LOOP',
    'QML_IMPORT_PATH', 'QML2_IMPORT_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_FORCE_STDERR_LOGGING')
$taskEnvironment = @{}
foreach ($taskKey in $taskEnvironmentKeys) { $taskEnvironment[$taskKey] = [Environment]::GetEnvironmentVariable($taskKey) }
try {
    foreach ($taskKey in $taskEnvironmentKeys) { [Environment]::SetEnvironmentVariable($taskKey, $null) }
    $env:Path = "$env:SystemRoot/System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_FORCE_STDERR_LOGGING = '1'
    if ($Phase -eq 'Package') {
        $taskImported = Run-Probe $taskPackage (New-ProbeStore '导入 用户数据') 'import'
        Wait-CoreExit $taskImported.corePid
        $taskRepairPackage = Join-Path $taskEvidence 'repair_package'
        $null = [IO.Directory]::CreateDirectory($taskRepairPackage)
        Copy-Item -LiteralPath (Join-Path $taskPackage 'bin') -Destination $taskRepairPackage -Recurse
        Copy-Item -LiteralPath (Join-Path $taskPackage 'metadata') -Destination $taskRepairPackage -Recurse
        Copy-Item -LiteralPath (Join-Path $taskPackage 'plugins') -Destination $taskRepairPackage -Recurse
        Copy-Item -LiteralPath (Join-Path $taskPackage 'qml') -Destination $taskRepairPackage -Recurse
        Copy-Item -LiteralPath (Join-Path $taskPackage 'translations') -Destination $taskRepairPackage -Recurse
        # Deliberately omit core/. Packaged resolution must not use a development fallback.
        $taskRepair = Run-Probe $taskRepairPackage (New-ProbeStore '修复 用户数据') 'repair'
        if ($taskRepair.ready -or $taskRepair.corePid -ne 0 -or $taskRepair.error -cne 'CoreIntegrityFailed') {
            throw 'Missing core was not reported precisely.'
        }
    } elseif ($Phase -eq 'Flow') {
        $taskServerStart = [Diagnostics.ProcessStartInfo]::new()
        $taskServerStart.FileName = Join-Path $taskPackage 'bin/quickclash.exe'
        $taskServerStart.WorkingDirectory = $taskRepo
        $taskServerStart.UseShellExecute = $false
        $taskServerStart.CreateNoWindow = $true
        $taskServerStart.ArgumentList.Add("--desktop-flow-server=$taskEvidence/flow_fixture.json")
        $taskServer = [Diagnostics.Process]::new()
        $taskServer.StartInfo = $taskServerStart
        try {
            if (-not $taskServer.Start()) { throw 'Fixture process failed.' }
            $taskClock = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath "$taskEvidence/flow_fixture.json") -and
                -not $taskServer.HasExited -and $taskClock.ElapsedMilliseconds -lt 5000) {
                Start-Sleep -Milliseconds 50
            }
            if (-not (Test-Path -LiteralPath "$taskEvidence/flow_fixture.json")) { throw 'Fixture not ready.' }
            $taskStore = New-ProbeStore '主链路 用户数据'
            $taskFlow = Run-Probe $taskPackage $taskStore 'flow'
            Wait-CoreExit $taskFlow.corePid
            $taskFlowReopened = Run-Probe $taskPackage $taskStore 'flow_reopen'
            Wait-CoreExit $taskFlowReopened.corePid
            foreach ($taskKey in 'selectedId', 'selectionGeneration', 'theme', 'nodeReadback') {
                if ($taskFlow.$taskKey -cne $taskFlowReopened.$taskKey) { throw "Reopen changed $taskKey" }
            }
            foreach ($taskKey in 'mixedPort', 'controllerPort', 'mode') {
                if ($taskFlow.preferences.$taskKey -cne $taskFlowReopened.preferences.$taskKey) { throw "Reopen changed $taskKey" }
            }
        } finally {
            if (-not $taskServer.HasExited) { $taskServer.Kill(); $taskServer.WaitForExit() }
            $taskServer.Dispose()
        }
    } elseif ($Phase -eq 'Proxy') {
        $taskProxy = Run-Probe $taskPackage (New-ProbeStore '代理 用户数据') 'proxy'
        Wait-CoreExit $taskProxy.corePid
        if (-not $taskProxy.nativeOriginalSnapshotRestored) { throw 'Original proxy snapshot not restored.' }
    } else {
        $taskLifecycle = Run-Probe $taskPackage (New-ProbeStore '窗口 用户数据') 'lifecycle'
        Wait-CoreExit $taskLifecycle.corePid
        $taskStore = New-ProbeStore '恢复 用户数据'
        $taskHoldOutput = Join-Path $taskEvidence 'hold.json'
        $taskStart = [Diagnostics.ProcessStartInfo]::new()
        $taskStart.FileName = Join-Path $taskPackage 'bin/quickclash.exe'
        $taskStart.WorkingDirectory = $taskEvidence
        $taskStart.UseShellExecute = $false
        $taskStart.CreateNoWindow = $true
        $taskStart.RedirectStandardOutput = $true
        $taskStart.RedirectStandardError = $true
        foreach ($taskArgument in (Probe-Arguments $taskStore 'hold' $taskHoldOutput)) { $taskStart.ArgumentList.Add($taskArgument) }
        $taskGui = [Diagnostics.Process]::new()
        $taskGui.StartInfo = $taskStart
        try {
            if (-not $taskGui.Start()) { throw 'GUI start failed.' }
            $taskStdout = $taskGui.StandardOutput.ReadToEndAsync()
            $taskStderr = $taskGui.StandardError.ReadToEndAsync()
            $taskClock = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath $taskHoldOutput) -and -not $taskGui.HasExited -and $taskClock.ElapsedMilliseconds -lt 30000) {
                Start-Sleep -Milliseconds 50
            }
            $taskHeld = Get-Content -LiteralPath $taskHoldOutput -Raw | ConvertFrom-Json
            if ($taskHeld.stage -cne 'holding' -or -not $taskHeld.ready -or $taskHeld.guiPid -ne $taskGui.Id) { throw 'GUI never reached held Ready.' }
            # Kill only the GUI; descendants must be reclaimed by production Job ownership.
            $taskGui.Kill()
            if (-not $taskGui.WaitForExit(5000)) { throw 'GUI kill did not finish.' }
            Wait-CoreExit $taskHeld.corePid
            $taskReopened = Run-Probe $taskPackage $taskStore 'reopen'
            Wait-CoreExit $taskReopened.corePid
            if ($taskReopened.selectedId -cne $taskHeld.selectedId -or
                $taskReopened.selectionGeneration -cne $taskHeld.selectionGeneration -or
                $taskReopened.preferences.mixedPort -ne $taskHeld.preferences.mixedPort -or
                $taskReopened.preferences.controllerPort -ne $taskHeld.preferences.controllerPort) {
                throw 'Reopen did not preserve selection and ports.'
            }
        } finally {
            if ($taskGui.Id -and -not $taskGui.HasExited) { $taskGui.Kill(); $null = $taskGui.WaitForExit(5000) }
            if ($taskStdout -and $taskStdout.Wait(5000)) { [IO.File]::WriteAllText((Join-Path $taskEvidence 'hold.stdout.log'), $taskStdout.GetAwaiter().GetResult()) }
            if ($taskStderr -and $taskStderr.Wait(5000)) { [IO.File]::WriteAllText((Join-Path $taskEvidence 'hold.stderr.log'), $taskStderr.GetAwaiter().GetResult()) }
            $taskGui.Dispose()
        }
    }
    [ordered]@{ passed = $true; phase = $Phase; package = $taskPackage; manifest_files = $taskManifest.files.Count;
        manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $taskPackage 'metadata/package_manifest.json')).Hash;
        developer_qt_path = $false; qpa = 'windows'; rendering_override = $false;
        scope = 'Current Windows medium-integrity desktop; not clean-machine, real network event or TUN acceptance'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskEvidence 'result.json') -Encoding utf8NoBOM
    Write-Output "PASS $Phase : $taskEvidence"
} finally {
    foreach ($taskKey in $taskEnvironmentKeys) { [Environment]::SetEnvironmentVariable($taskKey, $taskEnvironment[$taskKey]) }
}
