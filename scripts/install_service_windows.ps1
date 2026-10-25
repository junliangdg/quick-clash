#Requires -Version 7.2
#Requires -RunAsAdministrator
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [Parameter(Mandatory)][string]$PackageDirectory,
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$taskInstallRoot = Join-Path $env:ProgramFiles 'QuickClash'
$taskServiceName = 'QuickClashService'
if ($Uninstall) {
    if ($PSCmdlet.ShouldProcess($taskServiceName, 'Stop and unregister QuickClash TUN service; preserve installed files and user data')) {
        $taskExistingService = Get-Service -Name $taskServiceName -ErrorAction SilentlyContinue
        if ($taskExistingService) { Stop-Service -Name $taskServiceName -ErrorAction Stop; & sc.exe delete $taskServiceName; if ($LASTEXITCODE) { throw 'Service removal failed.' } }
    }
    return
}
$taskSourceRoot = (Resolve-Path -LiteralPath $PackageDirectory).Path
$taskManifest = Get-Content -LiteralPath (Join-Path $taskSourceRoot 'metadata/package_manifest.json') -Raw | ConvertFrom-Json
foreach ($taskEntry in $taskManifest.files) {
    $taskCandidate = [IO.Path]::GetFullPath([IO.Path]::Combine($taskSourceRoot, $taskEntry.path))
    if (-not $taskCandidate.StartsWith($taskSourceRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe package path.' }
    if ((Get-Item -LiteralPath $taskCandidate).LinkType -or (Get-FileHash -LiteralPath $taskCandidate -Algorithm SHA256).Hash.ToLowerInvariant() -ne $taskEntry.sha256) { throw 'Package integrity check failed.' }
}
if (-not (Test-Path -LiteralPath (Join-Path $taskSourceRoot 'bin/quickclash_service.exe'))) { throw 'Package lacks the service executable.' }
if (Test-Path -LiteralPath $taskInstallRoot) { throw 'Install directory already exists. Stop and explicitly move or remove the old installation before installing this build.' }
$taskServiceRoot = Join-Path $env:ProgramData 'QuickClash'
if (Test-Path -LiteralPath $taskServiceRoot) { throw 'Service data directory already exists; review its ownership before installing.' }
if (-not $PSCmdlet.ShouldProcess($taskInstallRoot, 'Install reviewed local package and register LocalSystem TUN service')) { return }
New-Item -ItemType Directory -Path $taskInstallRoot | Out-Null
New-Item -ItemType Directory -Path $taskServiceRoot | Out-Null
& icacls.exe $taskServiceRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
if ($LASTEXITCODE) { throw 'Service data ACL failed.' }
& icacls.exe $taskInstallRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
if ($LASTEXITCODE) { throw 'Installation ACL failed.' }
Copy-Item -LiteralPath (Join-Path $taskSourceRoot 'bin'), (Join-Path $taskSourceRoot 'core'), (Join-Path $taskSourceRoot 'geo'), (Join-Path $taskSourceRoot 'metadata'), (Join-Path $taskSourceRoot 'licenses') -Destination $taskInstallRoot -Recurse
New-Service -Name $taskServiceName -BinaryPathName ('"' + (Join-Path $taskInstallRoot 'bin/quickclash_service.exe') + '"') -DisplayName 'QuickClash TUN Service' -StartupType Manual | Out-Null
Start-Service -Name $taskServiceName
Write-Output 'Service installed. Run the GUI from Program Files/QuickClash/bin as an ordinary user. System proxy lease recovery remains in the user session.'
