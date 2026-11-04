#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskSelector = Join-Path $taskRepo 'scripts/select_ci_scope.ps1'
$taskRoot = Join-Path $taskRepo ('build/script_tests/select_ci_scope/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $taskRoot -Force
$taskGitMock = [pscustomobject]@{
    Changed = @('src/application/desktop_runtime.cpp')
    MissingBase = $false
    DiffFailure = $false
    Calls = [Collections.Generic.List[object]]::new()
}
$taskPassed = 0
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
# Mock only Git I/O; run the same selector used by Actions and inspect its output.
function git {
    $taskGitMock.Calls.Add(@($args))
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'cat-file' -and $taskGitMock.MissingBase) { $global:LASTEXITCODE = 1 }
    if ($args[0] -eq 'diff') {
        if ($taskGitMock.DiffFailure) { $global:LASTEXITCODE = 1 }
        $taskGitMock.Changed
    }
}
function Test-Scope([string]$Name, [hashtable]$Options, [bool[]]$Expected) {
    $taskGitMock.Calls.Clear()
    $arguments = @{
        EventName = 'push'; Ref = 'refs/heads/main'; DefaultBranch = 'main'
        BeforeSha = '1' * 40; BaseSha = '2' * 40; HeadSha = '3' * 40
    }
    foreach ($key in $Options.Keys) { $arguments[$key] = $Options[$key] }
    $actual = & $taskSelector @arguments
    Require ($actual.product -eq $Expected[0] -and $actual.full -eq $Expected[1] -and
        $actual.publish -eq $Expected[2]) "Unexpected product/full/publish scope: $Name ($actual)"
    Require ($LASTEXITCODE -eq 0) "Stale native failure escaped: $Name"
    $script:taskPassed++
    Write-Output "PASS: $Name"
}
Test-Scope 'main product changes use fast validation and publish' @{} @($true, $false, $true)
Require ($taskGitMock.Calls[1].Count -eq 5 -and $taskGitMock.Calls[1][2] -eq '--no-renames') 'Push comparison must use two commit arguments.'
Test-Scope 'PR product changes validate without publishing' @{ EventName = 'pull_request'; Ref = 'refs/pull/7/merge' } @($true, $false, $false)
Require ($taskGitMock.Calls[0][3] -eq (('2' * 40) + '...' + ('3' * 40))) 'PR comparison must use the merge base.'
$taskGitMock.Changed = @('docs/progress/TASKS.md', 'README.md')
Test-Scope 'main documentation skips product and release' @{} @($false, $false, $false)
Test-Scope 'PR documentation skips product' @{ EventName = 'pull_request'; Ref = 'refs/pull/7/merge' } @($false, $false, $false)
$taskGitMock.Changed = @()
Test-Scope 'empty diff skips product' @{} @($false, $false, $false)
foreach ($path in @('docs/baseline/config_policy.json', 'docs/baseline/release_matrix.json', 'docs/baseline/licenses.md',
        '.github/workflows/build.yml', 'LICENSE', 'unknown_file')) {
    $taskGitMock.Changed = @($path)
    Test-Scope "product-affecting path: $path" @{} @($true, $false, $true)
}
$taskGitMock.Changed = @('src/old_name.cpp', 'docs/moved_source.md')
Test-Scope 'product renamed under docs is not skipped' @{} @($true, $false, $true)
Test-Scope 'non-default branch never publishes' @{ Ref = 'refs/heads/feature' } @($true, $false, $false)
Test-Scope 'configured default branch is respected' @{ Ref = 'refs/heads/trunk'; DefaultBranch = 'trunk' } @($true, $false, $true)
$taskGitMock.Changed = @('README.md')
Test-Scope 'manual main run always builds and publishes' @{ EventName = 'workflow_dispatch' } @($true, $false, $true)
Require ($taskGitMock.Calls.Count -eq 0) 'Manual run unexpectedly inspected a diff.'
Test-Scope 'manual feature run only validates' @{ EventName = 'workflow_dispatch'; Ref = 'refs/heads/feature' } @($true, $false, $false)
Test-Scope 'manual full validation includes documentation' @{ EventName = 'workflow_dispatch'; FullValidation = $true } @($true, $true, $true)
Test-Scope 'version tag always validates fully and publishes' @{ Ref = 'refs/tags/v1.2.3' } @($true, $true, $true)
Require ($taskGitMock.Calls.Count -eq 0) 'Version tag unexpectedly inspected a push diff.'
Test-Scope 'manual version tag also validates fully' @{ EventName = 'workflow_dispatch'; Ref = 'refs/tags/v1.2.3-rc.1' } @($true, $true, $true)
Test-Scope 'new branch falls back to product build' @{ BeforeSha = '0' * 40 } @($true, $false, $true)
$taskGitMock.MissingBase = $true
Test-Scope 'unavailable push base falls back without failure' @{} @($true, $false, $true)
$taskGitMock.MissingBase = $false
$taskGitMock.DiffFailure = $true
$taskFailed = $false
try { & $taskSelector -EventName push -Ref refs/heads/main -DefaultBranch main -BeforeSha ('1' * 40) -HeadSha ('3' * 40) } catch { $taskFailed = $true }
Require $taskFailed 'Failed Git diff was treated as documentation-only.'
$taskPassed++
$taskFailed = $false
try { & $taskSelector -EventName pull_request -Ref refs/pull/7/merge -DefaultBranch main -BaseSha ('2' * 40) -HeadSha ('3' * 40) } catch { $taskFailed = $true }
Require $taskFailed 'Failed PR diff was treated as documentation-only.'
$taskPassed++
$taskGitMock.DiffFailure = $false
$taskFailed = $false
try { & $taskSelector -EventName pull_request -Ref refs/pull/7/merge -DefaultBranch main -BaseSha '' -HeadSha ('3' * 40) } catch { $taskFailed = $true }
Require $taskFailed 'Missing PR base SHA was accepted.'
$taskPassed++
$taskOutput = Join-Path $taskRoot 'github_output.txt'
Test-Scope 'GitHub output contract' @{ EventName = 'workflow_dispatch'; OutputPath = $taskOutput } @($true, $false, $true)
Require ((Get-Content -LiteralPath $taskOutput) -join ',' -ceq 'product=true,full=false,publish=true') 'GitHub outputs differ.'
Write-Output "PASS: $taskPassed CI scope cases; evidence: $taskRoot"
$global:LASTEXITCODE = 0
