[CmdletBinding()]
param([string]$BuildDirectory = 'build/dev', [switch]$ApiOnly)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$matrix = Get-Content (Join-Path $repoRoot 'docs/baseline/release_matrix.json') -Raw | ConvertFrom-Json
$core = Join-Path $repoRoot '.cache/mihomo/mihomo.exe'
if ((Get-FileHash $core -Algorithm SHA256).Hash -ine $matrix.mihomo.windows.executable_sha256) { throw 'Core hash mismatch' }
$runRoot = Join-Path $repoRoot ('.cache/core_probe_' + [guid]::NewGuid().ToString('N'))
New-Item $runRoot -ItemType Directory | Out-Null
$evidenceRoot = Join-Path $repoRoot 'docs/progress/reports/p0_core'
New-Item $evidenceRoot -ItemType Directory -Force | Out-Null
$handler = [System.Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(3)
$secret = [Convert]::ToHexString([System.Security.Cryptography.RandomNumberGenerator]::GetBytes(32))
$client.DefaultRequestHeaders.Authorization = [System.Net.Http.Headers.AuthenticationHeaderValue]::new('Bearer', $secret)
$script:records = [System.Collections.Generic.List[object]]::new()
$process = $null
$jobProcess = $null
function Require($Condition, $Message) { if (-not $Condition) { throw $Message } }
function FreePort {
    $listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return $listener.LocalEndpoint.Port } finally { $listener.Stop() }
}
$mixedPort = FreePort
do { $controllerPort = FreePort } while ($controllerPort -eq $mixedPort)
$script:baseUrl = "http://127.0.0.1:$controllerPort"
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
        try { return Request GET '/version' } catch { Start-Sleep -Milliseconds 100 }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'Core Ready deadline exceeded'
}
try {
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
    $validation = & $core -t -d $runRoot -f $configPath 2>&1
    $validationExit = $LASTEXITCODE
    Require ($validationExit -eq 0) 'Valid configuration rejected'
    $after = @(Get-ChildItem $runRoot -File | ForEach-Object Name)
    $sideEffects = @($after | Where-Object { $_ -notin $before })
    Set-Content (Join-Path $runRoot 'invalid.yaml') 'proxies: ['
    $invalidOutput = & $core -t -d $runRoot -f (Join-Path $runRoot 'invalid.yaml') 2>&1
    $invalidExit = $LASTEXITCODE
    Require ($invalidExit -ne 0) 'Invalid configuration accepted'
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
    $listenerResult = 'NotRun (API-only scope)'
    if (-not $ApiOnly) {
    $listeners = @(Get-NetTCPConnection -State Listen -OwningProcess $process.Id)
    Require (@($listeners | Where-Object { $_.LocalPort -eq $mixedPort -and $_.LocalAddress -eq '127.0.0.1' }).Count -eq 1) 'Mixed listener ownership mismatch'
    Require (@($listeners | Where-Object { $_.LocalPort -eq $controllerPort -and $_.LocalAddress -eq '127.0.0.1' }).Count -eq 1) 'Controller ownership mismatch'
    $listenerResult = 'Pass: owned PID and loopback verified'
    }
    $process.Kill()
    Require ($process.WaitForExit(5000)) 'Core exit deadline exceeded'
    $process.Dispose()
    $process = LaunchCore
    $null = WaitReady
    Require ((Request GET "/proxies/$group").now -eq 'DIRECT') 'Disabled selection persistence did not reset'
    $process.Kill()
    Require ($process.WaitForExit(5000)) 'Restarted core exit deadline exceeded'
    $process.Dispose()
    $process = $null
    $jobResult = 'NotRun'
    if (-not $ApiOnly) {
    $pidPath = Join-Path $runRoot 'child_pid.txt'
    $jobInfo = [System.Diagnostics.ProcessStartInfo]::new((Join-Path $repoRoot "$BuildDirectory/core_job_probe.exe"))
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
    $jobResult = 'Pass: suspended create -> assign -> resume -> owner kill -> child exit'
    }
    $summary = [ordered]@{api_result='Pass';tag=$matrix.mihomo.tag;valid_exit=$validationExit;invalid_exit=$invalidExit;validation_new_files=$sideEffects;authentication='401 with wrong secret';selection='encoded Unicode and reserved characters, readback and restart reset';modes='rule/global/direct readback';reload='PUT /configs payload';listeners=$listenerResult;job=$jobResult;system_proxy='untouched';geo='unsupported';validation_output=($validation -join [Environment]::NewLine);invalid_output=($invalidOutput -join [Environment]::NewLine)}
    $summary | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $evidenceRoot 'summary.json') -Encoding utf8
    ($script:records | ConvertTo-Json -Depth 20).Replace($secret, '<redacted>') | Set-Content (Join-Path $evidenceRoot 'api.json') -Encoding utf8
    Write-Output "PASS: real Mihomo validation/auth/API/readback/restart; listeners=$listenerResult; Job=$jobResult"
} finally {
    foreach ($ownedProcess in @($process, $jobProcess)) {
        if ($ownedProcess) {
            if (-not $ownedProcess.HasExited) { $ownedProcess.Kill(); $null = $ownedProcess.WaitForExit(5000) }
            $ownedProcess.Dispose()
        }
    }
    $client.Dispose()
    $resolvedRun = [IO.Path]::GetFullPath($runRoot)
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $repoRoot '.cache')) + [IO.Path]::DirectorySeparatorChar
    if ($resolvedRun.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedRun -Recurse -Force
    }
}
