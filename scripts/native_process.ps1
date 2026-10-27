#Requires -Version 7.2

function Invoke-NativeStep {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$Executable,
        [string[]]$Arguments = @(),
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [Parameter(Mandatory)][string]$LogPath,
        [Text.Encoding]$TextEncoding = [Text.Encoding]::UTF8,
        [ValidateRange(1, 7200)][int]$TimeoutSeconds = 120
    )

    $resolvedExecutable = (Get-Command $Executable -CommandType Application -ErrorAction Stop).Source
    $resolvedWorkingDirectory = (Resolve-Path -LiteralPath $WorkingDirectory).Path
    $fullLogPath = [IO.Path]::GetFullPath($LogPath, $resolvedWorkingDirectory)
    $null = New-Item -ItemType Directory -Path (Split-Path $fullLogPath) -Force
    $startInfo = [Diagnostics.ProcessStartInfo]::new($resolvedExecutable)
    $startInfo.WorkingDirectory = $resolvedWorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    # The caller must select the child's wire encoding. UTF-8 is the default
    # for our tools; do not infer it from the parent's console code page.
    $startInfo.StandardOutputEncoding = $TextEncoding
    $startInfo.StandardErrorEncoding = $TextEncoding
    foreach ($argument in $Arguments) { $startInfo.ArgumentList.Add($argument) }

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $started = [DateTime]::UtcNow
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $stdoutTask = $null
    $stderrTask = $null
    $timedOut = $false
    $exitCode = $null
    try {
        if (-not $process.Start()) { throw "Could not start $resolvedExecutable" }
        # Drain both pipes concurrently; compiler output must not block the child.
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $timedOut = $true
            # Only this step and its descendants are owned by the runner.
            $process.Kill($true)
            if (-not $process.WaitForExit(5000)) { throw 'Timed-out process did not exit' }
        }
        $exitCode = $process.ExitCode
    } finally {
        try {
            if ($stdoutTask) {
                if (-not $process.HasExited) {
                    $process.Kill($true)
                    $null = $process.WaitForExit(5000)
                }
                # A surviving descendant holding a pipe must not hang the runner either.
                foreach ($task in @($stdoutTask, $stderrTask)) {
                    if (-not $task.Wait(5000)) { throw 'Native output pipe did not close' }
                }
                $stdout = $stdoutTask.GetAwaiter().GetResult()
                $stderr = $stderrTask.GetAwaiter().GetResult()
                [IO.File]::WriteAllText($fullLogPath, $stdout + "`n--- stderr ---`n" + $stderr)
                [ordered]@{
                    executable = $resolvedExecutable
                    arguments = $Arguments
                    working_directory = $resolvedWorkingDirectory
                    text_encoding = $TextEncoding.WebName
                    started_at_utc = $started.ToString('o')
                    elapsed_ms = $timer.ElapsedMilliseconds
                    exit_code = $exitCode
                    timed_out = $timedOut
                } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath "$fullLogPath.json" -Encoding utf8
                if ($stdout) { Write-Host $stdout.TrimEnd() }
                if ($stderr) { Write-Host $stderr.TrimEnd() }
            }
        } finally {
            $process.Dispose()
        }
    }
    if ($timedOut) { throw "Native step timed out after $TimeoutSeconds seconds. Log: $fullLogPath" }
    if ($exitCode -ne 0) { throw "Native step exited $exitCode. Log: $fullLogPath" }
}
