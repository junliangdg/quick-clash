#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$runRoot = Join-Path $repoRoot ('.cache/dependency_fetch_test_' + [guid]::NewGuid().ToString('N'))
$fixtureRoot = Join-Path $runRoot 'fixture repo'
$fixtureScript = Join-Path $fixtureRoot 'scripts/fetch_dependencies.ps1'
$assetPath = Join-Path $fixtureRoot '.cache/yaml_fixture.tar.gz'
$assetUrl = 'https://fixture.invalid/pinned/yaml_fixture.tar.gz'
$payload = [Text.Encoding]::UTF8.GetBytes('verified fixture archive')
$expectedHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($payload)).ToLowerInvariant()
$downloadState = [pscustomobject]@{ Count = 0; Mode = 'valid' }
$script:passed = 0

function Require($Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Expect-Failure([scriptblock]$Action) {
    $message = $null
    try { & $Action | Out-Null } catch { $message = $_.Exception.Message }
    Require ($null -ne $message) 'Expected an error, but the operation succeeded.'
    return $message
}
function Invoke-Case([string]$Name, [scriptblock]$Action) {
    foreach ($path in @($assetPath, "$assetPath.partial")) {
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
    $downloadState.Count = 0
    $downloadState.Mode = 'valid'
    & $Action
    $script:passed++
    Write-Output "PASS: $Name"
}
# Shadow only the network command; exercise the real script with an isolated repository fixture.
function Invoke-WebRequest {
    param([string]$Uri, [string]$OutFile, [int]$TimeoutSec)
    Require ($Uri -ceq $assetUrl -and $TimeoutSec -eq 180) 'Unexpected download request.'
    $downloadState.Count++
    $bytes = $payload
    switch ($downloadState.Mode) {
        'oversized' { $bytes = [byte[]]::new(4000001) }
        'corrupt' { $bytes = [Text.Encoding]::UTF8.GetBytes('untrusted bytes') }
    }
    [IO.File]::WriteAllBytes($OutFile, $bytes)
    if ($downloadState.Mode -eq 'interrupted') { throw 'simulated network interruption' }
}

try {
    foreach ($directory in @('scripts', 'docs/baseline', '.cache')) {
        $null = New-Item -ItemType Directory -Path (Join-Path $fixtureRoot $directory) -Force
    }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'scripts/fetch_dependencies.ps1') -Destination $fixtureScript
    # No core or Geo fields: -YamlOnly must not inspect or fetch runtime resources.
    @{toolchain = @{yaml_cpp = @{version = 'fixture'; cache_file = 'yaml_fixture.tar.gz'; url = $assetUrl; sha256 = $expectedHash}}} |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $fixtureRoot 'docs/baseline/release_matrix.json') -Encoding utf8

    Invoke-Case 'verified download replaces stale partial only after validation' {
        [IO.File]::WriteAllText("$assetPath.partial", 'old partial')
        & $fixtureScript -YamlOnly | Out-Null
        Require ($downloadState.Count -eq 1) 'Expected exactly one download.'
        Require ((Get-FileHash -LiteralPath $assetPath).Hash -ieq $expectedHash) 'Published cache hash differs.'
        Require (-not (Test-Path -LiteralPath "$assetPath.partial")) 'Successful download left a partial file.'
    }
    Invoke-Case 'offline cache reuse does not download or rewrite valid data' {
        [IO.File]::WriteAllBytes($assetPath, $payload)
        $stamp = (Get-Item -LiteralPath $assetPath).LastWriteTimeUtc
        & $fixtureScript -YamlOnly -Offline | Out-Null
        Require ($downloadState.Count -eq 0) 'Offline cache reuse made a network request.'
        Require ((Get-Item -LiteralPath $assetPath).LastWriteTimeUtc -eq $stamp) 'Valid cache was rewritten.'
    }
    Invoke-Case 'online cache reuse does not download' {
        [IO.File]::WriteAllBytes($assetPath, $payload)
        & $fixtureScript -YamlOnly | Out-Null
        Require ($downloadState.Count -eq 0) 'Valid cache was downloaded again.'
    }
    Invoke-Case 'offline missing asset fails with path and recovery guidance' {
        $message = Expect-Failure { & $fixtureScript -YamlOnly -Offline }
        Require ($message.Contains('Offline asset missing') -and $message.Contains($assetPath) -and
                 $message.Contains('run without -Offline')) 'Offline error lacks actionable context.'
        Require ($downloadState.Count -eq 0) 'Offline missing asset made a network request.'
    }
    Invoke-Case 'download hash mismatch reports expected and actual hashes and cleans partial' {
        $downloadState.Mode = 'corrupt'
        $message = Expect-Failure { & $fixtureScript -YamlOnly }
        $actual = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes('untrusted bytes'))).ToLowerInvariant()
        Require ($message.Contains('SHA256 mismatch') -and $message.Contains($assetPath) -and
                 $message.Contains($assetUrl) -and $message.Contains("Expected: $expectedHash") -and
                 $message.Contains("Actual: $actual")) 'Hash error lacks verification details.'
        Require (-not (Test-Path -LiteralPath $assetPath)) 'Unverified bytes were published.'
        Require (-not (Test-Path -LiteralPath "$assetPath.partial")) 'Hash failure left a partial file.'
    }
    Invoke-Case 'bad existing cache is rejected without replacement' {
        [IO.File]::WriteAllText($assetPath, 'existing bad cache')
        $message = Expect-Failure { & $fixtureScript -YamlOnly }
        Require ($message.Contains('SHA256 mismatch')) 'Bad cache was accepted.'
        Require ($downloadState.Count -eq 0 -and [IO.File]::ReadAllText($assetPath) -ceq 'existing bad cache') 'Bad cache was silently replaced.'
    }
    Invoke-Case 'offline corrupt cache is rejected without network access' {
        [IO.File]::WriteAllText($assetPath, 'corrupt offline cache')
        $message = Expect-Failure { & $fixtureScript -YamlOnly -Offline }
        Require ($message.Contains('SHA256 mismatch') -and $downloadState.Count -eq 0) 'Offline verification was bypassed.'
    }
    Invoke-Case 'oversized download is rejected and cleaned' {
        $downloadState.Mode = 'oversized'
        $message = Expect-Failure { & $fixtureScript -YamlOnly }
        Require ($message.Contains('Actual bytes: 4000001; maximum bytes: 4000000')) 'Size error lacks limits.'
        Require (-not (Test-Path -LiteralPath $assetPath) -and -not (Test-Path -LiteralPath "$assetPath.partial")) 'Oversized download survived.'
    }
    Invoke-Case 'oversized existing cache is rejected without replacement' {
        [IO.File]::WriteAllBytes($assetPath, [byte[]]::new(4000001))
        $message = Expect-Failure { & $fixtureScript -YamlOnly }
        Require ($message.Contains('Asset exceeds size limit') -and $downloadState.Count -eq 0) 'Oversized cache was accepted or replaced.'
    }
    Invoke-Case 'interrupted network transfer preserves error and cleans partial' {
        $downloadState.Mode = 'interrupted'
        $message = Expect-Failure { & $fixtureScript -YamlOnly }
        Require ($message.Contains('simulated network interruption')) 'Network error was lost.'
        Require (-not (Test-Path -LiteralPath $assetPath) -and -not (Test-Path -LiteralPath "$assetPath.partial")) 'Interrupted download survived.'
    }
    Invoke-Case 'production Geo and license sources use full pinned commits' {
        $matrix = Get-Content -LiteralPath (Join-Path $repoRoot 'docs/baseline/release_matrix.json') -Raw | ConvertFrom-Json
        Require ($matrix.geo.revision -cmatch '^[0-9a-f]{40}$') 'Geo revision is not a full commit.'
        Require ($matrix.geo.license_revision -cmatch '^[0-9a-f]{40}$') 'License revision is not a full commit.'
        $base = 'https://raw.githubusercontent.com/MetaCubeX/meta-rules-dat/'
        foreach ($asset in @($matrix.geo.geoip, $matrix.geo.geosite)) {
            Require ($asset.url -ceq ($base + $matrix.geo.revision + '/' + $asset.file)) 'Geo download URL is not pinned to the recorded commit.'
        }
        Require ($matrix.geo.license_url -ceq ($base + $matrix.geo.license_revision + '/LICENSE')) 'License URL is not pinned to the recorded commit.'
    }
    Write-Output "PASS: $script:passed dependency fetch cases; no external network or product build."
} finally {
    $parent = [IO.Path]::GetFullPath((Join-Path $repoRoot '.cache')) + [IO.Path]::DirectorySeparatorChar
    if ([IO.Path]::GetFullPath($runRoot).StartsWith($parent, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $runRoot)) {
        Remove-Item -LiteralPath $runRoot -Recurse -Force
    }
}
