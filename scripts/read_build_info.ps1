#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Path,
    [string]$ExpectedCommit = ''
)
$ErrorActionPreference = 'Stop'
$taskInfo = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
if ($taskInfo.schema_version -ne 1 -or
    $taskInfo.base_version -isnot [string] -or $taskInfo.base_version -cnotmatch '^\d+\.\d+\.\d+$' -or
    $taskInfo.application_version -isnot [string] -or $taskInfo.commit -isnot [string] -or
    $taskInfo.source_state -cnotin @('clean', 'dirty', 'unknown')) {
    throw 'Missing or invalid compiled build identity; rebuild before packaging.'
}
if ($taskInfo.source_state -eq 'unknown') {
    if ($taskInfo.commit) { throw 'Unknown build identity must not claim a source commit.' }
} else {
    if ($taskInfo.commit -cnotmatch '^[0-9a-f]{40}$') { throw 'Build identity requires a full source commit.' }
}
if ($taskInfo.application_version -cne $taskInfo.base_version) { throw 'Application version differs from its base version.' }
if ($ExpectedCommit -and ($ExpectedCommit -cnotmatch '^[0-9a-fA-F]{40}$' -or
    $taskInfo.commit -cne $ExpectedCommit.ToLowerInvariant())) {
    throw 'Package source commit differs from the compiled build identity.'
}
if ($taskInfo.ci_run_id -isnot [string] -or $taskInfo.ci_run_attempt -isnot [string] -or
    (($taskInfo.ci_run_id -or $taskInfo.ci_run_attempt) -and
        ($taskInfo.ci_run_id -cnotmatch '^[1-9][0-9]*$' -or $taskInfo.ci_run_attempt -cnotmatch '^[1-9][0-9]*$'))) {
    throw 'Build identity contains an invalid CI run ID or attempt.'
}
$taskInfo
