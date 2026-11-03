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

foreach ($path in @($taskPath, $checklistPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $issues.Add("Missing current progress file: $path")
    }
}

# Only explicitly labelled current-task states are checked. History may keep
# its original vocabulary and tables; prose and new tasks need no fixed schema.
if (Test-Path -LiteralPath $taskPath -PathType Leaf) {
    $taskText = [System.IO.File]::ReadAllText($taskPath, $utf8)
    foreach ($match in [regex]::Matches($taskText, '(?<![\p{L}\p{N}_])状态：(?<state>[^\s；;，,。、:：()（）]+)')) {
        $state = $match.Groups['state'].Value
        if ($state -notin @('待做', '进行中', '完成', '阻塞')) {
            $issues.Add("Invalid current task state: $state")
        }
    }
}

# Check file targets, including images and archived documents, without imposing
# evidence formats or pretending to validate external URLs or Markdown anchors.
foreach ($file in (Get-ChildItem -LiteralPath $progressPath -File -Recurse -Filter '*.md')) {
    $text = [System.IO.File]::ReadAllText($file.FullName, $utf8)
    foreach ($match in [regex]::Matches($text, '!?\[[^\]\r\n]*\]\((?<target>[^)\r\n]+)\)')) {
        $target = $match.Groups['target'].Value
        if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $target.StartsWith('#')) { continue }
        $relative = ($target -split '#', 2)[0]
        $relative = [uri]::UnescapeDataString($relative)
        $resolved = Join-Path $file.DirectoryName $relative
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            $issues.Add("Broken local link in $($file.FullName): $target")
        }
    }
}

if ($issues.Count -gt 0) {
    foreach ($issue in $issues) { Write-Output ('ERROR: ' + $issue) }
    throw "Progress validation failed with $($issues.Count) issue(s)."
}
Write-Output 'PASS: current explicit task states and local progress Markdown file links are valid.'
Write-Output 'This is a documentation check only; it does not run or certify product tests.'
