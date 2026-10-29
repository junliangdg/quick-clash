#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackageRoot,
    [Parameter(Mandatory)][string]$Destination
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Requires Windows and a BUILD_TESTING local validation package.' }
$taskRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$taskSource = (Resolve-Path -LiteralPath $PackageRoot).Path
$taskDestination = [IO.Path]::GetFullPath($Destination, $taskRepo)
$taskPrefix = $taskSource.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
if (Test-Path -LiteralPath $taskDestination) { throw 'Use a new destination to preserve prior evidence.' }
if ($taskDestination.StartsWith($taskPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Validation kit must be outside the source package.'
}
. (Join-Path $PSScriptRoot 'service_package.ps1')
$taskPackage = Open-ServicePackage $taskSource
try {
    $taskManifestFile = $taskPackage.Files[0]
    $taskManifestFile.Stream.Position = 0
    $taskReader = [IO.StreamReader]::new($taskManifestFile.Stream, [Text.UTF8Encoding]::new($false, $true), $true, 4096, $true)
    try { $taskManifest = $taskReader.ReadToEnd() | ConvertFrom-Json -Depth 12 } finally { $taskReader.Dispose() }
    if ($taskManifest.purpose -cne 'Private Windows local validation; not a signed public release or clean-machine acceptance') {
        throw 'Use the local validation package, with BUILD_TESTING probes enabled.'
    }
    $taskParent = [IO.Path]::GetDirectoryName($taskDestination)
    while (-not (Test-Path -LiteralPath $taskParent)) { $taskParent = [IO.Path]::GetDirectoryName($taskParent) }
    Assert-ServicePackagePath $taskParent
    $null = [IO.Directory]::CreateDirectory($taskDestination)
    $taskPackageTarget = Join-Path $taskDestination 'package'
    Copy-ServicePackage $taskPackage $taskPackageTarget
    # The service copier deliberately excludes root documentation/scripts and the manifest.
    foreach ($taskRelative in 'readme.md', 'install_service_windows.ps1', 'service_package.ps1', 'metadata/package_manifest.json') {
        $taskEntry = @($taskManifest.files | Where-Object { $_.path -ceq $taskRelative })
        $taskStream = $null
        try {
            if ($taskRelative -ceq 'metadata/package_manifest.json') {
                $taskStream = $taskManifestFile.Stream
            } else {
                if ($taskEntry.Count -ne 1 -or $taskEntry[0].sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Missing or invalid root package entry.' }
                $taskPath = Join-Path $taskSource $taskRelative
                Assert-ServicePackagePath $taskPath
                $taskStream = [IO.File]::Open($taskPath, 'Open', 'Read', 'Read')
                if ($taskStream.Length -ne $taskEntry[0].bytes -or
                    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($taskStream)).ToLowerInvariant() -cne $taskEntry[0].sha256) {
                    throw 'Root package integrity check failed.'
                }
            }
            $taskStream.Position = 0
            $taskOutput = [IO.File]::Open((Join-Path $taskPackageTarget $taskRelative), 'CreateNew', 'Write', 'None')
            try { $taskStream.CopyTo($taskOutput); $taskOutput.Flush($true) } finally { $taskOutput.Dispose() }
        } finally {
            if ($taskStream -and $taskStream -ne $taskManifestFile.Stream) { $taskStream.Dispose() }
        }
    }
    foreach ($taskRelative in 'scripts/native_process.ps1', 'tests/scripts/desktop_package_test.ps1',
        'tests/fixtures/mvp/desktop_loopback.yaml', 'tests/fixtures/network/server_cert.pem', 'tests/fixtures/network/server_key.pem') {
        $taskTarget = Join-Path $taskDestination $taskRelative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskTarget))
        Copy-Item -LiteralPath (Join-Path $taskRepo $taskRelative) -Destination $taskTarget
    }
    Copy-Item -LiteralPath (Join-Path $taskRepo 'resources/windows_validation_readme.md') -Destination (Join-Path $taskDestination 'readme.md')
    $taskFiles = @(Get-ChildItem -LiteralPath $taskDestination -File -Recurse | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path = [IO.Path]::GetRelativePath($taskDestination, $_.FullName).Replace([char]92, [char]47)
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    [ordered]@{
        schema_version = 1
        created_at_utc = [DateTime]::UtcNow.ToString('o')
        purpose = 'Portable local validation kit; hashes detect transfer damage, not authenticity or acceptance.'
        requires = 'Windows x64 desktop; PowerShell 7.2+; BUILD_TESTING package probes'
        source_manifest_sha256 = (Get-FileHash -LiteralPath (Join-Path $taskPackageTarget 'metadata/package_manifest.json')).Hash.ToLowerInvariant()
        files = $taskFiles
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskDestination 'validation_manifest.json') -Encoding utf8NoBOM
    Write-Output "Created Windows validation kit: $taskDestination"
} finally { Close-ServicePackage $taskPackage }
