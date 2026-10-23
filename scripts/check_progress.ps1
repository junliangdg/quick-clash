[CmdletBinding()]
param(
    [string]$ProgressRoot = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($ProgressRoot)) {
    $ProgressRoot = Join-Path $repoRoot 'docs/progress'
}
$progressPath = (Resolve-Path -LiteralPath $ProgressRoot).Path
$taskPath = Join-Path $progressPath 'TASKS.md'
$checklistPath = Join-Path $progressPath 'CHECKLISTS.md'
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$issues = [System.Collections.Generic.List[string]]::new()
$tasks = @{}
$gates = @{}
$mvp = @{}
$checks = @{}

function Add-Issue([string]$Message) {
    $issues.Add($Message)
}

function Read-Text([string]$Path) {
    return [System.IO.File]::ReadAllText($Path, $utf8)
}

function Read-Cells([string]$Line) {
    return @($Line.Trim().Trim([char]'|').Split('|') | ForEach-Object { $_.Trim() })
}

# Completion evidence must be a saved local report, not a template or a remote-only claim.
function Test-Evidence([string]$Value, [string]$Context) {
    $links = [regex]::Matches($Value, '\[[^\]\r\n]+\]\((?<target>[^)\r\n]+)\)')
    if ($links.Count -eq 0) {
        Add-Issue "$Context requires a saved report link under reports/."
        return
    }
    $reportsRoot = [System.IO.Path]::GetFullPath((Join-Path $progressPath 'reports'))
    $reportsPrefix = $reportsRoot.TrimEnd([char[]]'\/') + [System.IO.Path]::DirectorySeparatorChar
    foreach ($link in $links) {
        $target = ($link.Groups['target'].Value -split '#', 2)[0]
        if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or [string]::IsNullOrWhiteSpace($target)) {
            Add-Issue "$Context must link a local report; record external CI evidence inside that report."
            continue
        }
        $fullPath = [System.IO.Path]::GetFullPath((Join-Path $progressPath $target))
        if (-not $fullPath.StartsWith($reportsPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
            [System.IO.Path]::GetFileName($fullPath) -eq 'README.md' -or
            [System.IO.Path]::GetExtension($fullPath) -ne '.md' -or
            -not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            Add-Issue "$Context has an invalid or missing report: $target"
        }
    }
}

$taskText = Read-Text $taskPath
foreach ($line in ($taskText -split '\r?\n')) {
    if ($line -notmatch '^\|\s*P\d-') { continue }
    $cells = @(Read-Cells $line)
    if ($cells.Count -ne 9) { Add-Issue "Task row requires 9 columns: $line"; continue }
    $id, $phase, $packages, $title, $state, $owner, $dependencies, $evidence, $next = $cells
    if ($id -notmatch '^P[0-5]-\d{2}$') { Add-Issue "Invalid task ID: $id"; continue }
    if ($tasks.ContainsKey($id)) { Add-Issue "Duplicate task ID: $id"; continue }
    if ($phase -ne $id.Substring(0, 2)) { Add-Issue "$id has inconsistent phase $phase." }
    if ($state -notin @('Todo', 'Doing', 'Review', 'Done', 'Blocked')) { Add-Issue "$id has invalid state $state." }
    if ([string]::IsNullOrWhiteSpace($title)) { Add-Issue "$id has no deliverable." }
    $packageIds = @($packages -split ',\s*')
    foreach ($package in $packageIds) {
        if ($package -notmatch '^W(0\d|1\d|20)$') { Add-Issue "$id has invalid work package $package." }
    }
    $dependencyIds = @()
    if ($dependencies -ne '—') { $dependencyIds = @($dependencies -split ',\s*') }
    if ($state -in @('Doing', 'Review', 'Done') -and $owner -in @('', '—', '未分配')) {
        Add-Issue "$id requires an owner in state $state."
    }
    if ($state -ne 'Done' -and $next -in @('', '—')) { Add-Issue "$id requires a next step or a concrete blocker." }
    if ($state -in @('Review', 'Done') -or $evidence -notin @('', '—')) { Test-Evidence $evidence $id }
    $tasks[$id] = [pscustomobject]@{
        Id = $id; Phase = $phase; Packages = $packageIds; State = $state
        Dependencies = $dependencyIds; Evidence = $evidence
    }
}
if ($tasks.Count -eq 0) { Add-Issue 'No task rows found.' }
foreach ($task in $tasks.Values) {
    foreach ($dependency in $task.Dependencies) {
        if (-not $tasks.ContainsKey($dependency)) { Add-Issue "$($task.Id) refers to missing dependency $dependency."; continue }
        if ($task.State -in @('Doing', 'Review') -and $tasks[$dependency].State -ne 'Done') {
            Add-Issue "$($task.Id) is $($task.State), but dependency $dependency is not Done."
        }
        if ($tasks[$dependency].Phase -gt $task.Phase) { Add-Issue "$($task.Id) depends on a later phase $dependency." }
    }
}
$visitState = @{}
function Visit-Task([string]$Id) {
    if ($visitState[$Id] -eq 'Visiting') { Add-Issue "Dependency cycle at $Id."; return }
    if ($visitState[$Id] -eq 'Visited') { return }
    $visitState[$Id] = 'Visiting'
    foreach ($dependency in $tasks[$Id].Dependencies) {
        if ($tasks.ContainsKey($dependency)) { Visit-Task $dependency }
    }
    $visitState[$Id] = 'Visited'
}
foreach ($id in $tasks.Keys) { Visit-Task $id }
foreach ($number in 0..20) {
    $package = 'W{0:D2}' -f $number
    if (@($tasks.Values | Where-Object { $package -in $_.Packages }).Count -eq 0) {
        Add-Issue "Work package $package has no tracked task."
    }
}

$resultStates = @('NotRun', 'Pass', 'Fail', 'Blocked', 'Stale')
$checklistText = Read-Text $checklistPath
foreach ($line in ($checklistText -split '\r?\n')) {
    if ($line -match '^\|\s*G\d') {
        $cells = @(Read-Cells $line)
        if ($cells.Count -ne 5) { Add-Issue "Gate row requires 5 columns: $line"; continue }
        $id, $phase, $state, $evidence, $next = $cells
        if ($id -notmatch '^G[0-5]$') { Add-Issue "Invalid gate ID: $id"; continue }
        if ($gates.ContainsKey($id)) { Add-Issue "Duplicate gate ID: $id"; continue }
        if ($phase -ne ('P' + $id.Substring(1))) { Add-Issue "$id has inconsistent phase $phase." }
        if ($state -notin $resultStates) { Add-Issue "$id has invalid state $state." }
        if ($state -in @('Pass', 'Fail') -or $evidence -notin @('', '—')) { Test-Evidence $evidence $id }
        if ($state -in @('Fail', 'Blocked', 'Stale') -and $next -in @('', '—')) { Add-Issue "$id needs a reason and next step." }
        $gates[$id] = [pscustomobject]@{ Phase = $phase; State = $state }
    }
    if ($line -match '^\|\s*MVP-') {
        $cells = @(Read-Cells $line)
        if ($cells.Count -ne 6) { Add-Issue "MVP row requires 6 columns: $line"; continue }
        $id, $condition, $taskRefs, $state, $evidence, $next = $cells
        if ($id -notmatch '^MVP-(0[1-9]|1[0-6])$') { Add-Issue "Invalid MVP ID: $id"; continue }
        if ($mvp.ContainsKey($id)) { Add-Issue "Duplicate MVP ID: $id"; continue }
        if ($state -notin $resultStates) { Add-Issue "$id has invalid state $state." }
        foreach ($reference in ($taskRefs -split ',\s*')) {
            if (-not $tasks.ContainsKey($reference)) { Add-Issue "$id refers to missing task $reference." }
        }
        if ($state -in @('Pass', 'Fail') -or $evidence -notin @('', '—')) { Test-Evidence $evidence $id }
        if ($state -in @('Fail', 'Blocked', 'Stale') -and $next -in @('', '—')) { Add-Issue "$id needs a reason and next step." }
        $mvp[$id] = $state
    }
    if ($line -match '^- \[(?<checked>[ xX])\] (?<id>G[0-5]-[A-E])：') {
        $id = $Matches['id']
        $isChecked = $Matches['checked'] -ne ' '
        if ($checks.ContainsKey($id)) { Add-Issue "Duplicate gate check $id."; continue }
        $checks[$id] = $isChecked
        if ($isChecked) { Test-Evidence $line $id }
    }
}
foreach ($number in 0..5) {
    $id = 'G' + $number
    $phase = 'P' + $number
    $phaseTasks = @($tasks.Values | Where-Object { $_.Phase -eq $phase })
    if ($phaseTasks.Count -eq 0) { Add-Issue "$phase has no tasks." }
    if (-not $gates.ContainsKey($id)) { Add-Issue "Missing gate $id."; continue }
    foreach ($letter in @('A', 'B', 'C', 'D', 'E')) {
        if (-not $checks.ContainsKey($id + '-' + $letter)) { Add-Issue "Missing check $id-$letter." }
    }
    if ($gates[$id].State -ne 'Pass') { continue }
    if ($number -gt 0 -and $gates['G' + ($number - 1)].State -ne 'Pass') { Add-Issue "$id requires the preceding gate to Pass." }
    if (@($phaseTasks | Where-Object { $_.State -ne 'Done' }).Count -gt 0) { Add-Issue "$id cannot Pass while $phase tasks are not Done." }
    foreach ($letter in @('A', 'B', 'C', 'D', 'E')) {
        if (-not $checks[$id + '-' + $letter]) { Add-Issue "$id cannot Pass while check $id-$letter is unchecked." }
    }
}
foreach ($number in 1..16) {
    $id = 'MVP-{0:D2}' -f $number
    if (-not $mvp.ContainsKey($id)) { Add-Issue "Missing MVP result $id." }
}
if ($gates['G2'].State -eq 'Pass' -and @($mvp.Values | Where-Object { $_ -ne 'Pass' }).Count -gt 0) {
    Add-Issue 'G2 cannot Pass until all 16 MVP results Pass.'
}

# Include links in workflow, templates and reports, without treating planned code paths as existing files.
foreach ($file in (Get-ChildItem -LiteralPath $progressPath -File -Recurse -Filter '*.md')) {
    foreach ($link in [regex]::Matches((Read-Text $file.FullName), '(?<!!)\[[^\]\r\n]*\]\((?<target>[^)\r\n]+)\)')) {
        $target = $link.Groups['target'].Value
        if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $target.StartsWith('#')) { continue }
        $relative = ($target -split '#', 2)[0]
        $resolved = Join-Path $file.DirectoryName $relative
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) { Add-Issue "Broken link in $($file.Name): $target" }
    }
}

Write-Output 'Progress snapshot (task counts are not product completion percentages):'
foreach ($number in 0..5) {
    $phase = 'P' + $number
    $items = @($tasks.Values | Where-Object { $_.Phase -eq $phase })
    $counts = foreach ($state in @('Todo', 'Doing', 'Review', 'Done', 'Blocked')) {
        '{0}={1}' -f $state, @($items | Where-Object { $_.State -eq $state }).Count
    }
    Write-Output ("$phase ($($items.Count)): " + ($counts -join ', '))
}
Write-Output ('Gates: ' + (($gates.Keys | Sort-Object | ForEach-Object { $_ + '=' + $gates[$_].State }) -join ', '))
Write-Output ("MVP Pass=$(@($mvp.Values | Where-Object { $_ -eq 'Pass' }).Count)/16")
if ($issues.Count -gt 0) {
    foreach ($issue in $issues) { Write-Output ('ERROR: ' + $issue) }
    throw "Progress validation failed with $($issues.Count) issue(s)."
}
Write-Output 'PASS: task IDs, dependencies, work packages, gate/MVP results, report references and local links are consistent.'
Write-Output 'This validates tracking structure only; it does not run or certify product tests.'

