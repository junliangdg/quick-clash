#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$QtRoot = '',
    [string]$VsInstallPath = '',
    [string]$CMake = 'cmake',
    [string]$Ninja = '',
    [switch]$Headless,
    [switch]$Fresh,
    [string]$BuildDirectory = '',
    [ValidateRange(1, 7200)][int]$ConfigureTimeoutSeconds = 120,
    [ValidateRange(1, 7200)][int]$BuildTimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'native_process.ps1')
if (-not $IsWindows) { throw 'This entry point requires Windows; use CMake presets on other platforms' }

if ($VsInstallPath -or -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    if ($VsInstallPath) {
        $vsRoot = (Resolve-Path $VsInstallPath).Path
    } else {
        $vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path $vswhere -PathType Leaf)) { throw 'vswhere.exe not found; pass -VsInstallPath or run from a Visual Studio developer shell' }
        $vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vsRoot) { throw 'MSVC x64 tools not found; pass -VsInstallPath to select an installation' }
    }

    $devShellModule = Join-Path $vsRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path $devShellModule -PathType Leaf)) { throw "Visual Studio developer shell module not found under: $vsRoot" }
    Import-Module $devShellModule
    Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
}

$resolvedQtRoot = ''
if ($QtRoot) {
    $resolvedQtRoot = (Resolve-Path $QtRoot).Path
    $env:Path = "$(Join-Path $resolvedQtRoot 'bin');$env:Path"
}

$resolvedNinja = ''
if ($Ninja) {
    $resolvedNinja = (Resolve-Path $Ninja).Path
}

$preset = if ($Headless) { 'headless' } else { 'dev' }
$resolvedBuild = if ($BuildDirectory) { [IO.Path]::GetFullPath($BuildDirectory, $repoRoot) } else { Join-Path $repoRoot "build/$preset" }
$logRoot = Join-Path $resolvedBuild ('logs/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '_' + [guid]::NewGuid().ToString('N'))
$resolvedCMake = (Get-Command $CMake -CommandType Application -ErrorAction Stop).Source
$ctest = Join-Path (Split-Path $resolvedCMake) 'ctest.exe'
Write-Output "Build evidence: $logRoot"
Push-Location $repoRoot
try {
    $configureArguments = @('--preset', $preset)
    if ($resolvedQtRoot) { $configureArguments += "-DCMAKE_PREFIX_PATH=$resolvedQtRoot" }
    if ($resolvedNinja) { $configureArguments += "-DCMAKE_MAKE_PROGRAM=$resolvedNinja" }
    if ($BuildDirectory) { $configureArguments += @('-B', $BuildDirectory) }
    if ($Fresh) { $configureArguments += '--fresh' }
    Invoke-NativeStep -Executable $resolvedCMake -Arguments $configureArguments -WorkingDirectory $repoRoot -LogPath (Join-Path $logRoot 'configure.log') -TimeoutSeconds $ConfigureTimeoutSeconds
    Invoke-NativeStep -Executable $resolvedCMake -Arguments @('--build', $resolvedBuild, '--parallel', '4') -WorkingDirectory $repoRoot -LogPath (Join-Path $logRoot 'build.log') -TimeoutSeconds $BuildTimeoutSeconds
    if (-not $Headless) {
        Invoke-NativeStep -Executable $resolvedCMake -Arguments @('--build', $resolvedBuild, '--target', 'quickclash_qmllint') -WorkingDirectory $repoRoot -LogPath (Join-Path $logRoot 'qmllint.log') -TimeoutSeconds $BuildTimeoutSeconds
    }
    Invoke-NativeStep -Executable $ctest -Arguments @('--test-dir', $resolvedBuild, '--output-on-failure', '--timeout', '30', '--output-junit', (Join-Path $logRoot 'tests.xml')) -WorkingDirectory $repoRoot -LogPath (Join-Path $logRoot 'ctest.log') -TimeoutSeconds 120
} finally { Pop-Location }
