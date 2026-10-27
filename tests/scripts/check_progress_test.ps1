#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskRoot = Join-Path $taskRepo ('build/script_tests/check_progress/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path (Join-Path $taskRoot 'reports') -Force
$null = New-Item -ItemType Directory -Path (Join-Path $taskRoot 'templates') -Force
$taskChecker = Join-Path $taskRepo 'scripts/check_progress.ps1'
[IO.File]::WriteAllText((Join-Path $taskRoot 'reports/acceptance.md'), '# Recorded acceptance')
[IO.File]::WriteAllText((Join-Path $taskRoot 'templates/task-report.md'), '# Unfilled template')
[IO.File]::WriteAllText((Join-Path $taskRoot 'DEVLOG.md'), "# Development log`n`n## Routine fix`n`nBuild fixture v1; validation passed; product tests not run.")
$taskPackages = (0..20 | ForEach-Object { 'W{0:D2}' -f $_ }) -join ', '
$taskRows = @(0..5 | ForEach-Object {
    $taskPhase = 'P' + $_
    $taskWork = if ($_ -eq 0) { $taskPackages } else { 'W01' }
    "| $taskPhase-01 | $taskPhase | $taskWork | Fixture delivery | Done | Tester | — | EVIDENCE | — |"
}) -join "`n"
$taskChecklist = @(@(0..5 | ForEach-Object {
    $taskGate = 'G' + $_
    $taskPhase = 'P' + $_
    $taskState = if ($_ -eq 0) { 'Pass' } else { 'NotRun' }
    $taskEvidence = if ($_ -eq 0) { '[Acceptance](reports/acceptance.md)' } else { '—' }
    "| $taskGate | $taskPhase | $taskState | $taskEvidence | Continue |"
    foreach ($taskLetter in 'A','B','C','D','E') {
        if ($_ -eq 0) { "- [x] $taskGate-${taskLetter}：Verified [Acceptance](reports/acceptance.md)" }
        else { "- [ ] $taskGate-${taskLetter}：Pending" }
    }
}); @(1..16 | ForEach-Object {
    '| MVP-{0:D2} | Fixture condition | P0-01 | NotRun | — | Pending |' -f $_
})) -join "`n"
function Test-Case([string]$Name, [string]$Evidence, [bool]$Expected, [string]$Checklist = $taskChecklist, [string]$Rows = $taskRows) {
    [IO.File]::WriteAllText((Join-Path $taskRoot 'TASKS.md'), $Rows.Replace('EVIDENCE', $Evidence))
    [IO.File]::WriteAllText((Join-Path $taskRoot 'CHECKLISTS.md'), $Checklist)
    $taskPassed = $true
    try { $null = & $taskChecker -ProgressRoot $taskRoot } catch { $taskPassed = $false }
    if ($taskPassed -ne $Expected) { throw "Unexpected checker result: $Name (expected $Expected)" }
    Write-Output "PASS: $Name"
}
Test-Case 'local report accepted' '[Report](reports/acceptance.md)' $true
Test-Case 'anchored routine DEVLOG accepted' '[Fix](DEVLOG.md#routine-fix)' $true
Test-Case 'specific Actions run accepted' '[CI](https://github.com/fixture/project/actions/runs/12345)' $true
Test-Case 'missing evidence rejected' '—' $false
Test-Case 'missing report rejected' '[Report](reports/missing.md)' $false
Test-Case 'DEVLOG without anchor rejected' '[Log](DEVLOG.md)' $false
Test-Case 'unknown DEVLOG anchor rejected' '[Log](DEVLOG.md#missing)' $false
Test-Case 'repository homepage rejected' '[CI](https://github.com/fixture/project)' $false
Test-Case 'template rejected' '[Template](templates/task-report.md)' $false
Test-Case 'gate still requires a summary report' '[Log](DEVLOG.md#routine-fix)' $false ($taskChecklist.Replace('[Acceptance](reports/acceptance.md)', '[Log](DEVLOG.md#routine-fix)'))
Test-Case 'gate cannot pass with unfinished task' '[Report](reports/acceptance.md)' $false $taskChecklist ($taskRows.Replace('| Done |', '| Review |'))
[IO.File]::AppendAllText((Join-Path $taskRoot 'DEVLOG.md'), "`n![Missing](reports/missing.png)")
Test-Case 'broken image rejected' '[Report](reports/acceptance.md)' $false
