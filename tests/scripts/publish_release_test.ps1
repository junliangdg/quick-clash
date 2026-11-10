#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskRoot = Join-Path $taskRepo ('build/script_tests/publish_release/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $taskRoot -Force
. (Join-Path $PSScriptRoot 'release_notes_fixture.ps1')
$taskFixture = New-ReleaseNotesFixture (Join-Path $taskRoot 'repo')
$taskArchive = Join-Path $taskRoot 'runtime.zip'
[IO.File]::WriteAllText($taskArchive, 'isolated release asset fixture')
$taskHash = (Get-FileHash -LiteralPath $taskArchive).Hash.ToLowerInvariant()
"$taskHash  runtime.zip" | Set-Content -LiteralPath "$taskArchive.sha256" -Encoding utf8NoBOM
$taskPublish = Join-Path $taskRepo 'scripts/publish_release.ps1'
$taskCommit = $taskFixture.Commit
$taskGhMock = [pscustomobject]@{
    Calls = [Collections.Generic.List[object]]::new(); Existing = $false; MissingTag = $false
    UploadFailure = $false; ApiFailure = $false; Head = $taskCommit
    Releases = @((New-FixtureRelease 'v1.0.0'))
}
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
# Intercept only gh; exercise the real release orchestration without remote writes.
function gh {
    $taskGhMock.Calls.Add(@($args))
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'api' -and $args[1] -like '*/releases?per_page=*') {
        if ($taskGhMock.ApiFailure) { $global:LASTEXITCODE = 1; return }
        # Model gh --paginate --slurp, including a baseline on a later page.
        $pages = [object[]]::new(2)
        $pages[0] = @(); $pages[1] = @($taskGhMock.Releases)
        return ConvertTo-Json -InputObject $pages -Depth 8
    }
    if ($args[0] -eq 'api' -and $args -contains '--jq') { return $taskGhMock.Head }
    if ($args[0] -eq 'api' -and $args -contains 'PATCH' -and $taskGhMock.MissingTag) { $global:LASTEXITCODE = 1 }
    if ($args[0] -eq 'release' -and $args[1] -eq 'view' -and -not $taskGhMock.Existing) { $global:LASTEXITCODE = 1 }
    if ($args[0] -eq 'release' -and $args[1] -eq 'upload' -and $taskGhMock.UploadFailure) { $global:LASTEXITCODE = 1 }
}
function Publish([string]$Type, [string]$Name) {
    & $taskPublish -Repository 'fixture/project' -Commit $taskCommit -RefType $Type -RefName $Name -Archive $taskArchive -NotesDirectory $taskRoot -SourceRoot $taskFixture.Root
}
$taskGhMock.MissingTag = $true
Publish 'branch' 'main'
Require ($taskGhMock.Calls.Count -eq 5) 'First continuous publication did not create tag/release.'
Require ($taskGhMock.Calls[2] -contains 'POST') 'Missing continuous tag did not use create.'
Require ($taskGhMock.Calls[4] -contains '--prerelease' -and $taskGhMock.Calls[4] -contains '--verify-tag') 'Continuous release flags differ.'
$taskGhMock.Calls.Clear(); $taskGhMock.MissingTag = $false; $taskGhMock.Existing = $true
Publish 'branch' 'main'
Require ($taskGhMock.Calls.Count -eq 5 -and $taskGhMock.Calls[1] -contains 'PATCH') 'Existing continuous tag was not updated.'
Require ($taskGhMock.Calls[4] -contains '--clobber') 'Repeat run did not replace assets.'
$taskGhMock.Calls.Clear(); $taskGhMock.Head = '2' * 40
Publish 'branch' 'main'
Require ($taskGhMock.Calls.Count -eq 1) 'Superseded build mutated the continuous release.'
$taskGhMock.Head = $taskCommit
$taskGhMock.Calls.Clear(); $taskGhMock.Existing = $false
Publish 'tag' 'v1.1.0'
Require ($taskGhMock.Calls.Count -eq 2 -and $taskGhMock.Calls[0] -contains '--slurp' -and $taskGhMock.Calls[1][1] -eq 'create') 'Version publication mutated a tag or did not paginate releases.'
Require (-not ($taskGhMock.Calls[1] -contains '--prerelease')) 'Stable version was marked prerelease.'
$taskBody = Get-Content -LiteralPath (Join-Path $taskRoot 'release_notes.md') -Raw
Require ($taskBody.Contains('### 新增功能') -and $taskBody.Contains('### 问题修复') -and
    $taskBody.Contains('Windows x64 portable Release build') -and -not $taskBody.Contains('documentation only')) 'Generated software notes or package text differ.'
$taskGhMock.Calls.Clear()
Publish 'tag' 'v1.1.0-rc.11'
Require ($taskGhMock.Calls[1] -contains '--prerelease') 'RC version was not marked prerelease.'
$taskGhMock.Calls.Clear()
$taskManualBody = "Manual introduction.`n$($taskBody.TrimEnd())`nManual known issue."
$taskGhMock.Releases = @((New-FixtureRelease 'v1.0.0'), (New-FixtureRelease 'v1.1.0' $false $false $taskManualBody))
Publish 'tag' 'v1.1.0'
Require ($taskGhMock.Calls.Count -eq 3 -and $taskGhMock.Calls[1][1] -eq 'edit' -and $taskGhMock.Calls[2] -contains '--clobber') 'Repeat version publication did not edit notes and replace assets.'
Require ((Get-Content -LiteralPath (Join-Path $taskRoot 'release_notes.md') -Raw).TrimEnd() -ceq $taskManualBody) 'Repeat publication replaced manual text or duplicated notes.'
$taskGhMock.Calls.Clear(); $taskGhMock.ApiFailure = $true
$taskFailed = $false
try { Publish 'tag' 'v1.1.0' } catch { $taskFailed = $true }
Require ($taskFailed -and $taskGhMock.Calls.Count -eq 1) 'Failed release discovery reached remote mutation.'
$taskGhMock.ApiFailure = $false
$taskGhMock.Releases = @((New-FixtureRelease 'v1.1.0' $false $false '<!-- quickclash:release-notes:start -->'))
$taskGhMock.Calls.Clear()
$taskFailed = $false
try { Publish 'tag' 'v1.1.0' } catch { $taskFailed = $true }
Require ($taskFailed -and $taskGhMock.Calls.Count -eq 1) 'Failed notes generation reached remote mutation.'
$taskGhMock.Releases = @((New-FixtureRelease 'v1.0.0'), (New-FixtureRelease 'v1.1.0' $false $false $taskManualBody))
$taskGhMock.Existing = $true; $taskGhMock.UploadFailure = $true
$taskFailed = $false
try { Publish 'tag' 'v1.1.0' } catch { $taskFailed = $true }
Require $taskFailed 'Asset upload failure was swallowed.'
$taskGhMock.Calls.Clear()
[IO.File]::AppendAllText($taskArchive, 'corrupt')
$taskFailed = $false
try { Publish 'tag' 'v1.1.0' } catch { $taskFailed = $true }
Require ($taskFailed -and $taskGhMock.Calls.Count -eq 0) 'Corrupt archive reached GitHub.'
Write-Output "PASS: 10 publish scenarios: first/repeat/superseded continuous, version/RC, manual text, API/notes/upload failure and checksum rejection (mock gh only); evidence at $taskRoot"
