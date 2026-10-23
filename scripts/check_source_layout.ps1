[CmdletBinding()]
param(
    [string]$SourceRoot = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $PSScriptRoot '..'
}
$rootPath = (Resolve-Path -LiteralPath $SourceRoot).Path
$sourceDirectories = @('app', 'src', 'qml', 'services', 'tests', 'bench', 'cmake', 'scripts', 'packaging', 'resources')
$fixedNames = @('CMakeLists.txt', 'CMakePresets.json', 'CMakeUserPresets.json', 'CTestConfig.cmake', 'qmldir')
$rootFiles = @('CMakeLists.txt', 'CMakePresets.json', 'CMakeUserPresets.json', 'CTestConfig.cmake', '.clang-format', '.clang-tidy', '.editorconfig')
$issues = [System.Collections.Generic.List[string]]::new()
$files = [System.Collections.Generic.List[System.IO.FileInfo]]::new()
$directories = [System.Collections.Generic.List[System.IO.DirectoryInfo]]::new()

# Only project-owned source roots are traversed; upstream/vendor and generated files belong outside them.
foreach ($sourceDirectory in $sourceDirectories) {
    $path = Join-Path $rootPath $sourceDirectory
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { continue }
    $items = @(Get-ChildItem -LiteralPath $path -Recurse -Force)
    $directories.Add((Get-Item -LiteralPath $path))
    foreach ($item in $items) {
        if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
            $issues.Add("Source tree contains an unreviewed link: $($item.FullName)")
            continue
        }
        if ($item.PSIsContainer) { $directories.Add($item) } else { $files.Add($item) }
    }
}
foreach ($fileName in $rootFiles) {
    $path = Join-Path $rootPath $fileName
    if (Test-Path -LiteralPath $path -PathType Leaf) { $files.Add((Get-Item -LiteralPath $path)) }
}
foreach ($directory in $directories) {
    if ($directory.Name -cnotmatch '^[a-z][a-z0-9]*(?:_[a-z0-9]+)*$') {
        $issues.Add("Directory must use lowercase snake_case: $($directory.FullName)")
    }
}
foreach ($file in $files) {
    if ($file.Name -cin $fixedNames -or $file.Name -cin @('.clang-format', '.clang-tidy', '.editorconfig')) { continue }
    if ($file.Name -cnotmatch '^[a-z][a-z0-9]*(?:_[a-z0-9]+)*(?:\.[a-z0-9]+)*$') {
        $issues.Add("File must use lowercase snake_case: $($file.FullName)")
    }
}
if ($issues.Count -gt 0) {
    foreach ($issue in $issues) { Write-Output ('ERROR: ' + $issue) }
    throw "Source naming validation failed with $($issues.Count) issue(s)."
}
$productFiles = @($files | Where-Object { $_.Extension -cin @('.cpp', '.h', '.mm', '.qml') })
Write-Output "PASS: $($directories.Count) source directories and $($files.Count) files follow QuickClash naming rules."
if ($productFiles.Count -eq 0) {
    Write-Output 'No product C++/QML source files exist yet; this is not build or implementation evidence.'
} else {
    Write-Output "Product source files inspected: $($productFiles.Count). Formatting, compilation and runtime behavior require separate checks."
}

