[CmdletBinding()]
param(
    [string]$QtRoot = '',
    [string]$VsInstallPath = '',
    [string]$CMake = 'cmake',
    [string]$Ninja = '',
    [switch]$Headless,
    [switch]$Fresh,
    [string]$BuildDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

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
Push-Location $repoRoot
try {
    $configureArguments = @('--preset', $preset)
    if ($resolvedQtRoot) { $configureArguments += "-DCMAKE_PREFIX_PATH=$resolvedQtRoot" }
    if ($resolvedNinja) { $configureArguments += "-DCMAKE_MAKE_PROGRAM=$resolvedNinja" }
    if ($BuildDirectory) { $configureArguments += @('-B', $BuildDirectory) }
    if ($Fresh) { $configureArguments += '--fresh' }
    & $CMake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    $resolvedBuild = if ($BuildDirectory) { $BuildDirectory } else { "build/$preset" }
    & $CMake --build $resolvedBuild --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    $ctest = Join-Path (Split-Path (Get-Command $CMake).Source) 'ctest.exe'
    & $ctest --test-dir $resolvedBuild --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
} finally { Pop-Location }
