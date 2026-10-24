#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$Executable = 'build/p1_windows/qt_cost_probe.exe',
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(2, 60)][int]$Seconds = 3
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'process_memory.ps1')
$taskExecutable = (Resolve-Path -LiteralPath $Executable).Path
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $taskOutput) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskSummary = [Collections.Generic.List[object]]::new()
foreach ($taskStage in @('application', 'quick_window', 'controls')) {
    foreach ($taskFont in @('default', 'system_cjk', 'application_cjk')) {
        $taskName = $taskStage + '_' + $taskFont
        $taskRows = [Collections.Generic.List[object]]::new()
        $taskStart = [Diagnostics.ProcessStartInfo]::new($taskExecutable)
        $taskStart.UseShellExecute = $false
        $taskStart.RedirectStandardOutput = $true
        $taskStart.RedirectStandardError = $true
        $taskStart.Environment['PATH'] = 'C:/Qt/6.8.0/msvc2022_64/bin;' + $env:PATH
        $taskStart.Environment['QSG_RENDER_LOOP'] = 'basic'
        $taskStart.Environment['QT_FORCE_STDERR_LOGGING'] = '1'
        foreach ($taskArgument in @("--stage=$taskStage", "--font=$taskFont", "--diagnostics=$taskOutput/$taskName.json", "--quit-after-ms=$(($Seconds + 5) * 1000)")) { $taskStart.ArgumentList.Add($taskArgument) }
        if ($taskStage -eq 'controls') { $taskStart.ArgumentList.Add("--capture=$taskOutput/$taskName.png") }
        $taskProcess = [Diagnostics.Process]::Start($taskStart)
        $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
        $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
        $taskClock = [Diagnostics.Stopwatch]::StartNew()
        try {
            while ($taskClock.Elapsed.TotalSeconds -lt ($Seconds + 3)) {
                if ($taskProcess.HasExited) { throw "Cost probe exited early: $($taskProcess.ExitCode)" }
                $taskMemory = [QuickClash.Diagnostics.ProcessMemory]::Read($taskProcess.Handle)
                $taskRows.Add([pscustomobject]@{
                    seconds = $taskClock.Elapsed.TotalSeconds
                    private_working_set_bytes = $taskMemory.PrivateWorkingSetSize.ToUInt64()
                    working_set_bytes = $taskMemory.WorkingSetSize.ToUInt64()
                    private_bytes = $taskMemory.PrivateUsage.ToUInt64()
                })
                Start-Sleep -Milliseconds 100
            }
            if (-not $taskProcess.WaitForExit(5000) -or $taskProcess.ExitCode -ne 0) { throw 'Cost probe failed to exit successfully.' }
            $taskSummary.Add([pscustomobject]@{
                stage = $taskStage; font_mode = $taskFont
                peak_private_working_set_bytes = ($taskRows | Measure-Object private_working_set_bytes -Maximum).Maximum
                peak_working_set_bytes = ($taskRows | Measure-Object working_set_bytes -Maximum).Maximum
                peak_private_bytes = ($taskRows | Measure-Object private_bytes -Maximum).Maximum
            })
        } finally {
            if (-not $taskProcess.HasExited) { $taskProcess.Kill($true); $taskProcess.WaitForExit(5000) | Out-Null }
            $taskRows | Export-Csv -LiteralPath "$taskOutput/$taskName.csv" -NoTypeInformation -Encoding utf8NoBOM
            $taskStdout.GetAwaiter().GetResult() | Set-Content -LiteralPath "$taskOutput/$taskName.stdout.log" -Encoding utf8NoBOM
            $taskStderr.GetAwaiter().GetResult() | Set-Content -LiteralPath "$taskOutput/$taskName.stderr.log" -Encoding utf8NoBOM
            $taskProcess.Dispose()
            ConvertTo-Json -InputObject @($taskSummary.ToArray()) -Depth 4 | Set-Content -LiteralPath "$taskOutput/summary.json" -Encoding utf8NoBOM
        }
    }
}
$taskSummary | Format-Table
