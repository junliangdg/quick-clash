#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$QtRoot = '',
    [string]$VsInstallPath = '',
    [string]$CMake = 'cmake',
    [string]$Ninja = '',
    [switch]$Headless,
    [switch]$Release,
    [switch]$Fast,
    [switch]$Full,
    [string[]]$Test = @(),
    [switch]$BuildOnly,
    [switch]$Lint,
    [switch]$Fresh,
    [string]$BuildDirectory = '',
    [ValidateRange(1, 7200)][int]$ConfigureTimeoutSeconds = 120,
    [ValidateRange(1, 7200)][int]$BuildTimeoutSeconds = 600,
    [ValidateRange(1, 7200)][int]$TestTimeoutSeconds = 600
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'native_process.ps1')
if (-not $IsWindows) { throw 'This entry point requires Windows; use CMake presets on other platforms' }
if (([int]$Fast.IsPresent + [int]$Full.IsPresent + [int]$BuildOnly.IsPresent + [int]$Release.IsPresent) -gt 1 -or
    ($Test.Count -gt 0 -and ($Fast -or $Full -or $BuildOnly -or $Release))) {
    throw 'Choose one scope: default smoke, -Test, -BuildOnly, -Fast, -Full or -Release.'
}
if ($Release -and ($Headless -or $Lint)) { throw '-Release cannot be combined with -Headless or -Lint; use a development build for validation.' }

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

$preset = if ($Release) { 'release' } elseif ($Headless) { 'headless' } else { 'dev' }
$resolvedBuild = if ($BuildDirectory) { [IO.Path]::GetFullPath($BuildDirectory, $repoRoot) } else { Join-Path $repoRoot "build/$preset" }
$logRoot = Join-Path $resolvedBuild ('logs/' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '_' + [guid]::NewGuid().ToString('N'))
$resolvedCMake = (Get-Command $CMake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
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
    $plan = if ($Release) {
        [pscustomobject][ordered]@{
            scope = 'release'; targets = @(); tests = @(); test_regex = ''; exclude_label = ''
            run_tests = $false; lint = $false
        }
    } else {
        & (Join-Path $PSScriptRoot 'select_windows_tests.ps1') `
            -ManifestPath (Join-Path $resolvedBuild 'quickclash_tests.json') `
            -Test $Test -Fast:$Fast -Full:$Full -BuildOnly:$BuildOnly -Lint:$Lint
    }
    $plan | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $logRoot 'scope.json') -Encoding utf8
    Write-Output "Validation scope: $($plan.scope); tests: $($plan.tests -join ', ')"
    $buildArguments = @('--build', $resolvedBuild, '--parallel', '4')
    if ($plan.targets.Count -gt 0) { $buildArguments += @('--target') + $plan.targets }
    Invoke-NativeStep -Executable $resolvedCMake -Arguments $buildArguments -WorkingDirectory $repoRoot -LogPath (Join-Path $logRoot 'build.log') -TimeoutSeconds $BuildTimeoutSeconds
    if (-not $Headless -and $plan.lint) {
        Invoke-NativeStep -Executable $resolvedCMake -Arguments @('--build', $resolvedBuild, '--target', 'quickclash_qmllint') -WorkingDirectory $repoRoot -LogPath (Join-Path $logRoot 'qmllint.log') -TimeoutSeconds $BuildTimeoutSeconds
    }
    if ($plan.run_tests) {
        $testArguments = @('--test-dir', $resolvedBuild, '--output-on-failure', '--no-tests=error', '--timeout', '30', '--output-junit', (Join-Path $logRoot 'tests.xml'))
        if ($plan.test_regex) { $testArguments += @('--tests-regex', $plan.test_regex) }
        if ($plan.exclude_label) { $testArguments += @('--label-exclude', $plan.exclude_label) }
        Invoke-NativeStep -Executable $ctest -Arguments $testArguments -WorkingDirectory $repoRoot -LogPath (Join-Path $logRoot 'ctest.log') -TimeoutSeconds $TestTimeoutSeconds
    } elseif ($Release) {
        Write-Output 'Production Release build; BUILD_TESTING=OFF; tests NotRun. Use package_release_windows.ps1 for package and startup checks.'
    } else {
        Write-Output 'Build only; tests NotRun. Complete the targeted manual check before claiming behavior verified.'
    }
} finally { Pop-Location }
