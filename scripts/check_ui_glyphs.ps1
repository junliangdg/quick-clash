[CmdletBinding()]
param([string]$SourceRoot = '')

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $PSScriptRoot '..'
}
$root = (Resolve-Path -LiteralPath $SourceRoot).Path

$problemGlyphs = [System.Collections.Generic.HashSet[int]]::new()
foreach ($codePoint in 0x25BE, 0x25B8, 0x2713, 0x270E, 0x22EE) {
    [void]$problemGlyphs.Add($codePoint)
}

$issues = [System.Collections.Generic.List[string]]::new()
$scanned = 0
foreach ($folder in 'qml') {
    $folderPath = Join-Path $root $folder
    if (-not (Test-Path -LiteralPath $folderPath -PathType Container)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $folderPath -Recurse -File -Filter '*.qml') {
        $scanned++
        $lineNumber = 0
        foreach ($line in [System.IO.File]::ReadAllLines($file.FullName, [System.Text.Encoding]::UTF8)) {
            $lineNumber++
            $index = 0
            while ($index -lt $line.Length) {
                $codePoint = [char]::ConvertToUtf32($line, $index)
                $width = if ([char]::IsSurrogatePair($line, $index)) { 2 } else { 1 }
                $index += $width
                if ($problemGlyphs.Contains($codePoint)) {
                    $issues.Add(('{0}:{1}: U+{2:X4} is a known icon glyph that triggers Windows font fallback' -f
                        $file.FullName, $lineNumber, $codePoint))
                }
            }
        }
    }
}
if ($issues.Count -gt 0) {
    foreach ($issue in $issues) { Write-Output ('ERROR: ' + $issue) }
    throw "UI glyph validation failed with $($issues.Count) issue(s)."
}
Write-Output "PASS: $scanned QML files contain none of the five known fallback-triggering icon glyphs."
