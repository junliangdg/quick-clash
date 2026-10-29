#Requires -Version 7.2
[CmdletBinding(SupportsShouldProcess, ConfirmImpact = 'High')]
param(
    [string]$PackageDirectory = $PSScriptRoot,
    [switch]$Uninstall,
    [switch]$ValidateOnly
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'service_package.ps1')
$taskAdministrator = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($ValidateOnly) {
    $taskPackage = Open-ServicePackage $PackageDirectory
    try { Write-Output "Package verified: $($taskPackage.Files.Count - 1) locked files; $($taskPackage.Bytes) bytes. No service or system changes." } finally { Close-ServicePackage $taskPackage }
    return
}
if (-not $taskAdministrator) { throw 'Authorization required: run this reviewed script from a PowerShell 7 administrator terminal. The GUI remains an ordinary-user process. Use -ValidateOnly to review the package without elevation.' }
$taskInstallRoot = Join-Path $env:ProgramFiles 'QuickClash'
$taskServiceName = 'QuickClashService'
if ($Uninstall) {
    if ($PSCmdlet.ShouldProcess($taskServiceName, 'Stop and unregister QuickClash TUN service; preserve installed files and user data')) {
        $taskExistingService = Get-Service -Name $taskServiceName -ErrorAction SilentlyContinue
        if ($taskExistingService) { Stop-Service -Name $taskServiceName -ErrorAction Stop; & sc.exe delete $taskServiceName; if ($LASTEXITCODE) { throw 'Service removal failed.' } }
    }
    return
}
if (Test-Path -LiteralPath $taskInstallRoot) { throw 'Install directory already exists. Stop and explicitly move or remove the old installation before installing this build.' }
$taskServiceRoot = Join-Path $env:ProgramData 'QuickClash'
if (Test-Path -LiteralPath $taskServiceRoot) { throw 'Service data directory already exists; review its ownership before installing.' }
if (-not $PSCmdlet.ShouldProcess($taskInstallRoot, 'Install reviewed local package and register LocalSystem TUN service')) { return }
$taskPackage = Open-ServicePackage $PackageDirectory
try {
    New-Item -ItemType Directory -Path $taskInstallRoot | Out-Null
    New-Item -ItemType Directory -Path $taskServiceRoot | Out-Null
    & icacls.exe $taskServiceRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' | Out-Null
    if ($LASTEXITCODE) { throw 'Service data ACL failed.' }
    & icacls.exe $taskInstallRoot /inheritance:r /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' '*S-1-5-32-545:(OI)(CI)RX' | Out-Null
    if ($LASTEXITCODE) { throw 'Installation ACL failed.' }
    Copy-ServicePackage $taskPackage $taskInstallRoot
    New-Service -Name $taskServiceName -BinaryPathName ('"' + (Join-Path $taskInstallRoot 'bin/quickclash_service.exe') + '"') -DisplayName 'QuickClash TUN Service' -StartupType Manual | Out-Null
    & sc.exe sdset $taskServiceName 'D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;CCLCRC;;;BU)' | Out-Null
    if ($LASTEXITCODE) { throw 'Service control ACL failed.' }
    Start-Service -Name $taskServiceName
    Write-Output 'Service installed. Run the GUI from Program Files/QuickClash/bin as an ordinary user. System proxy lease recovery remains in the user session.'
} catch {
    # Preserve reviewed files and failure evidence; never recursively delete a computed install path.
    Write-Warning 'Installation incomplete. Protected files are retained. Review the service status and use -Uninstall before explicitly archiving these fixed installation/data directories.'
    throw
} finally { Close-ServicePackage $taskPackage }
