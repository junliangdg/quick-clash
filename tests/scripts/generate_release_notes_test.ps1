#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskRoot = Join-Path $taskRepo ('build/script_tests/release_notes/' + [guid]::NewGuid().ToString('N'))
. (Join-Path $PSScriptRoot 'release_notes_fixture.ps1')
$taskFixture = New-ReleaseNotesFixture (Join-Path $taskRoot 'repo')
$taskGenerator = Join-Path $taskRepo 'scripts/generate_release_notes.ps1'
$taskRecords = Join-Path $taskRoot 'releases.json'
$taskOutput = Join-Path $taskRoot 'release_notes.md'
$taskPassed = 0
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Records([object[]]$Items) { ConvertTo-Json -InputObject @($Items) -Depth 5 | Set-Content -LiteralPath $taskRecords -Encoding utf8NoBOM }
function Generate([hashtable]$Options = @{}) {
    $arguments = @{
        Repository = 'fixture/project'; Commit = $taskFixture.Commit; Tag = 'v1.1.0'; SourceRoot = $taskFixture.Root
        PublishedReleasesFile = $taskRecords; PackageNotes = 'Package and license information.'; OutputPath = $taskOutput
    }
    foreach ($key in $Options.Keys) { $arguments[$key] = $Options[$key] }
    & $taskGenerator @arguments
}
function Pass([string]$Name) { $script:taskPassed++; Write-Output "PASS: $Name" }
function Reject([string]$Name, [hashtable]$Options, [string]$Expected) {
    $message = ''
    try { $null = Generate $Options } catch { $message = $_.Exception.Message }
    Require ($message -like "*$Expected*") "$Name did not fail as expected: $message"
    Pass $Name
}
Records @(
    (New-FixtureRelease 'v1.0.0'), (New-FixtureRelease 'v1.1.0-rc.2' $true), (New-FixtureRelease 'v1.1.0-rc.10' $true),
    (New-FixtureRelease 'v1.0.7'), (New-FixtureRelease 'v1.0.9' $false $true), (New-FixtureRelease 'continuous')
)
$taskActual = Generate
Require ($taskActual.PreviousTag -ceq 'v1.0.0' -and $taskActual.IncludedCount -eq 10) 'Stable baseline or filtered count differs.'
foreach ($text in @('### 新增功能', '### 问题修复', '### 性能优化', '### 不兼容变更', '<summary>其他改进</summary>',
        'migrate saved profiles', '批量更新 &amp; &lt;safe&gt; &#64;someone \[link\](bad)', 'Package and license information.',
        "https://github.com/fixture/project/commit/$($taskFixture.Feature)", 'update runtime dependency')) {
    Require ($taskActual.Body.Contains($text)) "Expected release content missing: $text"
}
foreach ($text in @('documentation only', 'test fixture only', 'workflow only', 'tooling only', 'test probe only', 'resource documentation only',
        'maintenance type with product path', 'repository scope with product path', 'unrelated branch', 'future unreleased change')) {
    Require (-not $taskActual.Body.Contains($text)) "Non-product/out-of-range change leaked: $text"
}
Require ($taskActual.Body.IndexOf('### 不兼容变更') -lt $taskActual.Body.IndexOf('### 新增功能')) 'Breaking changes are not first.'
Require (Test-Path -LiteralPath ([IO.Path]::ChangeExtension($taskOutput, '.json'))) 'Comparison diagnostics missing.'
Pass 'stable baseline, software filtering, categories, escaping, annotated tag and bounded range'
$taskActual = Generate @{ Tag = 'v1.1.0-rc.11' }
Require ($taskActual.PreviousTag -ceq 'v1.1.0-rc.10' -and $taskActual.IncludedCount -eq 1) 'Prerelease numeric ordering differs.'
Pass 'prerelease rc.10 follows rc.2'
Records @((New-FixtureRelease 'v1.0.0'))
$taskActual = Generate @{ Tag = 'v1.1.0-rc.11' }
Require ($taskActual.PreviousTag -ceq 'v1.0.0') 'Prerelease did not fall back to stable.'
Pass 'first prerelease compares with stable'
# A failed CI run can leave a v-prefixed tag without any GitHub Release.
# Such a tag is deliberately absent from Records and must not become a base.
$null = Invoke-FixtureGit $taskFixture.Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.0.6', 'v1.1.0-rc.2')
$taskActual = Generate
Require ($taskActual.PreviousTag -ceq 'v1.0.0' -and $taskActual.Body.Contains('批量更新')) 'Unpublished failed version hid software changes.'
Pass 'failed version with tag only is skipped and its software changes are included'
Records @()
$taskActual = Generate @{ Tag = 'v1.0.0'; Commit = $taskFixture.Base }
Require ($taskActual.PreviousTag -ceq '' -and $taskActual.Body.Contains('首次发布') -and $taskActual.IncludedCount -eq 1) 'First release differs.'
Pass 'first release includes initial software commit'
Records @((New-FixtureRelease 'v1.0.0'))
$taskActual = Generate @{ PreviousTag = 'v1.1.0-rc.10' }
Require ($taskActual.PreviousTag -ceq 'v1.1.0-rc.10') 'Explicit previous tag was ignored.'
Pass 'explicit baseline override'
$null = Invoke-FixtureGit $taskFixture.Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.1', $taskFixture.Commit)
Reject 'nonstandard tag requires explicit baseline' @{ Tag = 'v1.1' } 'vMAJOR.MINOR.PATCH'
$taskActual = Generate @{ Tag = 'v1.1'; PreviousTag = 'v1.0.0' }
Require ($taskActual.PreviousTag -ceq 'v1.0.0' -and $taskActual.IncludedCount -eq 10) 'Explicit nonstandard version comparison failed.'
Pass 'nonstandard tag supports explicit baseline'
$taskSaved = Generate
$taskManualBody = "手动前言`n`n$($taskSaved.Body)`n`n手动已知问题"
Records @((New-FixtureRelease 'v1.0.0'), (New-FixtureRelease 'v1.0.8'), (New-FixtureRelease 'v1.1.0' $false $false $taskManualBody))
$taskActual = Generate
Require ($taskActual.PreviousTag -ceq 'v1.0.0' -and $taskActual.Body -ceq $taskManualBody -and $taskActual.ExistingRelease) 'Rerun changed baseline or manual content.'
Pass 'rerun preserves manual text and frozen baseline despite newly published older version'
Records @((New-FixtureRelease 'v1.0.0'), (New-FixtureRelease 'v1.1.0' $false $false 'Legacy release description.'))
$taskActual = Generate
Require ($taskActual.Body.EndsWith('Legacy release description.') -and -not $taskActual.Body.Contains('Package and license information.')) 'Legacy release text was overwritten/duplicated.'
Pass 'legacy release body is preserved'
Records @((New-FixtureRelease 'v1.1.0' $false $false '<!-- quickclash:release-notes:start -->'))
Reject 'malformed managed block' @{} 'markers'
Records @((New-FixtureRelease 'v1.0.0'), (New-FixtureRelease 'v1.1.0' $false $false $taskManualBody))
$null = Invoke-FixtureGit $taskFixture.Root @('-c', 'tag.gpgsign=false', 'tag', '-f', 'v1.0.0', 'v1.1.0-rc.2')
Reject 'moved saved baseline' @{} 'saved previous tag moved'
$null = Invoke-FixtureGit $taskFixture.Root @('-c', 'tag.gpgsign=false', 'tag', '-f', 'v1.0.0', $taskFixture.Base)
Reject 'explicit non-ancestor' @{ PreviousTag = 'v1.0.7' } 'not an ancestor'
Reject 'explicit current tag' @{ PreviousTag = 'v1.1.0' } 'must differ'
Reject 'package commit mismatch' @{ Commit = $taskFixture.Base } 'package commit'
Records @((New-FixtureRelease 'v1.0.5'))
Reject 'missing published tag fails closed' @{} 'Git command failed'
Records @((New-FixtureRelease 'v1.0.8'))
$taskActual = Generate
Require ($taskActual.IncludedCount -eq 0 -and $taskActual.Body.Contains('没有可自动归类的软件变更')) 'Empty software range differs.'
Pass 'tags on same commit produce explicit empty summary'
'null' | Set-Content -LiteralPath $taskRecords
Reject 'malformed discovery is not first release' @{} 'JSON array'
$taskMaintenance = Add-FixtureCommit $taskFixture.Root 'docs/only.md' 'feat: documentation after version'
$null = Invoke-FixtureGit $taskFixture.Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.2.0')
Records @((New-FixtureRelease 'v1.1.0'))
# HEAD already had a genuine software commit after v1.1.0; override the base
# with a tag at that commit to isolate a docs-only release interval.
$null = Invoke-FixtureGit $taskFixture.Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.1.9', 'HEAD^')
$taskActual = Generate @{ Tag = 'v1.2.0'; Commit = $taskMaintenance; PreviousTag = 'v1.1.9' }
Require ($taskActual.CommitCount -eq 1 -and $taskActual.IncludedCount -eq 0 -and
    -not $taskActual.Body.Contains('### 新增功能')) 'Documentation-only version leaked into feature notes.'
Pass 'maintenance-only release has no software entries or empty headings'
$taskShallow = Join-Path $taskRoot 'shallow'
$taskUri = [Uri]::new($taskFixture.Root + [IO.Path]::DirectorySeparatorChar).AbsoluteUri
$null = Invoke-FixtureGit $taskRoot @('clone', '--quiet', '--depth=1', $taskUri, $taskShallow)
Reject 'shallow history fails closed' @{ SourceRoot = $taskShallow } 'complete history'
Write-Output "PASS: $taskPassed release-note scenarios; fixtures and preview retained at $taskRoot"
