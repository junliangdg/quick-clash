#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [ValidateRange(1, 30)][int]$Repeats = 3,
    [string]$EvidenceDirectory = ''
)
$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$taskExe = (Resolve-Path -LiteralPath $Executable).Path
if (-not $EvidenceDirectory) {
    $EvidenceDirectory = 'build/startup_reopen/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '_' + [guid]::NewGuid().ToString('N')
}
$taskEvidence = [IO.Path]::GetFullPath($EvidenceDirectory, $taskRepo)
if (Test-Path -LiteralPath $taskEvidence) { throw 'Use a new evidence directory.' }
$null = [IO.Directory]::CreateDirectory($taskEvidence)
$taskStore = Join-Path $taskEvidence 'store'
$null = [IO.Directory]::CreateDirectory($taskStore)
[IO.File]::WriteAllText((Join-Path $taskStore '.quickclash_desktop_probe'), "QuickClash isolated desktop probe`n")
$taskFixture = Join-Path $taskEvidence 'unicode_loopback.yaml'
$taskYaml = @'
proxies:
  - name: 中文节点测速记录长名称
    type: http
    server: 127.0.0.1
    port: 9
  - name: 中文
    type: http
    server: 127.0.0.1
    port: 9
  - name: 中文节点测速记录长名称更长
    type: http
    server: 127.0.0.1
    port: 9
proxy-groups:
  - name: 中文默认策略组
    type: select
    proxies: [中文节点测速记录长名称, 中文, 中文节点测速记录长名称更长, DIRECT]
rules:
  - MATCH,中文默认策略组
'@
[IO.File]::WriteAllText($taskFixture, $taskYaml, [Text.UTF8Encoding]::new($false))
. (Join-Path $taskRepo 'scripts/native_process.ps1')
function Invoke-StartupProbe([string]$Phase, [string]$Name) {
    $taskOutput = Join-Path $taskEvidence "$Name.json"
    $taskArguments = @('--desktop-test', "--desktop-root=$taskStore", "--desktop-phase=$Phase",
        "--desktop-output=$taskOutput", "--desktop-fixture=$taskFixture")
    Invoke-NativeStep -Executable $taskExe -Arguments $taskArguments -WorkingDirectory $taskRepo -LogPath (Join-Path $taskEvidence "$Name.log") -TimeoutSeconds 40
    $taskResult = Get-Content -LiteralPath $taskOutput -Raw | ConvertFrom-Json
    if (-not $taskResult.passed -or -not $taskResult.ready -or $taskResult.qmlErrors -ne 0) {
        throw "Startup probe failed: $Name"
    }
    $taskClock = [Diagnostics.Stopwatch]::StartNew()
    while (Get-Process -Id $taskResult.corePid -ErrorAction SilentlyContinue) {
        if ($taskClock.ElapsedMilliseconds -gt 5000) { throw 'Owned core did not exit with the probe.' }
        Start-Sleep -Milliseconds 20
    }
    return $taskResult
}
$null = Invoke-StartupProbe 'import' 'import'
$taskCatalogPath = Join-Path $taskStore 'data/catalog.json'
$taskCatalog = Get-Content -LiteralPath $taskCatalogPath -Raw | ConvertFrom-Json
$taskOriginal = $taskCatalog.profiles[0]
$taskSource = Join-Path $taskStore $taskOriginal.sourceRef
$taskProfiles = @()
for ($taskIndex = 0; $taskIndex -lt 3; $taskIndex++) {
    $taskEntry = $taskOriginal | ConvertTo-Json -Depth 20 | ConvertFrom-Json
    if ($taskIndex -gt 0) {
        $taskEntry.id = [guid]::NewGuid().ToString()
        $taskEntry.sourceRef = "data/profiles/$($taskEntry.id)/startup.yaml"
        $taskCopy = Join-Path $taskStore $taskEntry.sourceRef
        $null = [IO.Directory]::CreateDirectory((Split-Path $taskCopy))
        Copy-Item -LiteralPath $taskSource -Destination $taskCopy
    }
    $taskEntry.name = "中文启动配置$taskIndex"
    $taskProfiles += $taskEntry
}
$taskCatalog.profiles = $taskProfiles
$taskCatalog | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $taskCatalogPath -Encoding utf8NoBOM
$taskResults = @()
for ($taskAttempt = 1; $taskAttempt -le $Repeats; $taskAttempt++) {
    $taskResult = Invoke-StartupProbe 'startup' "startup_$taskAttempt"
    if ($taskResult.profileCount -ne 3 -or $taskResult.rebuildCycles -ne 3) {
        throw 'Multi-profile startup and window rebuild were not exercised.'
    }
    $taskResults += $taskResult
}
$taskResults | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $taskEvidence 'results.json') -Encoding utf8NoBOM
Write-Output "Unicode multi-profile startup: $Repeats runs, $($Repeats * 3) window rebuilds passed. Evidence: $taskEvidence"
