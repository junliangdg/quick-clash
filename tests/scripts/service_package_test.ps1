#Requires -Version 7.2
$ErrorActionPreference = 'Stop'
$taskRepo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $taskRepo 'scripts/service_package.ps1')
$taskRoot = Join-Path $taskRepo ('build/script_tests/service_package/' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $taskRoot
$taskCase = 0
function Require-Failure([scriptblock]$Action) {
    $taskFailed = $false
    try { & $Action } catch { $taskFailed = $true }
    if (-not $taskFailed) { throw 'Expected refusal.' }
}
function New-Fixture {
    $script:taskCase++
    $taskDirectory = Join-Path $taskRoot "case_$script:taskCase"
    foreach ($taskRelative in 'bin/quickclash.exe', 'bin/quickclash_service.exe', 'bin/quickclash_network.exe', 'core/mihomo.exe', 'bin/Qt6Core.dll', 'metadata/config_policy.json') {
        $taskPath = Join-Path $taskDirectory $taskRelative
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskPath))
        [IO.File]::WriteAllText($taskPath, "fixture:$taskRelative")
    }
    & (Join-Path $taskRepo 'scripts/copy_windows_readmes.ps1') -Destination $taskDirectory
    $taskEntries = @(Get-ChildItem -LiteralPath $taskDirectory -File -Recurse | ForEach-Object {
        @{ path = [IO.Path]::GetRelativePath($taskDirectory, $_.FullName).Replace([char]92, [char]47); bytes = $_.Length; sha256 = (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() }
    })
    @{schema_version = 1; files = $taskEntries} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskDirectory 'metadata/package_manifest.json')
    return $taskDirectory
}
$taskDirectory = New-Fixture
$taskPackage = Open-ServicePackage $taskDirectory
try {
    Require-Failure { [IO.File]::WriteAllText((Join-Path $taskDirectory 'bin/quickclash_service.exe'), 'replacement') }
    $taskDestination = Join-Path $taskRoot 'copied'
    Copy-ServicePackage $taskPackage $taskDestination
    if ([IO.File]::ReadAllText((Join-Path $taskDestination 'bin/quickclash_service.exe')) -cne 'fixture:bin/quickclash_service.exe') { throw 'Original locked bytes not copied.' }
    if ([IO.File]::ReadAllText((Join-Path $taskDestination 'bin/quickclash_network.exe')) -cne 'fixture:bin/quickclash_network.exe') { throw 'Download transport not copied.' }
    foreach ($taskReadme in 'readme.md', 'readme_zh_cn.md') {
        if (Test-Path -LiteralPath (Join-Path $taskDestination $taskReadme)) { throw 'Root documentation must not be installed as service payload.' }
    }
} finally { Close-ServicePackage $taskPackage }
Write-Output 'PASS: bilingual package accepted; locked source copy and digest verification; root documentation excluded'
foreach ($taskKind in 'unlisted', 'corrupt', 'duplicate', 'escape', 'missing', 'missing_transport', 'schema') {
    $taskDirectory = New-Fixture
    $taskManifestPath = Join-Path $taskDirectory 'metadata/package_manifest.json'
    $taskManifest = Get-Content -LiteralPath $taskManifestPath -Raw | ConvertFrom-Json
    switch ($taskKind) {
        'unlisted' { [IO.File]::WriteAllText((Join-Path $taskDirectory 'bin/extra.dll'), 'injected') }
        'corrupt' { [IO.File]::WriteAllText((Join-Path $taskDirectory 'core/mihomo.exe'), 'wrong') }
        'duplicate' { $taskManifest.files += @($taskManifest.files | Where-Object path -EQ 'bin/quickclash.exe')[0] }
        'escape' { @($taskManifest.files | Where-Object path -EQ 'bin/quickclash.exe')[0].path = 'bin/../../outside.exe' }
        'missing' { $taskManifest.files = @($taskManifest.files | Where-Object path -ne 'bin/quickclash_service.exe') }
        'missing_transport' { $taskManifest.files = @($taskManifest.files | Where-Object path -ne 'bin/quickclash_network.exe') }
        'schema' { $taskManifest.schema_version = 99 }
    }
    $taskManifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $taskManifestPath
    Require-Failure { $taskUnexpected = Open-ServicePackage $taskDirectory; Close-ServicePackage $taskUnexpected }
    Write-Output "PASS: $taskKind package rejected"
}
$taskDirectory = New-Fixture
$taskLink = Join-Path $taskRoot 'linked_package'
$null = New-Item -ItemType Junction -Path $taskLink -Target $taskDirectory
Require-Failure { $taskUnexpected = Open-ServicePackage $taskLink; Close-ServicePackage $taskUnexpected }
Write-Output 'PASS: junction package root rejected'
Write-Output "Evidence retained in $taskRoot; no administrator or system changes."
