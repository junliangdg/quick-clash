[CmdletBinding()]
param(
    [switch]$Check
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$designRoot = Join-Path $repoRoot 'docs/design'
$outputPath = Join-Path $designRoot 'QtQuick-Mihomo-完整设计文档.md'
$readmePath = Join-Path $designRoot 'README.md'
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$lf = [string][char]10

function Read-NormalizedText([string]$Path) {
    return [System.IO.File]::ReadAllText($Path, $utf8).Replace(([string][char]13 + [char]10), $lf).Replace([string][char]13, $lf)
}

# Keep chapter order explicit: missing or accidentally renamed sources are errors.
$chapters = @(Get-ChildItem -LiteralPath $designRoot -File -Filter '*.md' |
    Where-Object { $_.Name -match '^\d{2}-.+\.md$' } | Sort-Object Name)
if ($chapters.Count -ne 19) {
    throw "Expected 19 chapter sources, found $($chapters.Count). Update the catalog deliberately when adding chapters."
}
$chapterIds = @{}
$chapterTexts = @{}
$titles = @{}
for ($index = 0; $index -lt $chapters.Count; $index++) {
    $chapter = $chapters[$index]
    $number = '{0:D2}' -f ($index + 1)
    if (-not $chapter.Name.StartsWith($number + '-')) {
        throw "Expected chapter $number, found $($chapter.Name)."
    }
    $source = (Read-NormalizedText $chapter.FullName).Trim()
    $firstLine = ($source -split $lf, 2)[0]
    if (-not $firstLine.StartsWith('# ' + $number + ' ')) {
        throw "Chapter title does not match its file number: $($chapter.Name)."
    }
    $chapterIds[$chapter.Name] = 'chapter-' + $number
    $chapterTexts[$chapter.Name] = $source
    $titles[$chapter.Name] = $firstLine.Substring(2)
}

function Convert-ChapterText([string]$Source) {
    $inFence = $false
    $fenceMark = ''
    $lines = foreach ($line in ($Source -split $lf)) {
        if ($line -match '^\s*([\x60]{3,}|~{3,})') {
            $marker = $Matches[1].Substring(0, 1)
            if (-not $inFence) { $inFence = $true; $fenceMark = $marker }
            elseif ($marker -eq $fenceMark) { $inFence = $false }
            $line
            continue
        }
        if (-not $inFence) {
            $line = [regex]::Replace($line, '^(#{1,5}) ', '#$1 ')
            foreach ($fileName in $chapterIds.Keys) {
                $line = $line.Replace('](' + $fileName + ')', '](#' + $chapterIds[$fileName] + ')')
            }
        }
        $line
    }
    if ($inFence) { throw 'Unclosed code fence in chapter source.' }
    return $lines -join $lf
}

$readme = Read-NormalizedText $readmePath
$versionMatch = [regex]::Match($readme, '(?m)^版本：([^；]+)；日期：([^；]+)；')
if (-not $versionMatch.Success) { throw 'README must declare a document version and date.' }
$parts = [System.Collections.Generic.List[string]]::new()
$parts.Add('# QuickClash 完整设计文档（合并版）')
$parts.Add("版本 $($versionMatch.Groups[1].Value) · $($versionMatch.Groups[2].Value)。由 19 个分册生成，修改请以分册为准。")
$parts.Add('**官方预编译 Mihomo；G3 交付 Windows 日常使用，当前先落实跨平台静态兼容，三平台/五平台后续逐步真实覆盖；方案C极简桌面工具风，统一控件/页面与Light/Dark主题，桌面侧栏＋页面、窄窗底部四个图标入口；英文源文案/系统语言中英文/英文回退；Windows 客户端专用工作集 ≤100 MB，内核单列。**')
$parts.Add('本文件包含功能范围、首页、架构、恢复、实施与验收设计。实际实现、验证证据和下一步以 [开发进度台账](../progress/README.md) 为准，设计内容不表示功能已完成。生成与一致性检查使用 scripts/build_design_docs.ps1 及其 -Check 参数。')
$parts.Add('## 目录')
$parts.Add(($chapters | ForEach-Object { '- [' + $titles[$_.Name] + '](#' + $chapterIds[$_.Name] + ')' }) -join $lf)
foreach ($chapter in $chapters) {
    $parts.Add('---')
    $parts.Add('<a id="' + $chapterIds[$chapter.Name] + '"></a>')
    $parts.Add((Convert-ChapterText $chapterTexts[$chapter.Name]))
}
$referenceStart = $readme.IndexOf('## 5. 使用与限制')
if ($referenceStart -lt 0) { throw 'README use/limitations and reference sections are missing.' }
$parts.Add('---')
$parts.Add('## 参考资料与使用说明')
$parts.Add((Convert-ChapterText $readme.Substring($referenceStart).Trim()))
$expected = ($parts -join ($lf + $lf)).TrimEnd() + $lf

# Validate source and generated links before writing anything.
$documents = @{}
$documents[$readmePath] = $readme
foreach ($chapter in $chapters) { $documents[$chapter.FullName] = $chapterTexts[$chapter.Name] }
$referenceRoot = Join-Path $repoRoot 'docs/reference'
if (Test-Path -LiteralPath $referenceRoot -PathType Container) {
    foreach ($reference in (Get-ChildItem -LiteralPath $referenceRoot -File -Filter '*.md')) {
        $documents[$reference.FullName] = Read-NormalizedText $reference.FullName
    }
}
$documents[$outputPath] = $expected
foreach ($path in $documents.Keys) {
    $content = $documents[$path]
    foreach ($link in [regex]::Matches($content, '(?<!!)\[[^\]\r\n]*\]\((?<target>[^)\r\n]+)\)')) {
        $target = $link.Groups['target'].Value
        if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') { continue }
        if ($target.StartsWith('#')) {
            if ($target -match '^#chapter-\d{2}$' -and -not $content.Contains('<a id="' + $target.Substring(1) + '"></a>')) {
                throw "Missing anchor $target in $path."
            }
            continue
        }
        $relativePath = ($target -split '#', 2)[0]
        $resolvedLink = Join-Path ([System.IO.Path]::GetDirectoryName($path)) $relativePath
        if (-not (Test-Path -LiteralPath $resolvedLink -PathType Leaf)) {
            throw "Broken local link '$target' in $path."
        }
    }
}

if ($Check) {
    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf) -or
        (Read-NormalizedText $outputPath) -cne $expected) {
        throw 'Merged design document is stale. Run pwsh -File scripts/build_design_docs.ps1 and commit its output.'
    }
    Write-Output 'PASS: 19 chapters, merged content, chapter anchors and local Markdown links are consistent.'
} else {
    [System.IO.File]::WriteAllText($outputPath, $expected, $utf8)
    Write-Output "Generated $outputPath from 19 chapters; chapter anchors and local Markdown links are valid."
}
