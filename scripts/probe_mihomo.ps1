#Requires -Version 7.2
[CmdletBinding()]
param([string]$BuildDirectory = 'build/dev', [switch]$ApiOnly, [switch]$AppContainer, [string]$EvidenceDirectory = '')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'native_process.ps1')
if (-not $IsWindows) { throw 'This core probe requires the pinned Windows executable' }
$matrix = Get-Content (Join-Path $repoRoot 'docs/baseline/release_matrix.json') -Raw | ConvertFrom-Json
$core = Join-Path $repoRoot '.cache/mihomo/mihomo.exe'
if ((Get-FileHash $core -Algorithm SHA256).Hash -ine $matrix.mihomo.windows.executable_sha256) { throw 'Core hash mismatch' }
$runRoot = Join-Path $repoRoot ('.cache/core_probe_' + [guid]::NewGuid().ToString('N'))
$evidenceRoot = if ($EvidenceDirectory) {
    [IO.Path]::GetFullPath($EvidenceDirectory, $repoRoot)
} else {
    Join-Path $repoRoot ('build/core_probe/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '_' + [guid]::NewGuid().ToString('N'))
}
if (Test-Path -LiteralPath $evidenceRoot) { throw 'EvidenceDirectory must be new; preserve previous runs' }
New-Item $evidenceRoot -ItemType Directory | Out-Null
Write-Output "Core evidence: $evidenceRoot"
$handler = [System.Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(3)
$secret = [Convert]::ToHexString([System.Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$client.DefaultRequestHeaders.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $secret)
$script:records = [System.Collections.Generic.List[object]]::new()
$process = $null
$jobProcess = $null
$childProcess = $null
$stage = 'validation'
$summary = [ordered]@{
    result = 'Fail'; api_result = 'NotRun'; tag = $matrix.mihomo.tag
    scope = $(if ($ApiOnly) { 'API-only' } else { 'API, listener ownership and Job' })
    started_at_utc = [DateTime]::UtcNow.ToString('o')
    valid_exit = $null; invalid_exit = $null; validation_new_files = @()
    listeners = 'NotRun'; job = 'NotRun'; network_isolation = 'NotRun (optional AppContainer experiment; not used by the client)'
    system_proxy = 'untouched'; geo = 'unsupported'
}
function Require($Condition, $Message) { if (-not $Condition) { throw $Message } }
function FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return $listener.LocalEndpoint.Port } finally { $listener.Stop() }
}
function Request($Method, $Path, $Body = $null, $Expected = 200, [switch]$Unauthenticated) {
    $request = [System.Net.Http.HttpRequestMessage]::new([System.Net.Http.HttpMethod]::new($Method), "$script:baseUrl$Path")
    if ($Unauthenticated) { $request.Headers.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', 'invalid') }
    if ($null -ne $Body) { $request.Content = [System.Net.Http.StringContent]::new(($Body | ConvertTo-Json -Compress), [Text.Encoding]::UTF8, 'application/json') }
    try {
        $response = $client.SendAsync($request).GetAwaiter().GetResult()
        try {
            $raw = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
            $status = [int]$response.StatusCode
            $script:records.Add([ordered]@{method=$Method;path=$Path;request=$Body;status=$status;response=$raw})
            Require ($status -eq $Expected) "$Method $Path returned $status, expected $Expected"
            if ($raw) { return ($raw | ConvertFrom-Json) }
        } finally { $response.Dispose() }
    } finally { $request.Dispose() }
}
function LaunchCore {
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new($core)
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    foreach ($argument in @('-d', $runRoot, '-f', (Join-Path $runRoot 'config.yaml'))) { $startInfo.ArgumentList.Add($argument) }
    return [System.Diagnostics.Process]::Start($startInfo)
}
function WaitReady {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        if ($process -and $process.HasExited) { throw 'Core exited before Ready' }
        if ($jobProcess -and $jobProcess.HasExited) { throw 'Job owner exited before Ready' }
        try { return Request GET '/version' } catch { Start-Sleep -Milliseconds 100 }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Core Ready deadline exceeded'
}
try {
    New-Item $runRoot -ItemType Directory | Out-Null
    $mixedPort = FreePort
    do { $controllerPort = FreePort } while ($controllerPort -eq $mixedPort)
    $script:baseUrl = "http://127.0.0.1:$controllerPort"
    $config = @"
mixed-port: $mixedPort
bind-address: 127.0.0.1
allow-lan: false
external-controller: 127.0.0.1:$controllerPort
secret: $secret
mode: rule
log-level: silent
dns: {enable: false}
tun: {enable: false}
geo-auto-update: false
profile: {store-selected: false, store-fake-ip: false}
proxies:
  - {name: fixture, type: http, server: proxy.example.invalid, port: 18080}
proxy-groups:
  - name: '代理 / A%?#'
    type: select
    proxies: [DIRECT, fixture]
rules:
  - MATCH,代理 / A%?#
"@
    $configPath = Join-Path $runRoot 'config.yaml'
    Set-Content $configPath $config -Encoding utf8
    $before = @(Get-ChildItem $runRoot -File | ForEach-Object Name)
    $validationLog = Join-Path $evidenceRoot 'validation.log'
    Invoke-NativeStep -Executable $core -Arguments @('-t', '-d', $runRoot, '-f', $configPath) -WorkingDirectory $runRoot -LogPath $validationLog -TimeoutSeconds 10
    $summary.valid_exit = 0
    $after = @(Get-ChildItem $runRoot -File | ForEach-Object Name)
    $sideEffects = @($after | Where-Object { $_ -notin $before })
    $summary.validation_new_files = $sideEffects
    Require ($sideEffects.Count -eq 0) 'Validation unexpectedly created files'
    Set-Content (Join-Path $runRoot 'invalid.yaml') 'proxies: ['
    $invalidLog = Join-Path $evidenceRoot 'invalid.log'
    try {
        Invoke-NativeStep -Executable $core -Arguments @('-t', '-d', $runRoot, '-f', (Join-Path $runRoot 'invalid.yaml')) -WorkingDirectory $runRoot -LogPath $invalidLog -TimeoutSeconds 10
    } catch {
        if (-not (Test-Path -LiteralPath "$invalidLog.json")) { throw }
        $invalidStep = Get-Content -LiteralPath "$invalidLog.json" -Raw | ConvertFrom-Json
        if ($invalidStep.timed_out -or $invalidStep.exit_code -ne 1) { throw }
    }
    $invalidStep = Get-Content -LiteralPath "$invalidLog.json" -Raw | ConvertFrom-Json
    $summary.invalid_exit = $invalidStep.exit_code
    Require ($invalidStep.exit_code -eq 1) 'Invalid configuration must exit 1'
    if ($AppContainer) {
        $stage = 'network_isolation'
        $isolationBinary = Join-Path ([IO.Path]::GetFullPath($BuildDirectory, $repoRoot)) 'core_isolation_probe.exe'
        Invoke-NativeStep -Executable $isolationBinary -Arguments @($core, $configPath, (Join-Path $runRoot 'isolated'), $evidenceRoot) -WorkingDirectory $runRoot -LogPath (Join-Path $evidenceRoot 'isolation.log') -TimeoutSeconds 40
        $summary.network_isolation = 'Pass: zero-capability AppContainer, unrestricted control succeeds, isolated loopback denied within 1s, valid=0 invalid=1, no new files, profile removed'
    }
    $stage = 'api'
    $process = LaunchCore
    $version = WaitReady
    Require ($version.version -eq $matrix.mihomo.tag) 'Unexpected core version'
    $null = Request GET '/version' -Expected 401 -Unauthenticated
    $configs = Request GET '/configs'
    Require ($configs.'mixed-port' -eq $mixedPort -and -not $configs.'allow-lan') 'Listener policy mismatch'
    $null = Request GET '/proxies'
    $group = [Uri]::EscapeDataString('代理 / A%?#')
    $null = Request PUT "/proxies/$group" @{name='fixture'} 204
    $selected = Request GET "/proxies/$group"
    Require ($selected.now -eq 'fixture') 'Selection readback mismatch'
    $null = Request PUT "/proxies/$group" @{name='missing'} 400
    foreach ($mode in @('global','direct','rule')) {
        $null = Request PATCH '/configs' @{mode=$mode} 204
        Require ((Request GET '/configs').mode -eq $mode) 'Mode readback mismatch'
    }
    $null = Request PUT '/configs' @{payload=$config} 204
    $null = Request PUT "/proxies/$group" @{name='fixture'} 204
    Require ((Request GET '/configs').mode -eq 'rule') 'Reload failed'
    $summary.listeners = 'NotRun (API-only scope)'
    if (-not $ApiOnly) {
        $stage = 'listeners'
        $listeners = @(Get-NetTCPConnection -State Listen -OwningProcess $process.Id)
        Require (@($listeners | Where-Object { $_.LocalPort -eq $mixedPort -and $_.LocalAddress -eq '127.0.0.1' }).Count -eq 1) 'Mixed listener ownership mismatch'
        Require (@($listeners | Where-Object { $_.LocalPort -eq $controllerPort -and $_.LocalAddress -eq '127.0.0.1' }).Count -eq 1) 'Controller ownership mismatch'
        $summary.listeners = 'Pass: owned PID and loopback verified'
    }
    $stage = 'restart'
    $process.Kill()
    Require ($process.WaitForExit(5000)) 'Core exit deadline exceeded'
    $process.Dispose()
    $process = $null
    $process = LaunchCore
    $null = WaitReady
    Require ((Request GET "/proxies/$group").now -eq 'DIRECT') 'Disabled selection persistence did not reset'
    $process.Kill()
    Require ($process.WaitForExit(5000)) 'Restarted core exit deadline exceeded'
    $process.Dispose()
    $process = $null
    $summary.api_result = 'Pass'
    $summary.authentication = '401 with wrong secret'
    $summary.selection = 'encoded Unicode and reserved characters, readback and restart reset'
    $summary.modes = 'rule/global/direct readback'
    $summary.reload = 'PUT /configs payload'
    if (-not $ApiOnly) {
        $stage = 'job'
        $pidPath = Join-Path $runRoot 'child_pid.txt'
        $jobBinary = Join-Path ([IO.Path]::GetFullPath($BuildDirectory, $repoRoot)) 'core_job_probe.exe'
        $jobInfo = [System.Diagnostics.ProcessStartInfo]::new($jobBinary)
        $jobInfo.UseShellExecute = $false
        $jobInfo.CreateNoWindow = $true
        foreach ($argument in @($core, $configPath, $runRoot, $pidPath)) { $jobInfo.ArgumentList.Add($argument) }
        $jobProcess = [System.Diagnostics.Process]::Start($jobInfo)
        $null = WaitReady
        Require (Test-Path $pidPath) 'Job child PID missing'
        $childProcess = [System.Diagnostics.Process]::GetProcessById([int](Get-Content $pidPath))
        $jobProcess.Kill()
        Require ($jobProcess.WaitForExit(5000)) 'Job owner did not exit'
        Require ($childProcess.WaitForExit(5000)) 'KILL_ON_JOB_CLOSE did not terminate child'
        $childProcess.Dispose()
        $childProcess = $null
        $summary.job = 'Pass: suspended create -> assign -> resume -> owner kill -> child exit'
    }
    $summary.result = 'Pass'
} catch {
    $summary.failed_stage = $stage
    $summary.error = $_.Exception.Message.Replace($secret, '<redacted>')
    throw
} finally {
    $cleanupErrors = [System.Collections.Generic.List[string]]::new()
    foreach ($ownedProcess in @($process, $jobProcess)) {
        if ($ownedProcess) {
            try {
                if (-not $ownedProcess.HasExited) {
                    $ownedProcess.Kill()
                    Require ($ownedProcess.WaitForExit(5000)) 'Owned process did not exit during cleanup'
                }
            } catch {
                $cleanupErrors.Add($_.Exception.Message.Replace($secret, '<redacted>'))
            } finally { $ownedProcess.Dispose() }
        }
    }
    if ($childProcess) { $childProcess.Dispose() }
    try {
        $client.Dispose()
        foreach ($log in (Get-ChildItem -LiteralPath $evidenceRoot -File -Filter '*.log')) {
            $content = [IO.File]::ReadAllText($log.FullName)
            [IO.File]::WriteAllText($log.FullName, $content.Replace($secret, '<redacted>'))
        }
    } finally {
        $resolvedRun = [IO.Path]::GetFullPath($runRoot)
        $expectedParent = [IO.Path]::GetFullPath((Join-Path $repoRoot '.cache')) + [IO.Path]::DirectorySeparatorChar
        if ($resolvedRun.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedRun)) {
            try { Remove-Item -LiteralPath $resolvedRun -Recurse -Force } catch {
                $cleanupErrors.Add($_.Exception.Message.Replace($secret, '<redacted>'))
            }
        }
        $summary.cleanup = if ($cleanupErrors.Count) { 'Fail' } else { 'Pass: owned processes exited and temporary session removed' }
        if ($cleanupErrors.Count) {
            $summary.result = 'Fail'
            $summary.cleanup_errors = @($cleanupErrors.ToArray())
        }
        $summary.completed_at_utc = [DateTime]::UtcNow.ToString('o')
        ($summary | ConvertTo-Json -Depth 8).Replace($secret, '<redacted>') | Set-Content (Join-Path $evidenceRoot 'summary.json') -Encoding utf8
        (ConvertTo-Json -InputObject @($script:records.ToArray()) -Depth 20).Replace($secret, '<redacted>') | Set-Content (Join-Path $evidenceRoot 'api.json') -Encoding utf8
    }
    if ($cleanupErrors.Count) { throw 'Owned process cleanup failed; see summary.json' }
}
Write-Output "PASS: real Mihomo validation/auth/API/readback/restart; listeners=$($summary.listeners); Job=$($summary.job)"
