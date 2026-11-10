#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Repository,
    [Parameter(Mandatory)][string]$Commit,
    [Parameter(Mandatory)][string]$Tag,
    [string]$SourceRoot = (Join-Path $PSScriptRoot '..'),
    [AllowEmptyString()][string]$PreviousTag,
    [string]$PublishedReleasesFile = '',
    [string]$PackageNotes = '',
    [string]$OutputPath = ''
)
$ErrorActionPreference = 'Stop'
if ($Repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -cnotmatch '^[0-9a-f]{40}$' -or
    $Tag -cnotmatch '^v[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Invalid release repository, commit or tag.' }
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path

function Invoke-ReleaseGit([string[]]$Arguments) {
    $result = @(& git -C $SourceRoot -c core.quotepath=false -c i18n.logOutputEncoding=utf-8 @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "Release notes Git command failed: $($Arguments -join ' ')" }
    $result
}
function Resolve-ReleaseTag([string]$Name) {
    if ($Name -cnotmatch '^v[A-Za-z0-9][A-Za-z0-9._-]*$') { throw "Invalid previous version tag: $Name" }
    (Invoke-ReleaseGit @('rev-parse', '--verify', "refs/tags/$Name^{commit}")).Trim()
}
function ConvertTo-ReleaseVersion([string]$Name) {
    $version = $null
    if ($Name -cmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?$' -and
        [System.Management.Automation.SemanticVersion]::TryParse($Name.Substring(1), [ref]$version)) { return $version }
    $null
}
function Test-ReleaseAncestor([string]$Ancestor) {
    & git -C $SourceRoot merge-base --is-ancestor $Ancestor $Commit
    if ($LASTEXITCODE -eq 0) { return $true }
    if ($LASTEXITCODE -eq 1) { return $false }
    throw "Could not check release ancestry: $Ancestor"
}
function ConvertTo-ReleaseText([string]$Text) {
    $encoded = [Net.WebUtility]::HtmlEncode($Text).Replace('@', '&#64;')
    [regex]::Replace($encoded, '([\\`*_\[\]])', '\$1')
}
function Test-ProductPath([string]$Path) {
    # Documentation, test probes, build/repository tooling and license text are
    # not product changes, even when their commit is incorrectly marked feat.
    if ($Path -match '\.md$|^app/(?:[^/]*probe[^/]*|keyboard_probe)\.(?:cpp|h)$') { return $false }
    $Path -cmatch '^(app|src|qml|services)/|^resources/(icons/|i18n/[^/]+\.ts$|metadata/|quickclash\.rc$)|^docs/baseline/(config_policy|release_matrix)\.json$'
}

if ((Invoke-ReleaseGit @('rev-parse', '--is-shallow-repository')) -ne 'false') {
    throw 'Release notes require complete history and tags (checkout fetch-depth: 0).'
}
if ((Resolve-ReleaseTag $Tag) -cne $Commit) { throw 'Release tag does not point to the package commit.' }

if ($PublishedReleasesFile) {
    # Explicit offline input supports previews/tests without GitHub credentials.
    $records = Get-Content -LiteralPath $PublishedReleasesFile -Raw | ConvertFrom-Json -NoEnumerate
    if ($records -isnot [Array]) { throw 'Published releases input must be a JSON array.' }
    $releases = @($records)
} else {
    $json = @(& gh api "repos/$Repository/releases?per_page=100" --paginate --slurp)
    if ($LASTEXITCODE -ne 0) { throw 'Could not read published releases; release notes were not generated.' }
    $pages = ($json -join "`n") | ConvertFrom-Json -NoEnumerate
    if ($pages -isnot [Array]) { throw 'Published releases response must contain JSON pages.' }
    $releases = @(foreach ($page in $pages) {
        if ($page -isnot [Array]) { throw 'Published releases page must be a JSON array.' }
        foreach ($release in $page) { $release }
    })
}
foreach ($release in $releases) {
    if ($release.tag_name -isnot [string] -or $release.draft -isnot [bool] -or $release.prerelease -isnot [bool]) {
        throw 'Published release record is missing tag/draft/prerelease fields.'
    }
}
$existing = @($releases | Where-Object { $_.tag_name -ceq $Tag })
if ($existing.Count -gt 1) { throw 'Duplicate release records for the current tag.' }
$body = if ($existing.Count) { [string]$existing[0].body } else { '' }
$startMarker = '<!-- quickclash:release-notes:start -->'
$endMarker = '<!-- quickclash:release-notes:end -->'
$start = $body.IndexOf($startMarker, [StringComparison]::Ordinal)
$end = $body.IndexOf($endMarker, [StringComparison]::Ordinal)
$saved = $null
if ($start -ge 0 -or $end -ge 0) {
    if ($start -lt 0 -or $end -lt $start -or
        $body.IndexOf($startMarker, $start + $startMarker.Length, [StringComparison]::Ordinal) -ge 0 -or
        $body.IndexOf($endMarker, $end + $endMarker.Length, [StringComparison]::Ordinal) -ge 0) {
        throw 'Release notes markers are incomplete or duplicated; preserve the existing body for manual repair.'
    }
    $managed = $body.Substring($start, $end + $endMarker.Length - $start)
    $metadata = [regex]::Matches($managed, '(?m)^<!-- quickclash:release-notes:metadata (.+) -->\r?$')
    if ($metadata.Count -ne 1) { throw 'Release notes comparison metadata is missing or duplicated.' }
    $saved = $metadata[0].Groups[1].Value | ConvertFrom-Json
    if ($saved.schema -ne 1 -or $saved.tag -cne $Tag -or $saved.commit -cne $Commit) {
        throw 'Release notes metadata differs from the current tag/commit.'
    }
    if ($saved.previous_tag -isnot [string] -or $saved.previous_commit -isnot [string] -or
        ($saved.previous_tag -and $saved.previous_commit -cnotmatch '^[0-9a-f]{40}$') -or
        (-not $saved.previous_tag -and $saved.previous_commit)) { throw 'Release notes previous-version metadata is invalid.' }
}

$previous = ''
$previousCommit = ''
if ($PSBoundParameters.ContainsKey('PreviousTag')) {
    $previous = $PreviousTag
} elseif ($null -ne $saved) {
    $previous = [string]$saved.previous_tag
} else {
    $version = ConvertTo-ReleaseVersion $Tag
    if ($null -eq $version) {
        throw 'Automatic comparison requires vMAJOR.MINOR.PATCH[-prerelease]; specify -PreviousTag for other v-prefixed tags.'
    }
    $candidates = @(foreach ($release in $releases) {
        if ($release.draft -or $release.tag_name -ceq $Tag -or $release.tag_name -cnotmatch '^v[0-9]') { continue }
        $candidate = ConvertTo-ReleaseVersion $release.tag_name
        if ($null -eq $candidate -or $candidate -ge $version) { continue }
        $isPrerelease = [bool]$candidate.PreReleaseLabel -or [bool]$release.prerelease
        $sameVersion = $candidate.Major -eq $version.Major -and $candidate.Minor -eq $version.Minor -and $candidate.Patch -eq $version.Patch
        if ($isPrerelease -and (-not $version.PreReleaseLabel -or -not $sameVersion)) { continue }
        $candidateCommit = Resolve-ReleaseTag $release.tag_name
        if (Test-ReleaseAncestor $candidateCommit) {
            [pscustomobject]@{ Tag = $release.tag_name; Version = $candidate }
        }
    })
    $selected = $candidates | Sort-Object -Property Version -Descending | Select-Object -First 1
    if ($selected) { $previous = $selected.Tag }
}
if ($previous) {
    if ($previous -ceq $Tag) { throw 'Previous tag must differ from the current tag.' }
    $previousCommit = Resolve-ReleaseTag $previous
    if (-not (Test-ReleaseAncestor $previousCommit)) { throw 'Previous tag is not an ancestor of the current release.' }
}
if ($null -ne $saved -and -not $PSBoundParameters.ContainsKey('PreviousTag') -and
    $previousCommit -cne [string]$saved.previous_commit) { throw 'The saved previous tag moved; comparison was not regenerated.' }

$range = if ($previousCommit) { "$previousCommit..$Commit" } else { $Commit }
$commits = @(Invoke-ReleaseGit @('log', '--reverse', '--topo-order', '--no-merges', '--format=%H', $range, '--'))
$groups = [ordered]@{ '不兼容变更' = @(); '新增功能' = @(); '问题修复' = @(); '性能优化' = @(); '其他改进' = @() }
$skipped = [Collections.Generic.List[object]]::new()
foreach ($sha in $commits) {
    $message = (Invoke-ReleaseGit @('show', '-s', '--format=%s%x00%b', $sha, '--')) -join "`n"
    $parts = $message.Split([char]0, 2)
    $subject = $parts[0].Trim()
    $commitBody = if ($parts.Count -gt 1) { $parts[1] } else { '' }
    $type = ''
    $scope = ''
    $breaking = $false
    $description = $subject
    if ($subject -cmatch '^(?<type>[a-z]+)(?:\((?<scope>[^)]+)\))?(?<breaking>!)?:\s*(?<description>.+)$') {
        $type = $Matches.type
        $scope = $Matches.scope
        $breaking = $Matches.breaking -eq '!'
        $description = $Matches.description
    }
    if ($type -cin @('docs', 'test', 'tests', 'ci', 'build', 'chore', 'style') -or $scope -cmatch '^(docs?|tests?|ci|build|repo|workflow)$') {
        $skipped.Add([pscustomobject]@{ commit = $sha; reason = 'maintenance_type_or_scope' })
        continue
    }
    $paths = @(Invoke-ReleaseGit @('diff-tree', '--root', '--no-commit-id', '--name-only', '--no-renames', '-r', $sha, '--'))
    if (-not @($paths | Where-Object { Test-ProductPath $_ }).Count) {
        $skipped.Add([pscustomobject]@{ commit = $sha; reason = 'no_product_paths' })
        continue
    }
    $breakingNote = ''
    if ($commitBody -cmatch '(?m)^BREAKING[ -]CHANGE:\s*(.+)$') {
        $breaking = $true
        $breakingNote = $Matches[1].Trim()
    }
    $group = if ($breaking) { '不兼容变更' } elseif ($type -ceq 'feat') { '新增功能' } elseif ($type -ceq 'fix') { '问题修复' } elseif ($type -ceq 'perf') { '性能优化' } else { '其他改进' }
    $text = ConvertTo-ReleaseText $description
    if ($scope) { $text = "$(ConvertTo-ReleaseText $scope)：$text" }
    if ($breakingNote) { $text += "；$(ConvertTo-ReleaseText $breakingNote)" }
    $groups[$group] += "- $text ([$($sha.Substring(0, 7))](https://github.com/$Repository/commit/$sha))"
}

$selection = [ordered]@{
    schema = 1; tag = $Tag; commit = $Commit; previous_tag = $previous; previous_commit = $previousCommit
}
$lines = [Collections.Generic.List[string]]::new()
$lines.Add($startMarker)
$lines.Add("<!-- quickclash:release-notes:metadata $($selection | ConvertTo-Json -Compress) -->")
$lines.Add('## 本次更新')
$lines.Add('')
$lines.Add($(if ($previous) { "相比 $previous，仅列出软件相关变更：" } else { '首次发布，仅列出软件相关变更：' }))
foreach ($group in $groups.Keys) {
    if (-not $groups[$group].Count) { continue }
    $lines.Add('')
    if ($group -eq '其他改进') {
        $lines.Add('<details>')
        $lines.Add('<summary>其他改进</summary>')
        $lines.Add('')
    } else { $lines.Add("### $group"); $lines.Add('') }
    foreach ($line in $groups[$group]) { $lines.Add($line) }
    if ($group -eq '其他改进') { $lines.Add(''); $lines.Add('</details>') }
}
if ($commits.Count -eq $skipped.Count) { $lines.Add(''); $lines.Add('此版本没有可自动归类的软件变更。') }
$lines.Add('')
if ($previous) {
    $lines.Add("[完整提交对比](https://github.com/$Repository/compare/$previous...$Tag)")
} else { $lines.Add("[版本源码](https://github.com/$Repository/tree/$Commit)") }
$lines.Add($endMarker)
$generated = $lines -join "`n"
if ($start -ge 0) {
    $body = $body.Substring(0, $start) + $generated + $body.Substring($end + $endMarker.Length)
} else {
    if (-not $body) { $body = $PackageNotes }
    $body = $generated + $(if ($body) { "`n`n$body" } else { '' })
}
$result = [pscustomobject]@{
    Tag = $Tag; Commit = $Commit; PreviousTag = $previous; PreviousCommit = $previousCommit
    CommitCount = $commits.Count; IncludedCount = $commits.Count - $skipped.Count
    Skipped = @($skipped.ToArray()); ExistingRelease = [bool]$existing.Count; Body = $body
}
if ($OutputPath) {
    $output = [IO.Path]::GetFullPath($OutputPath)
    $null = New-Item -ItemType Directory -Path (Split-Path $output) -Force
    $body | Set-Content -LiteralPath $output -Encoding utf8NoBOM
    $result | Select-Object * -ExcludeProperty Body | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath ([IO.Path]::ChangeExtension($output, '.json')) -Encoding utf8NoBOM
}
$global:LASTEXITCODE = 0
$result
