[CmdletBinding()]
param([string]$SourceRoot = '')
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $SourceRoot = Join-Path $PSScriptRoot '..'
}
$root = (Resolve-Path -LiteralPath $SourceRoot).Path
$issues = [System.Collections.Generic.List[string]]::new()
$shared = @('src/domain', 'src/application', 'src/presentation')
$sdk = '^\s*#\s*include\s*[<"](?:windows[.]h|winsock2?[.]h|wininet[.]h|wincrypt[.]h|iphlpapi[.]h|userenv[.]h|psapi[.]h|aclapi[.]h|sddl[.]h|platform/windows/)'
foreach ($folder in $shared) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root $folder) -Recurse -File | Where-Object Extension -in '.cpp', '.h') {
        foreach ($match in Select-String -LiteralPath $file.FullName -Pattern $sdk) {
            $issues.Add("$($file.FullName):$($match.LineNumber): shared code includes a Windows adapter/SDK")
        }
    }
}
# Infra owns platform IO; its public headers must still be usable by a replacement host.
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'src/infra') -Recurse -File -Filter '*.h') {
    foreach ($match in Select-String -LiteralPath $file.FullName -Pattern $sdk) {
        $issues.Add("$($file.FullName):$($match.LineNumber): public infra header exposes a Windows adapter/SDK")
    }
}
# Page factories are shared by sidebar/bottom-bar/embedded projections.
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $root 'qml/features') -Recurse -File -Filter '*.qml') {
    foreach ($match in Select-String -LiteralPath $file.FullName -Pattern '\b(DesktopSidebar|MobileBottomBar|StackView)\b') {
        $issues.Add("$($file.FullName):$($match.LineNumber): feature depends on a navigation shell")
    }
}
if ($issues.Count) {
    $issues | ForEach-Object { Write-Output "ERROR: $_" }
    exit 1
}
Write-Output 'Shared platform include and feature-shell boundaries passed (static check only).'
