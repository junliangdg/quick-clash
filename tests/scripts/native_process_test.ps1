#Requires -Version 7.2
[CmdletBinding()]
param([string]$EvidenceDirectory = '')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $repoRoot 'scripts/native_process.ps1')
if (-not $EvidenceDirectory) {
    $EvidenceDirectory = 'build/script_tests/native_process/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '_' + [guid]::NewGuid().ToString('N')
}
$runRoot = [IO.Path]::GetFullPath($EvidenceDirectory, $repoRoot)
if (Test-Path -LiteralPath $runRoot) { throw "Use a new evidence directory: $runRoot" }
$null = New-Item -ItemType Directory -Path $runRoot
Write-Output "Native process test evidence: $runRoot"
$shell = (Get-Process -Id $PID).Path
[ordered]@{
    powershell = $PSVersionTable.PSVersion.ToString()
    os = [Runtime.InteropServices.RuntimeInformation]::OSDescription
    console_output_encoding = [Console]::OutputEncoding.WebName
    console_input_encoding = [Console]::InputEncoding.WebName
    pipe_encoding = 'utf-8'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runRoot 'environment.json') -Encoding utf8
function Require($Condition, $Message) { if (-not $Condition) { throw $Message } }
try {
    $fixture = Join-Path $runRoot 'fixture.ps1'
    @'
param([string]$Mode, [string]$Value)
# A redirected Windows pwsh otherwise inherits the OEM console code page.
# Match the process runner's explicit UTF-8 pipe contract on every platform.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
if ($Mode -eq 'output') {
    [Console]::Out.WriteLine($Value)
    [Console]::Out.WriteLine('x' * 100000)
    [Console]::Error.WriteLine($Value)
    [Console]::Error.WriteLine('y' * 100000)
} elseif ($Mode -eq 'fail') {
    [Console]::Error.WriteLine('expected failure')
    exit 7
} elseif ($Mode -eq 'wait') {
    [IO.File]::WriteAllText($Value, [string]$PID)
    $childInfo = [Diagnostics.ProcessStartInfo]::new((Get-Process -Id $PID).Path)
    $childInfo.UseShellExecute = $false
    $childInfo.CreateNoWindow = $true
    foreach ($argument in @('-NoProfile', '-File', $PSCommandPath, 'child', "$Value.child")) {
        $childInfo.ArgumentList.Add($argument)
    }
    $child = [Diagnostics.Process]::Start($childInfo)
    $child.Dispose()
    Start-Sleep -Seconds 30
} elseif ($Mode -eq 'child') {
    [IO.File]::WriteAllText($Value, [string]$PID)
    Start-Sleep -Seconds 30
}
'@ | Set-Content -LiteralPath $fixture -Encoding utf8
    $value = '中文 path with spaces "quoted" $literal'
    $logPath = Join-Path $runRoot 'output.log'
    Invoke-NativeStep -Executable $shell -Arguments @('-NoProfile', '-File', $fixture, 'output', $value) -WorkingDirectory $runRoot -LogPath $logPath -TimeoutSeconds 10 6>$null
    $output = [IO.File]::ReadAllText($logPath)
    $streams = $output -split [regex]::Escape("`n--- stderr ---`n"), 2
    Require ($streams.Count -eq 2) 'Captured stream separator missing; inspect output.log'
    Require ($streams[0].Contains($value)) 'Unicode, spaces, quotes or literal dollar stdout argument did not round-trip; inspect output.log'
    Require ($streams[1].Contains($value)) 'Unicode stderr did not round-trip; inspect output.log'
    Require ($streams[0].Contains('x' * 100000)) 'Concurrent stdout capture lost data; inspect output.log'
    Require ($streams[1].Contains('y' * 100000)) 'Concurrent stderr capture lost data; inspect output.log'
    $failed = $false
    try {
        Invoke-NativeStep -Executable $shell -Arguments @('-NoProfile', '-File', $fixture, 'fail') -WorkingDirectory $runRoot -LogPath (Join-Path $runRoot 'fail.log') -TimeoutSeconds 10 6>$null
    } catch { $failed = $true }
    $failure = Get-Content -LiteralPath (Join-Path $runRoot 'fail.log.json') -Raw | ConvertFrom-Json
    Require ($failed -and $failure.exit_code -eq 7 -and -not $failure.timed_out) 'Nonzero exit was not preserved'
    $pidPath = Join-Path $runRoot 'wait_pid.txt'
    $timedOut = $false
    try {
        Invoke-NativeStep -Executable $shell -Arguments @('-NoProfile', '-File', $fixture, 'wait', $pidPath) -WorkingDirectory $runRoot -LogPath (Join-Path $runRoot 'timeout.log') -TimeoutSeconds 3 6>$null
    } catch { $timedOut = $true }
    $timeout = Get-Content -LiteralPath (Join-Path $runRoot 'timeout.log.json') -Raw | ConvertFrom-Json
    Require ($timedOut -and $timeout.timed_out) 'Timeout was not recorded'
    Require (Test-Path -LiteralPath $pidPath) 'Timeout fixture did not start'
    $childId = [int](Get-Content -LiteralPath $pidPath)
    Require (-not (Get-Process -Id $childId -ErrorAction SilentlyContinue)) 'Timed-out owned process survived'
    Require (Test-Path -LiteralPath "$pidPath.child") 'Descendant fixture did not start'
    $descendantId = [int](Get-Content -LiteralPath "$pidPath.child")
    Require (-not (Get-Process -Id $descendantId -ErrorAction SilentlyContinue)) 'Timed-out owned descendant survived'
    Write-Output 'PASS: native arguments, concurrent pipes, nonzero exit, timeout and owned process tree cleanup'
} finally {
    Write-Output "Retained native process test evidence: $runRoot"
}
