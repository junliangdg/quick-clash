#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskRoot = Join-Path $taskRepo ('.cache/publish_release_test_' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $taskRoot
$taskArchive = Join-Path $taskRoot 'runtime.zip'
[IO.File]::WriteAllText($taskArchive, 'isolated release asset fixture')
$taskHash = (Get-FileHash -LiteralPath $taskArchive).Hash.ToLowerInvariant()
"$taskHash  runtime.zip" | Set-Content -LiteralPath "$taskArchive.sha256" -Encoding utf8NoBOM
$taskPublish = Join-Path $taskRepo 'scripts/publish_release.ps1'
$taskCommit = '1' * 40
$taskGhMock = [pscustomobject]@{ Calls = [Collections.Generic.List[object]]::new(); Existing = $false; MissingTag = $false; UploadFailure = $false; Head = $taskCommit }
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
# Intercept only gh; exercise the real release orchestration without remote writes.
function gh {
    $taskGhMock.Calls.Add(@($args))
    $global:LASTEXITCODE = 0
    if ($args[0] -eq 'api' -and $args -contains '--jq') { return $taskGhMock.Head }
    if ($args[0] -eq 'api' -and $args -contains 'PATCH' -and $taskGhMock.MissingTag) { $global:LASTEXITCODE = 1 }
    if ($args[0] -eq 'release' -and $args[1] -eq 'view' -and -not $taskGhMock.Existing) { $global:LASTEXITCODE = 1 }
    if ($args[0] -eq 'release' -and $args[1] -eq 'upload' -and $taskGhMock.UploadFailure) { $global:LASTEXITCODE = 1 }
}
function Publish([string]$Type, [string]$Name) {
    & $taskPublish -Repository 'fixture/project' -Commit $taskCommit -RefType $Type -RefName $Name -Archive $taskArchive -NotesDirectory $taskRoot
}
try {
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
    Publish 'tag' 'v1.2.3'
    Require ($taskGhMock.Calls.Count -eq 2 -and $taskGhMock.Calls[0][0] -eq 'release') 'Version publication mutated a tag.'
    Require (-not ($taskGhMock.Calls[1] -contains '--prerelease')) 'Stable version was marked prerelease.'
    $taskGhMock.Calls.Clear()
    Publish 'tag' 'v1.2.3-rc.1'
    Require ($taskGhMock.Calls[1] -contains '--prerelease') 'RC version was not marked prerelease.'
    $taskGhMock.Existing = $true; $taskGhMock.UploadFailure = $true
    $taskFailed = $false
    try { Publish 'tag' 'v1.2.3' } catch { $taskFailed = $true }
    Require $taskFailed 'Asset upload failure was swallowed.'
    $taskGhMock.Calls.Clear()
    [IO.File]::AppendAllText($taskArchive, 'corrupt')
    $taskFailed = $false
    try { Publish 'tag' 'v1.2.3' } catch { $taskFailed = $true }
    Require ($taskFailed -and $taskGhMock.Calls.Count -eq 0) 'Corrupt archive reached GitHub.'
    Write-Output 'PASS: first/repeat/superseded continuous, version/RC release, upload failure and checksum rejection (mock gh only)'
} finally {
    $taskResolved = [IO.Path]::GetFullPath($taskRoot)
    $taskCache = [IO.Path]::GetFullPath((Join-Path $taskRepo '.cache')) + [IO.Path]::DirectorySeparatorChar
    if ($taskResolved.StartsWith($taskCache, [StringComparison]::OrdinalIgnoreCase)) { Remove-Item -LiteralPath $taskResolved -Recurse -Force }
}
