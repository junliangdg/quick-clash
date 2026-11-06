#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ManifestPath,
    [string[]]$Test = @(),
    [switch]$Fast,
    [switch]$Full,
    [switch]$BuildOnly,
    [switch]$Lint
)
$ErrorActionPreference = 'Stop'
if (([int]$Fast.IsPresent + [int]$Full.IsPresent + [int]$BuildOnly.IsPresent) -gt 1 -or
    ($Test.Count -gt 0 -and ($Fast -or $Full -or $BuildOnly))) {
    throw 'Choose one scope: default smoke, -Test, -BuildOnly, -Fast or -Full.'
}
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json -AsHashtable
$selected = @()
$targets = @()
$regex = ''
$scope = 'smoke'
$exclude = ''
if ($Full) {
    $scope = 'full'
    $selected = @($manifest.test_targets.Keys | Sort-Object)
} elseif ($Fast) {
    $scope = 'fast'
    $targets = @('quickclash_ci_fast')
    $selected = @($manifest.test_targets.Keys | Where-Object { $_ -notin $manifest.slow_tests } | Sort-Object)
    $exclude = 'ci_slow'
} else {
    $targets = @($manifest.product_targets)
    if ($BuildOnly) {
        $scope = 'build_only'
    } else {
        $selected = if ($Test.Count -gt 0) { @($Test | Sort-Object -Unique) } else { @($manifest.smoke_tests) }
        if ($Test.Count -gt 0) { $scope = 'targeted' }
        foreach ($name in $selected) {
            if (-not $manifest.test_targets.ContainsKey($name)) {
                throw "Unknown test '$name'. Available tests: $($manifest.test_targets.Keys -join ', ')"
            }
            $targets += $manifest.test_targets[$name]
        }
        $regex = '^(' + (($selected | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
        $targets = @($targets | Sort-Object -Unique)
    }
}
if (-not $BuildOnly -and $selected.Count -eq 0) { throw 'Validation scope contains no tests.' }
[pscustomobject][ordered]@{
    scope = $scope
    targets = @($targets)
    tests = @($selected)
    test_regex = $regex
    exclude_label = $exclude
    run_tests = -not $BuildOnly.IsPresent
    lint = $Lint.IsPresent -or $Fast.IsPresent -or $Full.IsPresent
}
