[CmdletBinding()]
param([switch]$Offline, [switch]$YamlOnly)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$matrix = Get-Content (Join-Path $repoRoot 'docs/baseline/release_matrix.json') -Raw | ConvertFrom-Json
$cacheRoot = Join-Path $repoRoot '.cache'
New-Item $cacheRoot -ItemType Directory -Force | Out-Null
function Get-VerifiedAsset($Url, $Path, $Hash, $Limit) {
    if (-not (Test-Path -LiteralPath $Path)) {
        if ($Offline) { throw "Offline asset missing: $Path" }
        $partial = "$Path.partial"
        Invoke-WebRequest -Uri $Url -OutFile $partial -TimeoutSec 180
        if ((Get-Item -LiteralPath $partial).Length -gt $Limit) { throw "Asset exceeds size limit: $Url" }
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ine $Hash) { throw "SHA256 mismatch: $Url" }
        Move-Item -LiteralPath $partial -Destination $Path -Force
    }
    if ((Get-Item -LiteralPath $Path).Length -gt $Limit -or (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Hash) {
        throw "Cached asset size/hash mismatch: $Path"
    }
}
$yaml = $matrix.toolchain.yaml_cpp
Get-VerifiedAsset $yaml.url (Join-Path $cacheRoot $yaml.cache_file) $yaml.sha256 4000000
Write-Output "Verified yaml-cpp $($yaml.version) archive."
if ($YamlOnly) { return }
$core = $matrix.mihomo.windows
$archivePath = Join-Path $cacheRoot $core.asset
Get-VerifiedAsset $core.url $archivePath $core.archive_sha256 $core.max_archive_bytes
$coreRoot = Join-Path $cacheRoot 'mihomo'
New-Item $coreRoot -ItemType Directory -Force | Out-Null
# Read only the single expected executable, never extract arbitrary upstream paths.
$archive = [System.IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    $entries = @($archive.Entries | Where-Object { $_.Name -like 'mihomo*.exe' })
    if ($entries.Count -ne 1 -or $entries[0].Length -gt $core.max_executable_bytes) { throw 'Unexpected core archive layout/size' }
    $exePath = Join-Path $coreRoot 'mihomo.exe'
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $exePath, $true)
} finally { $archive.Dispose() }
$exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($core.executable_sha256 -and $exeHash -cne $core.executable_sha256) { throw 'Core executable hash mismatch' }
Write-Output "Verified Mihomo $($matrix.mihomo.tag): $exeHash"
Write-Output "Core path: $exePath"
Write-Output 'No core source is built.'
$geoRoot = Join-Path $cacheRoot 'geo'
New-Item $geoRoot -ItemType Directory -Force | Out-Null
foreach ($asset in @($matrix.geo.geoip, $matrix.geo.geosite)) {
    Get-VerifiedAsset $asset.url (Join-Path $geoRoot $asset.file) $asset.sha256 $asset.bytes
}
Get-VerifiedAsset $matrix.geo.license_url (Join-Path $geoRoot 'license.txt') $matrix.geo.license_sha256 40000
Write-Output "Verified bundled Geo snapshot $($matrix.geo.snapshot)."
