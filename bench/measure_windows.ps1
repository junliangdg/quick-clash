#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateRange(10, 600)][int]$Seconds = 30,
    [ValidateRange(3, 600)][int]$WarmupSeconds = 3,
    [ValidateRange(1, 10)][int]$Runs = 1,
    [ValidateSet(1000, 10000)][int]$ReplayConnections = 1000,
    [ValidateRange(20, 100)][int]$SampleIntervalMilliseconds = 100,
    [ValidateSet('all', 'light', 'dark')][string]$Theme = 'light',
    [ValidateSet('all', 'settings', 'home', 'proxies', 'connections', 'hidden', 'unloaded')][string]$Scenario = 'home'
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows collector only; macOS/Linux require native collectors.' }
. (Join-Path $PSScriptRoot 'process_memory.ps1')
$taskExecutable = (Resolve-Path -LiteralPath $Executable).Path
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $taskOutput) { throw 'Use a new evidence directory; existing runs must be preserved.' }
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskScenarios = if ($Scenario -eq 'all') { @('settings', 'home', 'proxies', 'connections', 'hidden', 'unloaded') } else { @($Scenario) }
$taskThemes = if ($Theme -eq 'all') { @('light', 'dark') } else { @($Theme) }
$taskEnvironment = [ordered]@{
    measured_at_utc = [DateTime]::UtcNow.ToString('o')
    os = [Environment]::OSVersion.VersionString
    runtime = [Environment]::Version.ToString()
    executable = $taskExecutable
    executable_sha256 = (Get-FileHash -LiteralPath $taskExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    metric = 'Record private working set, working set and private commit; CPU normalized to one logical core'
    memory_policy = 'Low memory design goal; compare same-environment baselines and bounded lifecycle behavior, with no fixed byte gate'
    sample_interval_ms = $SampleIntervalMilliseconds
    steady_seconds = $Seconds
    warmup_seconds = $WarmupSeconds
    runs_per_scenario_theme = $Runs
    themes = $taskThemes
    replay_connections = $ReplayConnections
    replay_scale = if ($ReplayConnections -eq 1000) { 'representative' } else { 'exploratory' }
    rendering_environment = [ordered]@{
        QT_QPA_PLATFORM = $env:QT_QPA_PLATFORM; QT_QUICK_BACKEND = $env:QT_QUICK_BACKEND
        QSG_RHI_BACKEND = $env:QSG_RHI_BACKEND; QSG_INFO = $env:QSG_INFO
        QSG_RENDER_LOOP = $env:QSG_RENDER_LOOP
        QT_SCALE_FACTOR = $env:QT_SCALE_FACTOR; QT_FONT_DPI = $env:QT_FONT_DPI
    }
    build_requirement = 'Release; record CMake cache, Qt/compiler, DPI/GPU/backend separately in report'
    scope = 'P1 isolated GUI replay process; no Mihomo/helper/update process exists in this prototype'
    exclusions = 'Collector process and screenshot/profiler tools; future necessary client processes must be aggregated'
    limitations = 'Sampled peaks can miss sub-interval spikes; no GPU attribution, scrolling, frame times or real business workload. Design 1.6 accepts single short runs for development; longer/repeated/frame/GPU investigations are optional when needed. Unsupported memory API is a collection failure, never replaced by another metric.'
}
$taskEnvironment | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'environment.json') -Encoding utf8NoBOM
$taskSummaries = [Collections.Generic.List[object]]::new()
foreach ($taskScenario in $taskScenarios) {
    foreach ($taskTheme in $taskThemes) {
        for ($taskRun = 1; $taskRun -le $Runs; $taskRun++) {
            $taskName = "${taskScenario}_${taskTheme}_$taskRun"
            $taskRows = [Collections.Generic.List[object]]::new()
            $taskDiagnostics = Join-Path $taskOutput ($taskName + '_diagnostics.json')
            $taskSummary = [ordered]@{
                scenario = $taskScenario; theme = $taskTheme; run = $taskRun
                execution_result = 'Running'; memory_measurement_result = 'NotRun'
            }
            $taskStart = [Diagnostics.ProcessStartInfo]::new()
            $taskStart.FileName = $taskExecutable
            $taskStart.WorkingDirectory = Split-Path $taskExecutable
            $taskStart.UseShellExecute = $false
            $taskStart.RedirectStandardOutput = $true
            $taskStart.RedirectStandardError = $true
            $taskPage = if ($taskScenario -in @('settings', 'proxies', 'connections')) { $taskScenario } else { 'home' }
            foreach ($taskArgument in @('--isolated', '--replay', "--replay-connections=$ReplayConnections", "--theme=$taskTheme", "--page=$taskPage", "--scenario=$taskScenario", "--diagnostics=$taskDiagnostics", "--quit-after-ms=$(($WarmupSeconds + $Seconds + 2) * 1000)")) { $taskStart.ArgumentList.Add($taskArgument) }
            $taskProcess = $null
            $taskStdout = $null
            $taskStderr = $null
            $taskClock = [Diagnostics.Stopwatch]::StartNew()
            $taskPreviousCpu = 0.0
            $taskPreviousTime = 0.0
            $taskModulesRecorded = $false
            try {
                $taskProcess = [Diagnostics.Process]::Start($taskStart)
                $taskStdout = $taskProcess.StandardOutput.ReadToEndAsync()
                $taskStderr = $taskProcess.StandardError.ReadToEndAsync()
                while ($taskClock.Elapsed.TotalSeconds -lt ($WarmupSeconds + $Seconds)) {
                    if ($taskProcess.HasExited) { throw "Prototype exited early with code $($taskProcess.ExitCode)" }
                    $taskProcess.Refresh()
                    $taskNow = $taskClock.Elapsed.TotalSeconds
                    if (-not $taskModulesRecorded -and $taskNow -ge $WarmupSeconds) {
                        try {
                            @($taskProcess.Modules | ForEach-Object { [pscustomobject]@{ name = $_.ModuleName; image_bytes = $_.ModuleMemorySize; file = $_.FileName } }) |
                                ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $taskOutput ($taskName + '_modules.json')) -Encoding utf8NoBOM
                        } catch {
                            $_.Exception.Message | Set-Content -LiteralPath (Join-Path $taskOutput ($taskName + '_modules_error.log')) -Encoding utf8NoBOM
                        }
                        $taskModulesRecorded = $true
                    }
                    $taskCpu = $taskProcess.TotalProcessorTime.TotalSeconds
                    $taskMemory = [QuickClash.Diagnostics.ProcessMemory]::Read($taskProcess.Handle)
                    $taskCpuPercent = if ($taskNow -gt $taskPreviousTime) { 100.0 * ($taskCpu - $taskPreviousCpu) / ($taskNow - $taskPreviousTime) } else { 0.0 }
                    $taskRows.Add([pscustomobject]@{
                        seconds = $taskNow; pid = $taskProcess.Id
                        private_bytes = $taskMemory.PrivateUsage.ToUInt64()
                        working_set_bytes = $taskMemory.WorkingSetSize.ToUInt64()
                        private_working_set_bytes = $taskMemory.PrivateWorkingSetSize.ToUInt64()
                        handles = $taskProcess.HandleCount
                        threads = $taskProcess.Threads.Count
                        cpu_total_seconds = $taskCpu
                        cpu_one_core_percent = $taskCpuPercent
                        phase = if ($taskNow -lt $WarmupSeconds) { 'startup_warmup' } else { 'steady' }
                    })
                    $taskPreviousCpu = $taskCpu
                    $taskPreviousTime = $taskNow
                    Start-Sleep -Milliseconds $SampleIntervalMilliseconds
                }
                $taskProcess.WaitForExit(10000) | Out-Null
                if (-not $taskProcess.HasExited) { throw 'Prototype did not exit within its deadline' }
                if ($taskProcess.ExitCode -ne 0) { throw "Prototype exit code $($taskProcess.ExitCode)" }
                if (-not (Test-Path -LiteralPath $taskDiagnostics)) { throw 'Missing lifecycle diagnostics' }
                $taskState = Get-Content -LiteralPath $taskDiagnostics -Raw | ConvertFrom-Json
                if ($taskState.qml_errors -ne 0) { throw 'QML warnings/errors occurred' }
                if ($taskScenario -in @('hidden', 'unloaded') -and ($taskState.active_demands -ne 0 -or $taskState.replay_running)) { throw 'Hidden prototype retained page demand/timer' }
                if ($taskScenario -eq 'hidden' -and $taskState.host_state -ne 'Hidden') { throw 'Hidden scenario did not hide the window' }
                if ($taskScenario -eq 'unloaded' -and $taskState.host_state -ne 'Unloaded') { throw 'Unload scenario did not release the engine' }
                if ($taskScenario -notin @('hidden', 'unloaded') -and (-not $taskState.page_ready -or $taskState.page -ne $taskPage)) { throw 'Visible scenario did not load the requested page' }
                if ($taskScenario -in @('home', 'proxies', 'connections') -and $taskState.active_demands -lt 1) { throw 'Visible data page did not register its demand' }
                if ($taskScenario -eq 'settings' -and ($taskState.active_demands -ne 0 -or $taskState.replay_running)) { throw 'Static settings page retained a data demand/timer' }
                $taskSummary.execution_result = 'Pass'
                $taskSummary.lifecycle = $taskState
            } catch {
                $taskSummary.execution_result = 'Fail'
                $taskSummary.error = $_.Exception.Message
            } finally {
                $taskRows | Export-Csv -LiteralPath (Join-Path $taskOutput ($taskName + '.csv')) -NoTypeInformation -Encoding utf8NoBOM
                if ($null -ne $taskProcess) {
                    if (-not $taskProcess.HasExited) { $taskProcess.Kill($true); $taskProcess.WaitForExit(5000) | Out-Null }
                    if ($null -ne $taskStdout -and $taskStdout.Wait(2000)) { $taskStdout.Result | Set-Content -LiteralPath (Join-Path $taskOutput ($taskName + '_stdout.log')) -Encoding utf8NoBOM }
                    if ($null -ne $taskStderr -and $taskStderr.Wait(2000)) { $taskStderr.Result | Set-Content -LiteralPath (Join-Path $taskOutput ($taskName + '_stderr.log')) -Encoding utf8NoBOM }
                    $taskProcess.Dispose()
                }
                $taskPeakPrivate = ($taskRows | Measure-Object private_bytes -Maximum).Maximum
                $taskPeakWorkingSet = ($taskRows | Measure-Object working_set_bytes -Maximum).Maximum
                $taskPeakPrivateWorkingSet = ($taskRows | Measure-Object private_working_set_bytes -Maximum).Maximum
                $taskSteady = @($taskRows | Where-Object phase -eq 'steady')
                $taskSummary.peak_private_bytes = $taskPeakPrivate
                $taskSummary.peak_working_set_bytes = $taskPeakWorkingSet
                $taskSummary.peak_private_working_set_bytes = $taskPeakPrivateWorkingSet
                $taskSummary.steady_private_bytes_max = ($taskSteady | Measure-Object private_bytes -Maximum).Maximum
                $taskSummary.steady_working_set_bytes_max = ($taskSteady | Measure-Object working_set_bytes -Maximum).Maximum
                $taskSummary.steady_private_working_set_bytes_max = ($taskSteady | Measure-Object private_working_set_bytes -Maximum).Maximum
                $taskSummary.steady_handles_first = if ($taskSteady.Count) { $taskSteady[0].handles } else { $null }
                $taskSummary.steady_handles_last = if ($taskSteady.Count) { $taskSteady[-1].handles } else { $null }
                $taskSummary.steady_threads_max = ($taskSteady | Measure-Object threads -Maximum).Maximum
                $taskSummary.steady_cpu_one_core_percent_average = if ($taskSteady.Count -gt 1) {
                    $taskFirst = $taskSteady[0]
                    $taskLast = $taskSteady[-1]
                    100.0 * ($taskLast.cpu_total_seconds - $taskFirst.cpu_total_seconds) / ($taskLast.seconds - $taskFirst.seconds)
                } else { $null }
                if ($taskRows.Count -gt 0) {
                    $taskSummary.memory_measurement_result = if ($taskSteady.Count -gt 0) { 'Sampled' } else { 'NotRun' }
                }
                $taskSummaries.Add([pscustomobject]$taskSummary)
                ConvertTo-Json -InputObject @($taskSummaries.ToArray()) -Depth 6 | Set-Content -LiteralPath (Join-Path $taskOutput 'summary.json') -Encoding utf8NoBOM
            }
        }
    }
}
Write-Output "Saved raw samples to $taskOutput. P1 replay evidence may support design 1.6 G1 review; it does not prove G2 proxy, long-term CPU/frame/GPU or product performance."
if (@($taskSummaries | Where-Object { $_.execution_result -ne 'Pass' -or $_.memory_measurement_result -ne 'Sampled' }).Count -gt 0) { throw 'One or more scenarios failed to execute or collect memory samples; see summary.json' }
