#Requires -Version 7.2
[CmdletBinding()]
param([string]$ConfiguredManifest = '')
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
        ui_subscriptions = 'quickclash'; ui_subscription_layout = 'quickclash'
        ui_settings = 'quickclash'; ui_settings_layout = 'quickclash'
    }
    slow_tests = @('runtime', 'runtime_proxy_latency', 'ui_subscription_layout', 'ui_settings_layout')
    full_replacements = @{ui_settings = 'ui_settings_layout'}
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskManifest -Encoding utf8
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }

if ($ConfiguredManifest) {
    $fast = & $taskSelector -ManifestPath $ConfiguredManifest -Fast
    $full = & $taskSelector -ManifestPath $ConfiguredManifest -Full
    $manifest = Get-Content -LiteralPath $ConfiguredManifest -Raw | ConvertFrom-Json -AsHashtable
    $hasUi = 'quickclash' -in $manifest.product_targets
    if ($hasUi) {
        foreach ($name in @('qml_smoke', 'ui_state', 'ui_keyboard', 'ui_accessibility', 'ui_accessibility_zh',
                'ui_subscriptions', 'ui_subscriptions_zh', 'ui_settings', 'ui_runtime_lists',
                'ui_custom_groups', 'host_lifecycle')) {
            Require ($name -in $fast.tests) "Critical daily check is missing: $name"
        }
        foreach ($name in @('ui_prototype', 'ui_visual', 'ui_subscription_layout', 'ui_subscription_layout_zh',
                'ui_accessibility_layout', 'ui_accessibility_layout_zh', 'ui_settings_layout',
                'ui_runtime_lists_layout', 'ui_custom_groups_layout', 'ui_settings_zh',
                'ui_runtime_lists_zh', 'ui_custom_groups_zh')) {
            Require ($name -notin $fast.tests -and $name -in $full.tests) "Full-only check is misclassified: $name"
            $targeted = & $taskSelector -ManifestPath $ConfiguredManifest -Test $name
            Require ($targeted.tests.Count -eq 1 -and $targeted.tests[0] -eq $name -and
                'quickclash' -in $targeted.targets -and -not $targeted.exclude_label) "Targeted UI check cannot run: $name"
        }
        foreach ($short in @('connection_rule', 'ui_accessibility', 'ui_accessibility_zh',
                'ui_settings', 'ui_runtime_lists', 'ui_custom_groups')) {
            $replacement = $manifest.full_replacements[$short]
            $targeted = & $taskSelector -ManifestPath $ConfiguredManifest -Test $short
            $expectedInFast = $short -ne 'connection_rule'
            Require ($replacement -and (($short -in $fast.tests) -eq $expectedInFast) -and $short -notin $full.tests -and
                $replacement -in $full.tests -and $targeted.tests.Count -eq 1 -and
                $targeted.tests[0] -eq $short -and -not $targeted.exclude_label) "Full replacement is misclassified: $short"
        }
        Require ($full.exclude_label -eq 'full_redundant') 'Full must exclude subsumed entries from CTest.'
    } else {
        Require ($manifest.full_replacements.Count -eq 0 -and
            @($full.tests | Where-Object { $_ -match '^(ui_|qml_|host_)' }).Count -eq 0) 'Headless manifest contains UI tests or replacements.'
    }
    $expectedTargets = @(@($manifest.product_targets) + @($fast.tests | ForEach-Object {
        $manifest.test_targets[$_]
    }) | Sort-Object -Unique)
    Require ($manifest.fast_targets.Count -gt 0 -and
        -not (Compare-Object -ReferenceObject $expectedTargets -DifferenceObject @($manifest.fast_targets))) 'Fast build dependencies differ from the selected test executables and product.'
    $configuredBuild = Split-Path -Parent (Resolve-Path -LiteralPath $ConfiguredManifest).Path
    function Get-CTestNames([string[]]$Arguments) {
        $result = & ctest @Arguments --show-only=json-v1
        if ($LASTEXITCODE -ne 0) { throw "CTest could not enumerate: $($Arguments -join ' ')" }
        $listing = $result | ConvertFrom-Json -AsHashtable
        return @($listing.tests | ForEach-Object { $_.name })
    }
    Push-Location $taskRepo
    try {
        $actualFast = @(Get-CTestNames -Arguments @('--test-dir', $configuredBuild, '--label-exclude', 'ci_slow'))
        $actualFull = @(Get-CTestNames -Arguments @('--test-dir', $configuredBuild, '--label-exclude', 'full_redundant'))
        $preset = if ($hasUi) { 'dev' } else { 'headless' }
        $presetFull = @(Get-CTestNames -Arguments @('--preset', $preset, '--test-dir', $configuredBuild))
        Require (-not (Compare-Object -ReferenceObject @($fast.tests) -DifferenceObject $actualFast)) 'Fast manifest differs from the CTest label selection.'
        Require (-not (Compare-Object -ReferenceObject @($full.tests) -DifferenceObject $actualFull)) 'Full manifest differs from the CTest label selection.'
        Require (-not (Compare-Object -ReferenceObject @($full.tests) -DifferenceObject $presetFull)) 'Test preset differs from the full manifest.'
    } finally {
        Pop-Location
    }
    Write-Output 'PASS: configured manifest, fast build dependencies, CTest labels and preset agree on daily/full selections'
}

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
Require ($plan.scope -eq 'fast' -and $plan.targets[0] -eq 'quickclash_ci_fast' -and $plan.exclude_label -eq 'ci_slow' -and $plan.lint -and $plan.tests.Count -eq 6 -and 'ui_subscriptions' -in $plan.tests -and 'ui_subscription_layout' -notin $plan.tests -and 'ui_settings' -in $plan.tests) 'CI fast scope changed.'
$plan = & $taskSelector -ManifestPath $taskManifest -Full
Require ($plan.scope -eq 'full' -and $plan.targets.Count -eq 0 -and $plan.tests.Count -eq 9 -and 'ui_subscription_layout' -in $plan.tests -and 'ui_settings_layout' -in $plan.tests -and 'ui_settings' -notin $plan.tests -and -not $plan.test_regex -and $plan.exclude_label -eq 'full_redundant' -and $plan.lint) 'Explicit full scope must run replacements instead of duplicate behavior tests.'
$plan = & $taskSelector -ManifestPath $taskManifest -Test ui_subscription_layout
Require ($plan.tests.Count -eq 1 -and $plan.tests[0] -eq 'ui_subscription_layout' -and 'quickclash' -in $plan.targets -and -not $plan.exclude_label -and 'ui_subscriptions' -notmatch $plan.test_regex) 'A targeted layout test must run without selecting the behavior test.'
$plan = & $taskSelector -ManifestPath $taskManifest -BuildOnly
Require ($plan.scope -eq 'build_only' -and -not $plan.run_tests -and $plan.tests.Count -eq 0 -and $plan.targets.Count -eq 2) 'Build-only must not imply test coverage.'
Write-Output 'PASS: fast/full scopes retained and build-only reports tests NotRun'

$manifest = Get-Content -LiteralPath $taskManifest -Raw | ConvertFrom-Json -AsHashtable
$manifest.full_replacements.ui_settings = 'missing_test'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskManifest -Encoding utf8
$rejected = $false
try { & $taskSelector -ManifestPath $taskManifest -Full | Out-Null } catch { $rejected = $true }
Require $rejected 'Full scope must reject a missing replacement.'
$manifest.full_replacements.ui_settings = 'ui_settings_layout'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskManifest -Encoding utf8

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
    full_replacements = @{}
} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $taskManifest -Encoding utf8
$plan = & $taskSelector -ManifestPath $taskManifest
Require ($plan.tests.Count -eq 1 -and $plan.tests[0] -eq 'contracts' -and 'quickclash' -notin $plan.targets) 'Headless selection must not require the UI.'
$plan = & $taskSelector -ManifestPath $taskManifest -Full:$false -Fast:$false
Require ($plan.scope -eq 'smoke') 'False switch values must not opt into a broad scope.'
Write-Output "PASS: headless scope and explicit false switches; evidence: $taskRoot"
$global:LASTEXITCODE = 0
