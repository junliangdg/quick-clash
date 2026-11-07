#Requires -Version 7.2
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$script = Join-Path $repo 'bench/attach_memory_windows.ps1'
$root = Join-Path $repo ('build/script_tests/attach_memory/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    & $script -ClientPid $PID -OutputDirectory (Join-Path $root 'self') -Seconds 1
    $summary = Get-Content -LiteralPath (Join-Path $root 'self/summary.json') -Raw | ConvertFrom-Json
    $rows = @(Import-Csv -LiteralPath (Join-Path $root 'self/memory.csv'))
    if ($summary.status -ne 'Sampled' -or $rows.Count -lt 2 -or
        @($rows | Where-Object {
            $_.pid -ne $PID -or $_.role -ne 'client' -or [int]$_.sample -le 0 -or
            [uint64]$_.private_working_set_bytes -le 0 -or
            [uint64]$_.working_set_bytes -lt [uint64]$_.private_working_set_bytes -or
            [uint64]$_.private_commit_bytes -le 0
        }).Count) { throw 'Self sampling counters failed.' }
    $rejected = $false
    try { & $script -ClientPid $PID -CorePid $PID -OutputDirectory (Join-Path $root 'duplicate') }
    catch { $rejected = $true }
    if (-not $rejected -or (Test-Path (Join-Path $root 'duplicate'))) { throw 'Duplicate PID accepted.' }
    $rejected = $false
    try { & $script -ClientPid ([int]::MaxValue) -OutputDirectory (Join-Path $root 'missing') }
    catch { $rejected = $true }
    if (-not $rejected -or (Test-Path (Join-Path $root 'missing'))) { throw 'Missing PID accepted.' }
    if (-not (Get-Process -Id $PID -ErrorAction Stop)) { throw 'Sampler controlled the target.' }
    Write-Output 'PASS: self counters/three metrics, duplicate rejection, missing rejection; target remains running (3 cases).'
} finally {
    Write-Output "Evidence retained in $root"
}
