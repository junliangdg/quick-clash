#Requires -Version 7.2
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build/p1_windows',
    [string]$Destination = 'dist/p2_windows',
    [string]$QtRoot = 'C:/Qt/6.8.0/msvc2022_64',
    [string]$VcRuntimeDirectory = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Redist/MSVC/14.50.35710/x64/Microsoft.VC145.CRT',
    [string]$MihomoLicense = "$env:TEMP/quickclash_mihomo_license.txt",
    [string]$CMake = 'cmake'
)
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'Windows x64 local validation package only.' }
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$taskDestination = [IO.Path]::GetFullPath($Destination, $taskRepo)
if (Test-Path -LiteralPath $taskDestination) { throw 'Use a new destination to preserve prior evidence.' }
$taskMatrixPath = Join-Path $taskRepo 'docs/baseline/release_matrix.json'
$taskMatrix = Get-Content -LiteralPath $taskMatrixPath -Raw | ConvertFrom-Json
$taskCore = Join-Path $taskRepo '.cache/mihomo/mihomo.exe'
if ((Get-FileHash -LiteralPath $taskCore -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskMatrix.mihomo.windows.executable_sha256) { throw 'Official Mihomo hash mismatch.' }
if (-not (Test-Path -LiteralPath $MihomoLicense)) { throw 'Provide the frozen official Mihomo license text.' }
& (Join-Path $PSScriptRoot 'deploy_windows.ps1') -BuildDirectory $BuildDirectory -Destination $taskDestination -CMake $CMake
New-Item -ItemType Directory -Path (Join-Path $taskDestination 'core'), (Join-Path $taskDestination 'licenses'), (Join-Path $taskDestination 'metadata') | Out-Null
Copy-Item -LiteralPath $taskCore -Destination (Join-Path $taskDestination 'core/mihomo.exe')
New-Item -ItemType Directory -Path (Join-Path $taskDestination 'geo') | Out-Null
foreach ($taskAsset in @($taskMatrix.geo.geoip, $taskMatrix.geo.geosite)) {
    $taskAssetPath = Join-Path $taskRepo ('.cache/geo/' + $taskAsset.file)
    if ((Get-Item -LiteralPath $taskAssetPath).Length -ne $taskAsset.bytes -or
        (Get-FileHash -LiteralPath $taskAssetPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskAsset.sha256) { throw 'Geo snapshot size/hash mismatch.' }
    Copy-Item -LiteralPath $taskAssetPath -Destination (Join-Path $taskDestination ('geo/' + $taskAsset.file))
}
$taskGeoLicense = Join-Path $taskRepo '.cache/geo/license.txt'
if ((Get-FileHash -LiteralPath $taskGeoLicense -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskMatrix.geo.license_sha256) { throw 'Geo license hash mismatch.' }
Copy-Item -LiteralPath $taskGeoLicense -Destination (Join-Path $taskDestination 'licenses/meta_rules_dat_gpl_3.txt')
if (-not (Test-Path -LiteralPath $VcRuntimeDirectory)) { throw 'Provide the Microsoft redistributable x64 CRT directory.' }
foreach ($taskDll in Get-ChildItem -LiteralPath $VcRuntimeDirectory -Filter '*.dll' -File) {
    Copy-Item -LiteralPath $taskDll.FullName -Destination (Join-Path $taskDestination 'bin')
}
Copy-Item -LiteralPath $MihomoLicense -Destination (Join-Path $taskDestination 'licenses/mihomo_gpl_3.txt')
$taskYamlLicense = Join-Path ([IO.Path]::GetFullPath($BuildDirectory, $taskRepo)) '_deps/yaml_cpp-src/LICENSE'
Copy-Item -LiteralPath $taskYamlLicense -Destination (Join-Path $taskDestination 'licenses/yaml_cpp_mit.txt')
$taskQtLicenses = Join-Path (Split-Path (Split-Path $QtRoot)) 'Licenses'
foreach ($taskLicense in Get-ChildItem -LiteralPath $taskQtLicenses -File) {
    Copy-Item -LiteralPath $taskLicense.FullName -Destination (Join-Path $taskDestination ('licenses/qt_' + $taskLicense.Name.ToLowerInvariant()))
}
Copy-Item -LiteralPath $taskMatrixPath -Destination (Join-Path $taskDestination 'metadata/release_matrix.json')
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs/baseline/config_policy.json') -Destination (Join-Path $taskDestination 'metadata/config_policy.json')
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs/baseline/windows_user_guide.md') -Destination (Join-Path $taskDestination 'readme.md')
Copy-Item -LiteralPath (Join-Path $taskRepo 'scripts/install_service_windows.ps1') -Destination (Join-Path $taskDestination 'install_service_windows.ps1')
Copy-Item -LiteralPath (Join-Path $taskRepo 'scripts/service_package.ps1') -Destination $taskDestination
Copy-Item -LiteralPath (Join-Path $taskRepo 'docs/baseline/licenses.md') -Destination (Join-Path $taskDestination 'licenses/source_obligations.md')
$taskFiles = @(Get-ChildItem -LiteralPath $taskDestination -File -Recurse | ForEach-Object {
    [ordered]@{ path = [IO.Path]::GetRelativePath($taskDestination, $_.FullName).Replace('\', '/'); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
[ordered]@{
    schema_version = 1; created_at_utc = [DateTime]::UtcNow.ToString('o')
    purpose = 'Private Windows local validation; not a signed public release or clean-machine acceptance'
    official_core = $taskMatrix.mihomo.windows.url; files = $taskFiles
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskDestination 'metadata/package_manifest.json') -Encoding utf8NoBOM
Write-Output "Offline validation package: $taskDestination. Clean ordinary-user execution and public-distribution source obligations remain separate checks."
