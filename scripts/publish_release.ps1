#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Repository,
    [Parameter(Mandatory)][string]$Commit,
    [Parameter(Mandatory)][string]$RefName,
    [Parameter(Mandatory)][ValidateSet('branch', 'tag')][string]$RefType,
    [string]$Archive = 'dist/quickclash_windows_x64.zip',
    [string]$NotesDirectory = (Join-Path $PSScriptRoot '../build/release/logs')
)
$ErrorActionPreference = 'Stop'
if ($Repository -cnotmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$' -or $Commit -cnotmatch '^[0-9a-f]{40}$') { throw 'Invalid release repository or commit.' }
$taskContinuous = $RefType -eq 'branch'
if (-not $taskContinuous -and $RefName -cnotmatch '^v[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Version releases require a v-prefixed tag.' }
$taskTag = if ($taskContinuous) { 'continuous' } else { $RefName }
$taskPrerelease = $taskContinuous -or $RefName.Contains('-')
$taskArchive = (Resolve-Path -LiteralPath $Archive).Path
$taskChecksum = (Resolve-Path -LiteralPath "$Archive.sha256").Path
$taskExpected = ((Get-Content -LiteralPath $taskChecksum -Raw).Trim() -split '\s+')[0]
if ((Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash.ToLowerInvariant() -cne $taskExpected) { throw 'Release archive checksum differs.' }
$taskNotesDirectory = $NotesDirectory
$null = New-Item -ItemType Directory -Path $taskNotesDirectory -Force
$taskNotes = [IO.Path]::GetFullPath((Join-Path $taskNotesDirectory 'release_notes.md'))
$taskTitle = if ($taskContinuous) { 'QuickClash continuous (Windows x64)' } else { "QuickClash $taskTag (Windows x64)" }
@"
$taskTitle

Windows x64 portable Release build from commit $Commit. Download the ZIP, extract the complete directory, and run bin/quickclash.exe.

Includes the pinned official Mihomo core, Geo resources, Qt/MSVC runtime dependencies and licenses. Tests, test probes, build tools, logs and source files are excluded. The package is currently unsigned.

Linux/macOS are checked by the CI build jobs and are not included in this release.
"@ | Set-Content -LiteralPath $taskNotes -Encoding utf8NoBOM
if ($taskContinuous) {
    # Concurrency groups do not guarantee arrival order. An older run must not
    # move the continuous release backwards after a newer commit has arrived.
    $taskBranch = [Uri]::EscapeDataString($RefName)
    $taskHead = & gh api "repos/$Repository/commits/$taskBranch" --jq .sha
    if ($LASTEXITCODE -ne 0) { throw 'Could not verify the current branch commit.' }
    if ($taskHead.Trim() -cne $Commit) {
        Write-Output "Skip superseded continuous build: $Commit"
        return
    }
    # Only this CI-owned moving tag is updated; version tags remain unchanged.
    & gh api --method PATCH "repos/$Repository/git/refs/tags/continuous" -f "sha=$Commit" -F force=true 1>$null 2>$null
    if ($LASTEXITCODE -ne 0) {
        & gh api --method POST "repos/$Repository/git/refs" -f 'ref=refs/tags/continuous' -f "sha=$Commit"
        if ($LASTEXITCODE -ne 0) { throw 'Could not create/update the continuous tag.' }
    }
}
& gh release view $taskTag --repo $Repository 1>$null 2>$null
if ($LASTEXITCODE -eq 0) {
    $taskArguments = @('release', 'edit', $taskTag, '--repo', $Repository, '--title', $taskTitle, '--notes-file', $taskNotes)
    if ($taskPrerelease) { $taskArguments += @('--prerelease', '--latest=false') }
    & gh @taskArguments
    if ($LASTEXITCODE -ne 0) { throw 'Could not update release notes.' }
    & gh release upload $taskTag $taskArchive $taskChecksum --repo $Repository --clobber
} else {
    $taskArguments = @('release', 'create', $taskTag, $taskArchive, $taskChecksum, '--repo', $Repository, '--verify-tag', '--title', $taskTitle, '--notes-file', $taskNotes)
    if ($taskPrerelease) { $taskArguments += @('--prerelease', '--latest=false') }
    & gh @taskArguments
}
if ($LASTEXITCODE -ne 0) { throw 'GitHub Release asset publication failed.' }
