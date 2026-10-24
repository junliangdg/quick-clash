#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$Executable = 'dist/p2_windows/bin/quickclash.exe',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Fixture = 'tests/fixtures/mvp/inline_valid.yaml',
    [ValidateRange(10, 30)][int]$Seconds = 10
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows only.' }
. (Join-Path $PSScriptRoot 'process_memory.ps1')
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskFixture = [IO.Path]::GetFullPath($Fixture, $taskRepo)
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory, $taskRepo)
if (Test-Path -LiteralPath $taskOutput) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory -Path $taskOutput | Out-Null
function Get-TaskPort {
    $taskListener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $taskListener.Start()
    try { return $taskListener.LocalEndpoint.Port } finally { $taskListener.Stop() }
}
$taskMixed = Get-TaskPort
$taskController = Get-TaskPort
while ($taskMixed -eq $taskController) { $taskController = Get-TaskPort }
$taskState = Join-Path $taskOutput 'state.json'
$taskStart = [Diagnostics.ProcessStartInfo]::new()
$taskStart.FileName = [IO.Path]::GetFullPath($Executable, $taskRepo)
$taskStart.WorkingDirectory = $taskRepo
$taskStart.UseShellExecute = $false
$taskStart.CreateNoWindow = $true
$taskStart.RedirectStandardOutput = $true
$taskStart.RedirectStandardError = $true
foreach ($taskArgument in @('--isolated', '--no-tray', "--runtime-probe=$taskFixture", "--probe-mixed-port=$taskMixed", "--probe-controller-port=$taskController", "--runtime-state=$taskState", "--quit-after-ms=$(($Seconds + 35) * 1000)")) { $taskStart.ArgumentList.Add($taskArgument) }
$taskProcess = $null
$taskCore = $null
$taskRows = [Collections.Generic.List[object]]::new()
try {
    $taskProcess = [Diagnostics.Process]::Start($taskStart)
    $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
    $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
    $taskClock = [Diagnostics.Stopwatch]::StartNew()
    $taskReady = $false
    while ($taskClock.Elapsed.TotalSeconds -lt 25) {
        if ($taskProcess.HasExited) { throw 'Client exited before Ready.' }
        if (Test-Path -LiteralPath $taskState) {
            $taskValue = Get-Content -LiteralPath $taskState -Raw | ConvertFrom-Json
            if ($taskValue.ready -and -not $taskValue.busy -and $taskValue.corePid -gt 0) { $taskReady = $true; break }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $taskReady) { throw 'Runtime Ready timeout; check state and logs.' }
    $taskCore = [Diagnostics.Process]::GetProcessById($taskValue.corePid)
    $taskClock.Restart()
    while ($taskClock.Elapsed.TotalSeconds -lt $Seconds) {
        if ($taskProcess.HasExited -or $taskCore.HasExited) { throw 'Runtime stopped during sample.' }
        $taskClientMemory = [QuickClash.Diagnostics.ProcessMemory]::Read($taskProcess.Handle)
        $taskCoreMemory = [QuickClash.Diagnostics.ProcessMemory]::Read($taskCore.Handle)
        $taskRows.Add([pscustomobject]@{
            seconds = $taskClock.Elapsed.TotalSeconds; client_pid = $taskProcess.Id; core_pid = $taskCore.Id
            client_private_working_set_bytes = $taskClientMemory.PrivateWorkingSetSize.ToUInt64()
            client_working_set_bytes = $taskClientMemory.WorkingSetSize.ToUInt64()
            client_private_commit_bytes = $taskClientMemory.PrivateUsage.ToUInt64()
            core_private_working_set_bytes = $taskCoreMemory.PrivateWorkingSetSize.ToUInt64()
            core_working_set_bytes = $taskCoreMemory.WorkingSetSize.ToUInt64()
            core_private_commit_bytes = $taskCoreMemory.PrivateUsage.ToUInt64()
        })
        Start-Sleep -Milliseconds 100
    }
    $taskRows | Export-Csv -LiteralPath (Join-Path $taskOutput 'memory.csv') -NoTypeInformation
    $taskPeak = ($taskRows | Measure-Object client_private_working_set_bytes -Maximum).Maximum
    [ordered]@{
        result = if ($taskPeak -le 100000000) { 'Pass' } else { 'Fail' }
        executable_sha256 = (Get-FileHash -LiteralPath $taskStart.FileName -Algorithm SHA256).Hash.ToLowerInvariant()
        scenario = 'Real Qt Quick home + persistent storage worker + official owned Ready core; local import/selection; system proxy untouched; no replay or sustained traffic'
        fixture = [IO.Path]::GetFileName($taskFixture)
        client_process_group = @($taskProcess.Id); auxiliary_client_processes = @(); core_pid = $taskCore.Id
        seconds = $Seconds; samples = $taskRows.Count; sample_interval_ms = 100
        client_private_working_set_peak_bytes = $taskPeak
        client_working_set_peak_bytes = ($taskRows | Measure-Object client_working_set_bytes -Maximum).Maximum
        client_private_commit_peak_bytes = ($taskRows | Measure-Object client_private_commit_bytes -Maximum).Maximum
        core_private_working_set_peak_bytes = ($taskRows | Measure-Object core_private_working_set_bytes -Maximum).Maximum
        core_working_set_peak_bytes = ($taskRows | Measure-Object core_working_set_bytes -Maximum).Maximum
        core_private_commit_peak_bytes = ($taskRows | Measure-Object core_private_commit_bytes -Maximum).Maximum
        limitations = 'One development short sample; no sustained download, import peak, hidden/rebuild, CPU/frame/GPU or long-term acceptance.'
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskOutput 'summary.json') -Encoding utf8NoBOM
    if ($taskPeak -gt 100000000) { throw 'Private working set exceeds 100 MB.' }
} finally {
    if ($taskProcess) {
        # Let the bounded probe exit normally, including lease/session/worker cleanup.
        if (-not $taskProcess.HasExited) { $taskProcess.WaitForExit(50000) | Out-Null }
        if (-not $taskProcess.HasExited) { $taskProcess.Kill($true); $taskProcess.WaitForExit() }
        $taskStdout.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $taskOutput 'stdout.log')
        $taskStderr.GetAwaiter().GetResult() | Set-Content -LiteralPath (Join-Path $taskOutput 'stderr.log')
        [ordered]@{ client_exit_code = $taskProcess.ExitCode; owned_core_exited = if ($taskCore) { $taskCore.HasExited } else { $null } } |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskOutput 'cleanup.json') -Encoding utf8NoBOM
        $taskProcess.Dispose()
    }
    if ($taskCore) { $taskCore.Dispose() }
}
