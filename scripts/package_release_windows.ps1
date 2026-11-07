#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/release',
    [string]$Destination = 'dist/quickclash_windows_x64',
    [string]$Version = 'continuous',
    [string]$Commit = '',
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows x64 release packaging only.' }
if ($Version -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') { throw 'Invalid package version.' }
if ($Commit -and $Commit -cnotmatch '^[0-9a-fA-F]{40}$') { throw 'Expected a full commit SHA.' }
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskBuild = (Resolve-Path -LiteralPath ([IO.Path]::GetFullPath($BuildDirectory, $taskRepo))).Path
$taskDestination = [IO.Path]::GetFullPath($Destination, $taskRepo)
$taskArchive = "$taskDestination.zip"
foreach ($taskPath in @($taskDestination, $taskArchive, "$taskArchive.sha256")) {
    if (Test-Path -LiteralPath $taskPath) { throw "Use a new destination: $taskPath" }
}
$taskCachePath = Join-Path $taskBuild 'CMakeCache.txt'
$taskCache = Get-Content -LiteralPath $taskCachePath -Raw
if ($taskCache -notmatch '(?m)^BUILD_TESTING:BOOL=OFF\r?$' -or
    $taskCache -notmatch '(?m)^CMAKE_BUILD_TYPE:STRING=Release\r?$' -or
    $taskCache -notmatch '(?m)^QUICKCLASH_BUILD_UI:BOOL=ON\r?$') {
    throw 'Use a Release configuration with BUILD_TESTING=OFF and QUICKCLASH_BUILD_UI=ON.'
}
$taskMatrixPath = Join-Path $taskRepo 'docs/baseline/release_matrix.json'
$taskMatrix = Get-Content -LiteralPath $taskMatrixPath -Raw | ConvertFrom-Json
if ($taskMatrix.license -cne 'GPL-3.0-only' -or
    -not (Test-Path -LiteralPath (Join-Path $taskRepo 'LICENSE') -PathType Leaf)) {
    throw 'Project license metadata or LICENSE file is missing.'
}
function Assert-Hash([string]$Path, [string]$Expected) {
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -cne $Expected) {
        throw "Package dependency hash mismatch: $Path"
    }
}
function Copy-SourceObligations([string]$Source, [string]$Destination) {
    $taskText = Get-Content -LiteralPath $Source -Raw
    $taskText = $taskText.Replace('](../../LICENSE)', '](quickclash_gpl_3.txt)')
    $taskText = $taskText.Replace('](../../resources/licenses/lucide_isc.txt)', '](lucide_isc.txt)')
    $taskText = $taskText.Replace('](../../resources/licenses/simple_icons_cc0.txt)', '](simple_icons_cc0.txt)')
    $taskText = $taskText.Replace('[预览说明](../reference/README.md)', '预览说明（仓库 docs/reference/README.md）')
    Set-Content -LiteralPath $Destination -Value $taskText -Encoding utf8NoBOM -NoNewline
}
$taskCore = Join-Path $taskRepo '.cache/mihomo/mihomo.exe'
$taskCoreLicense = Join-Path $taskRepo '.cache/mihomo/license.txt'
Assert-Hash $taskCore $taskMatrix.mihomo.windows.executable_sha256
Assert-Hash $taskCoreLicense $taskMatrix.mihomo.license_sha256
Assert-Hash (Join-Path $taskRepo '.cache/geo/license.txt') $taskMatrix.geo.license_sha256
foreach ($taskAsset in @($taskMatrix.geo.geoip, $taskMatrix.geo.geosite)) {
    $taskAssetPath = Join-Path $taskRepo ('.cache/geo/' + $taskAsset.file)
    Assert-Hash $taskAssetPath $taskAsset.sha256
    if ((Get-Item -LiteralPath $taskAssetPath).Length -ne $taskAsset.bytes) { throw 'Geo size mismatch.' }
}
& (Join-Path $PSScriptRoot 'deploy_windows.ps1') -BuildDirectory $taskBuild -Destination $taskDestination -CMake $CMake
foreach ($taskName in @('core', 'geo', 'licenses', 'metadata')) {
    $null = New-Item -ItemType Directory -Path (Join-Path $taskDestination $taskName) -Force
}
Copy-Item -LiteralPath $taskCore -Destination (Join-Path $taskDestination 'core/mihomo.exe')
foreach ($taskAsset in @($taskMatrix.geo.geoip, $taskMatrix.geo.geosite)) {
    Copy-Item -LiteralPath (Join-Path $taskRepo ('.cache/geo/' + $taskAsset.file)) -Destination (Join-Path $taskDestination ('geo/' + $taskAsset.file))
}
Copy-Item -LiteralPath $taskCoreLicense -Destination (Join-Path $taskDestination 'licenses/mihomo_gpl_3.txt')
Copy-Item -LiteralPath (Join-Path $taskRepo '.cache/geo/license.txt') -Destination (Join-Path $taskDestination 'licenses/meta_rules_dat_gpl_3.txt')
Copy-Item -LiteralPath (Join-Path $taskBuild '_deps/yaml_cpp-src/LICENSE') -Destination (Join-Path $taskDestination 'licenses/yaml_cpp_mit.txt')
Copy-SourceObligations (Join-Path $taskRepo 'docs/baseline/licenses.md') (Join-Path $taskDestination 'licenses/source_obligations.md')
foreach ($taskLicense in Get-ChildItem -LiteralPath (Join-Path $taskRepo 'resources/licenses') -File) {
    Copy-Item -LiteralPath $taskLicense.FullName -Destination (Join-Path $taskDestination 'licenses')
}
& (Join-Path $PSScriptRoot 'copy_windows_readmes.ps1') -Destination $taskDestination
Copy-Item -LiteralPath $taskMatrixPath -Destination (Join-Path $taskDestination 'metadata/release_matrix.json')
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs/baseline/config_policy.json') -Destination (Join-Path $taskDestination 'metadata/config_policy.json')
# Preserve Qt-provided license/notices when present; deployment keeps runtime
# libraries and the redistributable CRT alongside the application.
$taskQtMatch = [regex]::Match($taskCache, '(?m)^Qt6_DIR:PATH=(.+)\r?$')
if (-not $taskQtMatch.Success) { throw 'Qt installation missing from CMake cache.' }
$taskQtRoot = [IO.Path]::GetFullPath('../../..', $taskQtMatch.Groups[1].Value.Trim())
$taskLicenseRoots = @((Join-Path $taskQtRoot 'licenses'), (Join-Path (Split-Path (Split-Path $taskQtRoot)) 'Licenses'))
foreach ($taskLicenseRoot in $taskLicenseRoots | Select-Object -Unique) {
    if (Test-Path -LiteralPath $taskLicenseRoot -PathType Container) {
        foreach ($taskLicense in Get-ChildItem -LiteralPath $taskLicenseRoot -File -Recurse) {
            $taskRelative = [IO.Path]::GetRelativePath($taskLicenseRoot, $taskLicense.FullName)
            $taskTarget = Join-Path $taskDestination (Join-Path 'licenses/qt' $taskRelative)
            $null = New-Item -ItemType Directory -Path (Split-Path $taskTarget) -Force
            Copy-Item -LiteralPath $taskLicense.FullName -Destination $taskTarget
        }
    }
}
$taskExecutableNames = @('quickclash.exe', 'quickclash_service.exe', 'quickclash_network.exe', 'mihomo.exe')
foreach ($taskFile in Get-ChildItem -LiteralPath $taskDestination -File -Recurse) {
    $taskRelative = [IO.Path]::GetRelativePath($taskDestination, $taskFile.FullName).Replace('\', '/')
    if ($taskFile.Extension -in '.pdb', '.lib', '.obj', '.cpp', '.h', '.log' -or
        $taskRelative -match '(^|/)(tests?|bench|CMakeFiles|logs)(/|$)' -or
        ($taskFile.Extension -eq '.exe' -and $taskFile.Name -notin $taskExecutableNames) -or
        $taskFile.Name -match '^Qt6Testd?.dll$') {
        throw "Non-runtime content in release package: $taskRelative"
    }
}
foreach ($taskRequired in @('bin/quickclash.exe', 'bin/quickclash_service.exe', 'bin/quickclash_network.exe', 'bin/Qt6Core.dll', 'bin/msvcp140.dll', 'bin/vcruntime140.dll', 'core/mihomo.exe', 'licenses/quickclash_gpl_3.txt', 'readme.md', 'readme_zh_cn.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $taskDestination $taskRequired) -PathType Leaf)) { throw "Missing runtime file: $taskRequired" }
}
$taskFiles = @(Get-ChildItem -LiteralPath $taskDestination -File -Recurse | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = [IO.Path]::GetRelativePath($taskDestination, $_.FullName).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
[ordered]@{
    schema_version = 1; version = $Version; commit = $Commit; platform = 'windows'; arch = 'x64'
    created_at_utc = [DateTime]::UtcNow.ToString('o'); build_type = 'Release'; build_testing = $false
    signed = $false; official_core = $taskMatrix.mihomo.windows.url; files = $taskFiles
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskDestination 'metadata/package_manifest.json') -Encoding utf8NoBOM
[IO.Compression.ZipFile]::CreateFromDirectory($taskDestination, $taskArchive)
$taskDigest = (Get-FileHash -LiteralPath $taskArchive -Algorithm SHA256).Hash.ToLowerInvariant()
"$taskDigest  $([IO.Path]::GetFileName($taskArchive))" | Set-Content -LiteralPath "$taskArchive.sha256" -Encoding utf8NoBOM
Write-Output "Release package: $taskArchive"
