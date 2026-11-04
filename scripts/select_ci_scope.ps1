#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('push', 'pull_request', 'workflow_dispatch')][string]$EventName,
    [Parameter(Mandatory)][string]$Ref,
    [Parameter(Mandatory)][string]$DefaultBranch,
    [string]$BeforeSha = '',
    [string]$BaseSha = '',
    [Parameter(Mandatory)][string]$HeadSha,
    [switch]$FullValidation,
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
$versionTag = $EventName -ne 'pull_request' -and $Ref.StartsWith('refs/tags/v', [StringComparison]::Ordinal)
$full = $versionTag -or $FullValidation.IsPresent
$product = $true
$changed = $null

# Tags and explicit runs always build. For ordinary pushes/PRs only a known
# documentation-only diff may skip the product; an unavailable base fails safe.
# Disable rename folding so moving a product file under docs still reports its deletion.
if (-not $versionTag -and $EventName -eq 'pull_request') {
    if ($BaseSha -cnotmatch '^[0-9a-f]{40}$' -or $HeadSha -cnotmatch '^[0-9a-f]{40}$') {
        throw 'Pull request comparison requires full base and head commit SHAs.'
    }
    $changed = @(git diff --name-only --no-renames "$BaseSha...$HeadSha")
    if ($LASTEXITCODE -ne 0) { throw 'Cannot classify pull request changes.' }
} elseif (-not $versionTag -and $EventName -eq 'push' -and
    $BeforeSha -cmatch '^[0-9a-f]{40}$' -and $BeforeSha -ne ('0' * 40)) {
    git cat-file -e "$BeforeSha^{commit}" 2>$null
    if ($LASTEXITCODE -eq 0) {
        $changed = @(git diff --name-only --no-renames $BeforeSha $HeadSha)
        if ($LASTEXITCODE -ne 0) { throw 'Cannot classify push changes.' }
    }
}
if ($null -ne $changed) {
    $product = @($changed | Where-Object {
        $_ -notmatch '^(docs/|[^/]+[.]md$)' -or
        $_ -match '^docs/baseline/((config_policy|release_matrix)[.]json|licenses[.]md)$'
    }).Count -gt 0
}
if ($full) { $product = $true }
$scope = [pscustomobject][ordered]@{
    product = $product
    full = $full
    publish = $product -and $EventName -ne 'pull_request' -and
        ($versionTag -or $Ref -ceq "refs/heads/$DefaultBranch")
}
if ($OutputPath) {
    $scope.PSObject.Properties | ForEach-Object {
        "$($_.Name)=$($_.Value.ToString().ToLowerInvariant())"
    } | Add-Content -LiteralPath $OutputPath -Encoding utf8NoBOM
}
# An absent push base deliberately falls back to a product build, not a failed
# PowerShell step caused by git cat-file's otherwise stale native exit code.
$global:LASTEXITCODE = 0
$scope
