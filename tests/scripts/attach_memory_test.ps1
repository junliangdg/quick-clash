#Requires -Version 7.2
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$script = Join-Path $repo 'bench/attach_memory_windows.ps1'
. (Join-Path $repo 'bench/process_memory.ps1')
$root = Join-Path $repo ('build/script_tests/attach_memory/' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
try {
    $unavailable = [QuickClash.Diagnostics.ProcessMemoryCounters]::new()
    $unavailable.WorkingSetSize = [UIntPtr]::new([uint64]4096)
    $unavailable.PrivateUsage = [UIntPtr]::new([uint64]2048)
    $unavailableError = $null
    try { [QuickClash.Diagnostics.ProcessMemory]::Validate($unavailable) }
    catch { $unavailableError = $_ }
    if (-not $unavailableError -or
        $unavailableError.Exception.ToString() -notmatch 'Private working set is unavailable from GetProcessMemoryInfo') {
        throw 'An unavailable private working set must be rejected explicitly.'
    }
    Write-Output 'PASS: a missing private working set is rejected even when other counters are available.'

    $sampleError = $null
    $rows = $null
    $sampled = $false
    try { & $script -ClientPid $PID -OutputDirectory (Join-Path $root 'self') -Seconds 1 }
    catch { $sampleError = $_ }
    $summaryPath = Join-Path $root 'self/summary.json'
    if ($sampleError) {
        if ($sampleError.Exception.ToString() -notmatch 'Private working set is unavailable from GetProcessMemoryInfo') {
            throw $sampleError
        }
        if (Test-Path -LiteralPath (Join-Path $root 'self')) {
            if (-not (Test-Path -LiteralPath $summaryPath)) { throw 'Incomplete sample is missing its summary.' }
            $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
            $rows = @(Import-Csv -LiteralPath (Join-Path $root 'self/memory.csv'))
            if ($summary.status -ne 'Incomplete') { throw 'Unavailable counter was recorded as a complete sample.' }
        }
        Write-Output 'PASS: unavailable private working set stops sampling without a zero-valued result.'
        Write-Output 'NotRun: a complete self-sampling interval could not be validated.'
    } else {
        $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
        $rows = @(Import-Csv -LiteralPath (Join-Path $root 'self/memory.csv'))
        if ($summary.status -ne 'Sampled' -or $rows.Count -lt 1) { throw 'Self sampling did not produce a result.' }
        $sampled = $true
    }
    if ($null -ne $rows) {
        if (@($rows | Where-Object {
                $_.pid -ne $PID -or $_.role -ne 'client' -or [int]$_.sample -le 0 -or
                [uint64]$_.private_working_set_bytes -le 0 -or
                [uint64]$_.working_set_bytes -lt [uint64]$_.private_working_set_bytes -or
                [uint64]$_.private_commit_bytes -le 0
            }).Count) { throw 'Self sampling counters failed.' }
    }
    if ($sampled) { Write-Output 'PASS: self sampling reports three coherent counters.' }
    $rejected = $false
    try { & $script -ClientPid $PID -CorePid $PID -OutputDirectory (Join-Path $root 'duplicate') }
    catch { $rejected = $true }
    if (-not $rejected -or (Test-Path (Join-Path $root 'duplicate'))) { throw 'Duplicate PID accepted.' }
    $rejected = $false
    try { & $script -ClientPid ([int]::MaxValue) -OutputDirectory (Join-Path $root 'missing') }
    catch { $rejected = $true }
    if (-not $rejected -or (Test-Path (Join-Path $root 'missing'))) { throw 'Missing PID accepted.' }
    if (-not (Get-Process -Id $PID -ErrorAction Stop)) { throw 'Sampler controlled the target.' }
    Write-Output 'PASS: duplicate and missing PID rejected; target remains running.'
} finally {
    Write-Output "Evidence retained in $root"
}
