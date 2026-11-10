#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/p1_windows',
    [string]$Destination = 'dist/p1_windows',
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows deployment smoke only.' }
$taskRepo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'native_process.ps1')
$taskBuild = (Resolve-Path -LiteralPath ([IO.Path]::GetFullPath($BuildDirectory, $taskRepo))).Path
$taskTesting = Select-String -LiteralPath (Join-Path $taskBuild 'CMakeCache.txt') -Pattern '^BUILD_TESTING:BOOL=ON$' -Quiet
# Qt 6.8 qt.conf generation requires an absolute install prefix.
$taskDestination = [IO.Path]::GetFullPath($Destination, $taskRepo)
$taskCMake = (Get-Command $CMake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$taskLogs = Join-Path $taskBuild ('logs/deploy_' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ'))
Invoke-NativeStep -Executable $taskCMake -Arguments @('--install', $taskBuild, '--prefix', $taskDestination) -WorkingDirectory $taskRepo -LogPath (Join-Path $taskLogs 'install.log') -TimeoutSeconds 120
$taskOriginalPath = $env:Path
$taskOriginalPlatform = $env:QT_QPA_PLATFORM
$taskOriginalBackend = $env:QT_QUICK_BACKEND
$taskOriginalLogging = $env:QT_FORCE_STDERR_LOGGING
try {
    $env:Path = "$env:SystemRoot/System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_QUICK_BACKEND = 'software'
    $env:QT_FORCE_STDERR_LOGGING = '1'
    [string[]]$taskSmokeArguments = if ($taskTesting) { @('--smoke-test') } else { @('--isolated', '--quit-after-ms=1500') }
    $taskSmokeArguments += "--diagnostics=$taskLogs/smoke.json"
    Invoke-NativeStep -Executable (Join-Path $taskDestination 'bin/quickclash.exe') -Arguments $taskSmokeArguments -WorkingDirectory $taskDestination -LogPath (Join-Path $taskLogs 'smoke.log') -TimeoutSeconds 15
    $taskSmoke = Get-Content -LiteralPath (Join-Path $taskLogs 'smoke.json') -Raw | ConvertFrom-Json
    if (-not $taskSmoke.page_ready -or $taskSmoke.qml_errors -ne 0) { throw 'Deployed application did not load its QML page cleanly.' }
    $taskBuildInfo = & (Join-Path $PSScriptRoot 'read_build_info.ps1') -Path (Join-Path $taskDestination 'metadata/build_info.json')
    if ($taskSmoke.application_version -cne $taskBuildInfo.application_version -or
        $taskSmoke.base_version -cne $taskBuildInfo.base_version -or
        $taskSmoke.source_commit -cne $taskBuildInfo.commit -or
        $taskSmoke.source_state -cne $taskBuildInfo.source_state) {
        throw 'Deployed application differs from the compiled build identity.'
    }
} finally {
    $env:Path = $taskOriginalPath
    $env:QT_QPA_PLATFORM = $taskOriginalPlatform
    $env:QT_QUICK_BACKEND = $taskOriginalBackend
    $env:QT_FORCE_STDERR_LOGGING = $taskOriginalLogging
}
Write-Output "Deployment smoke passed: $taskLogs. This is not clean-machine or P2 offline-package acceptance."
