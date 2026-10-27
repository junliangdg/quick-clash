#Requires -Version 7.2
[CmdletBinding()]
param([switch]$Offline, [switch]$YamlOnly)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$matrix = Get-Content (Join-Path $repoRoot 'docs/baseline/release_matrix.json') -Raw | ConvertFrom-Json
$cacheRoot = Join-Path $repoRoot '.cache'
New-Item $cacheRoot -ItemType Directory -Force | Out-Null
function Assert-VerifiedAsset([string]$Url, [string]$Path, [string]$Hash, [long]$Limit) {
    $size = (Get-Item -LiteralPath $Path).Length
    if ($size -gt $Limit) {
        throw "Asset exceeds size limit: $Path. Source: $Url. Actual bytes: $size; maximum bytes: $Limit."
    }
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ine $Hash) {
        throw @"
SHA256 mismatch: $Path
Source: $Url
Expected: $Hash
Actual: $actual
The file does not match the pinned release_matrix.json snapshot. The upstream asset may have changed, or the response may be damaged. Restore trusted matching bytes; do not disable verification or automatically accept a new hash.
"@
    }
}
function Get-VerifiedAsset([string]$Url, [string]$Path, [string]$Hash, [long]$Limit) {
    if (Test-Path -LiteralPath $Path) {
        # Never overwrite an existing cache entry, even if it fails validation.
        Assert-VerifiedAsset $Url $Path $Hash $Limit
        return
    }
    if ($Offline) {
        throw "Offline asset missing: $Path. Restore the pinned file or run without -Offline. Source: $Url"
    }
    $partial = "$Path.partial"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $partial -TimeoutSec 180
        Assert-VerifiedAsset $Url $partial $Hash $Limit
        Move-Item -LiteralPath $partial -Destination $Path -Force
    } catch {
        throw "Failed to fetch verified asset '$Path'. Source: $Url. $($_.Exception.Message)"
    } finally {
        if (Test-Path -LiteralPath $partial -PathType Leaf) {
            Remove-Item -LiteralPath $partial -Force
        }
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
if ($matrix.mihomo.license_sha256) {
    Get-VerifiedAsset $matrix.mihomo.license_url (Join-Path $coreRoot 'license.txt') $matrix.mihomo.license_sha256 40000
}
$geoRoot = Join-Path $cacheRoot 'geo'
New-Item $geoRoot -ItemType Directory -Force | Out-Null
foreach ($asset in @($matrix.geo.geoip, $matrix.geo.geosite)) {
    Get-VerifiedAsset $asset.url (Join-Path $geoRoot $asset.file) $asset.sha256 $asset.bytes
}
Get-VerifiedAsset $matrix.geo.license_url (Join-Path $geoRoot 'license.txt') $matrix.geo.license_sha256 40000
Write-Output "Verified bundled Geo snapshot $($matrix.geo.snapshot)."
