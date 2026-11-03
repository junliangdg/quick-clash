#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$root = Join-Path $repo ('build/script_tests/check_progress/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path (Join-Path $root 'reports') -Force
$checker = Join-Path $repo 'scripts/check_progress.ps1'
$utf8 = [System.Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText((Join-Path $root 'reports/evidence.md'), '# Historical evidence', $utf8)
[IO.File]::WriteAllText((Join-Path $root 'CHECKLISTS.md'), "# Release`n`n| G1 | P1 | Stale |`n| G2 | P2 | Pass |`n`n- [x] G2-A: Independent evidence", $utf8)
[IO.File]::WriteAllText((Join-Path $root 'history.md'), '| P1-01 | Review | Codex |', $utf8)

function Test-Case([string]$Name, [string]$Tasks, [bool]$Expected) {
    [IO.File]::WriteAllText((Join-Path $root 'TASKS.md'), $Tasks, $utf8)
    $passed = $true
    try { $null = & $checker -ProgressRoot $root } catch { $passed = $false }
    if ($passed -ne $Expected) { throw "Unexpected checker result: $Name (expected $Expected)" }
    Write-Output "PASS: $Name"
}

Test-Case 'lightweight task without owner, work package or evidence' '- Work — 状态：进行中；next step.' $true
Test-Case 'all current states accepted' '状态：待做； 状态：进行中； 状态：完成； 状态：阻塞' $true
Test-Case 'state qualifier and product-test status accepted' '状态：完成（Windows）；测试状态：Pass' $true
Test-Case 'prose without a labelled state accepted' 'An unlabelled note and next step.' $true
Test-Case 'unknown explicit state rejected' '- Work — 状态：Review；next step.' $false
Test-Case 'local and external links accepted' '[Evidence](reports/evidence.md#scope) [CI](https://example.org/run)' $true
Test-Case 'missing local link rejected' '[Missing](reports/missing.md)' $false
Test-Case 'missing image rejected' '![Missing](reports/missing.png)' $false

[IO.File]::WriteAllText((Join-Path $root 'history.md'), '[Missing](reports/missing.md)', $utf8)
Test-Case 'broken archived link rejected' '- Work — 状态：待做；next step.' $false
[IO.File]::WriteAllText((Join-Path $root 'history.md'), '| P1-01 | Review | Codex |', $utf8)

Remove-Item -LiteralPath (Join-Path $root 'CHECKLISTS.md')
Test-Case 'missing current checklist rejected' '- Work — 状态：待做；next step.' $false
