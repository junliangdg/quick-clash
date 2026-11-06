#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskSelector = Join-Path $taskRepo 'scripts/select_windows_tests.ps1'
$taskRoot = Join-Path $taskRepo ('build/script_tests/select_windows_tests/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $taskRoot -Force
$taskManifest = Join-Path $taskRoot 'quickclash_tests.json'
@{
    product_targets = @('quickclash', 'quickclash_service')
    smoke_tests = @('contracts', 'qml_smoke')
    test_targets = @{
        contracts = 'contracts_test'; qml_smoke = 'quickclash'; ui_state = 'ui_state_test'
        runtime = 'runtime_test'; runtime_proxy_latency = 'runtime_test'; 'literal.test' = 'literal_test'
    }
    slow_tests = @('runtime', 'runtime_proxy_latency')
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskManifest -Encoding utf8
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

$plan = & $taskSelector -ManifestPath $taskManifest
Require ($plan.scope -eq 'smoke' -and $plan.tests.Count -eq 2 -and $plan.run_tests -and -not $plan.lint) 'Default must run only the smoke scope, without full lint.'
Require ($plan.targets.Count -eq 3 -and 'runtime_test' -notin $plan.targets -and 'ui_state_test' -notin $plan.targets) 'Default must not build unrelated test executables.'
Require ('contracts' -match $plan.test_regex -and 'qml_smoke' -match $plan.test_regex -and 'contracts_extra' -notmatch $plan.test_regex) 'Smoke filter must match exact names.'
Write-Output 'PASS: default scope builds product and smoke dependencies only'

$plan = & $taskSelector -ManifestPath $taskManifest -Test runtime_proxy_latency,runtime_proxy_latency
Require ($plan.scope -eq 'targeted' -and $plan.tests.Count -eq 1 -and 'runtime_test' -in $plan.targets -and -not $plan.exclude_label) 'An explicitly selected slow test must run and build its shared executable.'
Require ('runtime_proxy_latency' -match $plan.test_regex -and 'runtime' -notmatch $plan.test_regex) 'Selecting one shared-executable test must not select its peers.'
$plan = & $taskSelector -ManifestPath $taskManifest -Test literal.test -Lint
Require ($plan.lint -and 'literal.test' -match $plan.test_regex -and 'literalXtest' -notmatch $plan.test_regex) 'Literal names must be escaped and explicit lint preserved.'
Write-Output 'PASS: targeted selection includes slow tests, deduplicates and uses exact names'

$plan = & $taskSelector -ManifestPath $taskManifest -Fast
Require ($plan.scope -eq 'fast' -and $plan.targets[0] -eq 'quickclash_ci_fast' -and $plan.exclude_label -eq 'ci_slow' -and $plan.lint -and $plan.tests.Count -eq 4) 'CI fast scope changed.'
$plan = & $taskSelector -ManifestPath $taskManifest -Full
Require ($plan.scope -eq 'full' -and $plan.targets.Count -eq 0 -and $plan.tests.Count -eq 6 -and -not $plan.test_regex -and -not $plan.exclude_label -and $plan.lint) 'Explicit full scope must retain every target and test.'
$plan = & $taskSelector -ManifestPath $taskManifest -BuildOnly
Require ($plan.scope -eq 'build_only' -and -not $plan.run_tests -and $plan.tests.Count -eq 0 -and $plan.targets.Count -eq 2) 'Build-only must not imply test coverage.'
Write-Output 'PASS: fast/full scopes retained and build-only reports tests NotRun'

foreach ($options in @(@{Test = @('unknown')}, @{Fast = $true; Full = $true},
        @{Test = @('contracts'); Fast = $true}, @{Test = @('contracts'); Full = $true},
        @{Test = @('contracts'); BuildOnly = $true}, @{Full = $true; BuildOnly = $true},
        @{Fast = $true; BuildOnly = $true})) {
    $rejected = $false
    try { & $taskSelector -ManifestPath $taskManifest @options | Out-Null } catch { $rejected = $true }
    Require $rejected "Unknown test or conflicting scope was accepted: $($options | ConvertTo-Json -Compress)"
}
Write-Output 'PASS: unknown tests and conflicting scope flags are rejected'

@{
    product_targets = @('quickclash_config', 'quickclash_storage')
    smoke_tests = @('contracts')
    test_targets = @{contracts = 'contracts_test'; storage = 'storage_test'}
    slow_tests = @()
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskManifest -Encoding utf8
$plan = & $taskSelector -ManifestPath $taskManifest
Require ($plan.tests.Count -eq 1 -and $plan.tests[0] -eq 'contracts' -and 'quickclash' -notin $plan.targets) 'Headless selection must not require the UI.'
$plan = & $taskSelector -ManifestPath $taskManifest -Full:$false -Fast:$false
Require ($plan.scope -eq 'smoke') 'False switch values must not opt into a broad scope.'
Write-Output "PASS: headless scope and explicit false switches; evidence: $taskRoot"
$global:LASTEXITCODE = 0
