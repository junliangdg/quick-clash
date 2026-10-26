#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][int[]]$ClientPid,
    [int[]]$CorePid = @(),
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(1, 30)][int]$Seconds = 10,
    [ValidateRange(20, 1000)][int]$IntervalMs = 100
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows only.' }
$allIds = @($ClientPid) + @($CorePid)
if (@($allIds | Where-Object { $_ -le 0 }).Count -or
    @($allIds | Select-Object -Unique).Count -ne $allIds.Count) {
    throw 'Use positive, distinct PIDs; include necessary services in ClientPid, not CorePid.'
}
. (Join-Path $PSScriptRoot 'process_memory.ps1')
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a new evidence directory.' }
$processes = @()
$rows = [Collections.Generic.List[object]]::new()
$status = 'Sampling'
try {
    # Keep identities to reject PID reuse. Never inspect command lines or application data.
    foreach ($processId in $allIds) {
        $process = [Diagnostics.Process]::GetProcessById($processId)
        $processes += [pscustomobject]@{ process = $process; started = $process.StartTime.ToUniversalTime() }
        $null = [QuickClash.Diagnostics.ProcessMemory]::ReadPid($processId)
    }
    New-Item -ItemType Directory -Path $output | Out-Null
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $sample = 0
    while ($clock.Elapsed.TotalSeconds -lt $Seconds) {
        ++$sample
        foreach ($entry in $processes) {
            $process = $entry.process
            $process.Refresh()
            if ($process.HasExited -or $process.StartTime.ToUniversalTime() -ne $entry.started) {
                throw 'Specified process exited or identity changed; sample incomplete.'
            }
            $memory = [QuickClash.Diagnostics.ProcessMemory]::ReadPid($process.Id)
            $rows.Add([pscustomobject]@{
                sample = $sample
                seconds = $clock.Elapsed.TotalSeconds
                pid = $process.Id
                role = if ($ClientPid -contains $process.Id) { 'client' } else { 'core' }
                private_working_set_bytes = $memory.PrivateWorkingSetSize.ToUInt64()
                working_set_bytes = $memory.WorkingSetSize.ToUInt64()
                private_commit_bytes = $memory.PrivateUsage.ToUInt64()
            })
        }
        Start-Sleep -Milliseconds $IntervalMs
    }
    $status = 'Sampled'
} catch {
    $status = 'Incomplete'
    throw
} finally {
    if (Test-Path -LiteralPath $output) {
        $rows | Export-Csv -LiteralPath (Join-Path $output 'memory.csv') -NoTypeInformation
        [ordered]@{
            status = $status; client_pids = @($ClientPid); core_pids = @($CorePid)
            requested_seconds = $Seconds; interval_ms = $IntervalMs; rows = $rows.Count
            limitations = 'Query-only explicit PIDs. No executable/source identity claim, automatic child discovery, product acceptance or memory-budget Pass. Include all required client services explicitly. Core restarts require a new sample.'
        } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output 'summary.json') -Encoding utf8NoBOM
    }
    foreach ($entry in $processes) { $entry.process.Dispose() }
}
