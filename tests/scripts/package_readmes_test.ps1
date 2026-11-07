#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$taskRoot = Join-Path $taskRepo ('build/script_tests/package_readmes/' + [guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($taskRoot)
$taskCase = 0
$taskReadmes = @('readme.md', 'readme_zh_cn.md')
function Require($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Require-Failure([scriptblock]$Action, [string]$Expected) {
    $taskFailure = $null
    try { & $Action | Out-Null } catch { $taskFailure = $_.Exception.Message }
    Require ($taskFailure -and $taskFailure.Contains($Expected)) "Expected '$Expected', received '$taskFailure'."
}
function Write-FixtureManifest([string]$Directory) {
    $taskEntries = @(Get-ChildItem -LiteralPath $Directory -File -Recurse | Where-Object {
        $_.FullName -ne (Join-Path $Directory 'metadata/package_manifest.json')
    } | ForEach-Object {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($Directory, $_.FullName).Replace([char]92, [char]47)
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()
        }
    })
    [ordered]@{
        schema_version = 1
        purpose = 'Private Windows local validation; not a signed public release or clean-machine acceptance'
        files = $taskEntries
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $Directory 'metadata/package_manifest.json') -Encoding utf8NoBOM
}
function New-Fixture {
    $script:taskCase++
    $taskDirectory = Join-Path $taskRoot "case_$script:taskCase"
    foreach ($taskRelative in 'bin/quickclash.exe', 'bin/quickclash_service.exe', 'bin/quickclash_network.exe',
        'core/mihomo.exe', 'bin/Qt6Core.dll', 'metadata/config_policy.json', 'licenses/quickclash_gpl_3.txt', 'licenses/source_obligations.md') {
        $taskPath = Join-Path $taskDirectory $taskRelative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskPath))
        [IO.File]::WriteAllText($taskPath, "fixture:$taskRelative")
    }
    & (Join-Path $taskRepo 'scripts/copy_windows_readmes.ps1') -Destination $taskDirectory
    Write-FixtureManifest $taskDirectory
    return $taskDirectory
}
function Require-Links([string]$Path, [string[]]$Expected) {
    $taskTargets = @([regex]::Matches([IO.File]::ReadAllText($Path), '\[[^\]\r\n]*\]\(([^)\r\n]+)\)') | ForEach-Object {
        $_.Groups[1].Value
    })
    Require (($taskTargets -join '|') -ceq ($Expected -join '|')) "Unexpected links in $Path."
    foreach ($taskTarget in $taskTargets) {
        Require (Test-Path -LiteralPath (Join-Path ([IO.Path]::GetDirectoryName($Path)) $taskTarget) -PathType Leaf) "Broken link in ${Path}: $taskTarget"
    }
}
$taskSourceTargets = @('windows_readme.md', 'windows_readme_zh_cn.md', '../LICENSE', '../docs/baseline/licenses.md')
foreach ($taskSource in 'windows_readme.md', 'windows_readme_zh_cn.md') {
    Require-Links (Join-Path $taskRepo "resources/$taskSource") $taskSourceTargets
}
$taskDirectory = New-Fixture
$taskManifest = Get-Content -LiteralPath (Join-Path $taskDirectory 'metadata/package_manifest.json') -Raw | ConvertFrom-Json
foreach ($taskReadme in $taskReadmes) {
    $taskPath = Join-Path $taskDirectory $taskReadme
    Require-Links $taskPath @('readme.md', 'readme_zh_cn.md', 'licenses/quickclash_gpl_3.txt', 'licenses/source_obligations.md')
    $taskEntry = @($taskManifest.files | Where-Object path -CEQ $taskReadme)
    Require ($taskEntry.Count -eq 1 -and $taskEntry[0].bytes -eq (Get-Item -LiteralPath $taskPath).Length -and
        $taskEntry[0].sha256 -ceq (Get-FileHash -LiteralPath $taskPath).Hash.ToLowerInvariant()) 'Documentation missing from package manifest.'
}
Write-Output 'PASS: source and packaged language/license links resolve; both readmes have manifest sizes and hashes'

$taskArchive = Join-Path $taskRoot 'package.zip'
$taskExtracted = Join-Path $taskRoot 'extracted'
[IO.Compression.ZipFile]::CreateFromDirectory($taskDirectory, $taskArchive)
[IO.Compression.ZipFile]::ExtractToDirectory($taskArchive, $taskExtracted)
$taskKit = Join-Path $taskRoot 'kit'
& (Join-Path $taskRepo 'scripts/create_windows_validation_kit.ps1') -PackageRoot $taskExtracted -Destination $taskKit
$taskKitTransport = Join-Path $taskKit 'package/bin/quickclash_network.exe'
Require (Test-Path -LiteralPath $taskKitTransport -PathType Leaf) 'Validation kit omitted the network transport.'
Require ((Get-FileHash -LiteralPath $taskKitTransport).Hash -ceq
    (Get-FileHash -LiteralPath (Join-Path $taskDirectory 'bin/quickclash_network.exe')).Hash) 'Validation kit changed the network transport.'
foreach ($taskReadme in $taskReadmes) {
    $taskKitReadme = Join-Path $taskKit "package/$taskReadme"
    Require ((Get-FileHash -LiteralPath $taskKitReadme).Hash -ceq (Get-FileHash -LiteralPath (Join-Path $taskDirectory $taskReadme)).Hash) 'Validation kit changed readme contents.'
    Require-Links $taskKitReadme @('readme.md', 'readme_zh_cn.md', 'licenses/quickclash_gpl_3.txt', 'licenses/source_obligations.md')
}
$taskKitManifest = Get-Content -LiteralPath (Join-Path $taskKit 'validation_manifest.json') -Raw | ConvertFrom-Json
Require (@($taskKitManifest.files | Where-Object path -CEQ 'package/bin/quickclash_network.exe').Count -eq 1) 'Network transport missing from validation kit manifest.'
foreach ($taskEntry in $taskKitManifest.files) {
    $taskPath = Join-Path $taskKit $taskEntry.path
    Require ((Get-Item -LiteralPath $taskPath).Length -eq $taskEntry.bytes -and
        (Get-FileHash -LiteralPath $taskPath).Hash.ToLowerInvariant() -ceq $taskEntry.sha256) 'Validation kit manifest mismatch.'
}
Require ((Get-FileHash -LiteralPath (Join-Path $taskExtracted 'metadata/package_manifest.json')).Hash -ceq
    (Get-FileHash -LiteralPath (Join-Path $taskKit 'package/metadata/package_manifest.json')).Hash) 'Source manifest was not preserved.'
Write-Output 'PASS: ZIP round trip and real validation-kit creation preserve network transport, bilingual docs, links, and all manifest hashes'

foreach ($taskReadme in $taskReadmes) {
    foreach ($taskKind in 'missing', 'corrupt', 'duplicate') {
        $taskDirectory = New-Fixture
        $taskManifestPath = Join-Path $taskDirectory 'metadata/package_manifest.json'
        $taskManifest = Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
        switch ($taskKind) {
            'missing' { $taskManifest.files = @($taskManifest.files | Where-Object path -CNE $taskReadme) }
            'corrupt' { [IO.File]::AppendAllText((Join-Path $taskDirectory $taskReadme), 'tampered') }
            'duplicate' { $taskManifest.files += @($taskManifest.files | Where-Object path -CEQ $taskReadme)[0] }
        }
        $taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskManifestPath -Encoding utf8NoBOM
        $taskExpected = if ($taskKind -eq 'corrupt') { 'Root package integrity check failed.' } else { 'Missing or invalid root package entry.' }
        Require-Failure {
            & (Join-Path $taskRepo 'scripts/create_windows_validation_kit.ps1') -PackageRoot $taskDirectory -Destination (Join-Path $taskRoot "kit_$taskCase")
        } $taskExpected
    }
}
Write-Output "PASS: missing, corrupt, and duplicate entries rejected for each language; evidence: $taskRoot"
$global:LASTEXITCODE = 0
