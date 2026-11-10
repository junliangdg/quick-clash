#Requires -Version 7.2
# Shared isolated Git fixture; no changes to the checkout or remote repositories.
function Invoke-FixtureGit([string]$Root, [string[]]$Arguments) {
    $output = @(& git -C $Root @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "Fixture Git failed: $($Arguments -join ' ')" }
    $output
}
function Add-FixtureCommit([string]$Root, [string]$Path, [string]$Message) {
    $file = Join-Path $Root $Path
    $null = New-Item -ItemType Directory -Path (Split-Path $file) -Force
    # Qt's configure-time QML scanner can discover ignored fixture directories.
    # Keep the path-filter fixture valid QML so it adds no product-build warning.
    $content = if ($Path.EndsWith('.qml', [StringComparison]::Ordinal)) { "import QtQuick`nItem {} // $Message`n" } else { "$Message`n" }
    [IO.File]::AppendAllText($file, $content, [Text.UTF8Encoding]::new($false))
    $null = Invoke-FixtureGit $Root @('add', '--', $Path)
    $null = Invoke-FixtureGit $Root @('-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', $Message)
    (Invoke-FixtureGit $Root @('rev-parse', 'HEAD')).Trim()
}
function New-ReleaseNotesFixture([string]$Root) {
    $null = New-Item -ItemType Directory -Path $Root -Force
    $null = Invoke-FixtureGit $Root @('init', '--quiet', '--initial-branch=main')
    $null = Invoke-FixtureGit $Root @('config', 'user.name', 'Release Fixture')
    $null = Invoke-FixtureGit $Root @('config', 'user.email', 'fixture@example.invalid')
    $null = Invoke-FixtureGit $Root @('config', 'core.autocrlf', 'false')
    $null = Invoke-FixtureGit $Root @('config', 'core.excludesFile', '/dev/null')
    $base = Add-FixtureCommit $Root 'src/product.cpp' 'feat: initial product'
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.0.0')
    $null = Add-FixtureCommit $Root 'docs/guide.md' 'feat: documentation only'
    $null = Add-FixtureCommit $Root 'tests/unit/product_test.cpp' 'fix: test fixture only'
    $null = Add-FixtureCommit $Root '.github/workflows/build.yml' 'feat: workflow only'
    $null = Add-FixtureCommit $Root 'scripts/tool.ps1' 'fix: repository tooling only'
    $null = Add-FixtureCommit $Root 'app/host_probe.cpp' 'feat: test probe only'
    $null = Add-FixtureCommit $Root 'resources/i18n/readme.md' 'feat: resource documentation only'
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'docs: maintenance type with product path'
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'feat(repo): repository scope with product path'
    $feature = Add-FixtureCommit $Root 'src/product.cpp' 'feat(subscription): 批量更新 & <safe> @someone [link](bad)'
    $null = Add-FixtureCommit $Root 'qml/main.qml' 'fix(ui): retain selection'
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'perf: reduce memory'
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'refactor: simplify product state'
    $null = Add-FixtureCommit $Root 'resources/i18n/product_zh_cn.ts' 'Improve product translation'
    $null = Add-FixtureCommit $Root 'docs/baseline/release_matrix.json' 'fix(core): update runtime dependency'
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'fix(api)!: change configuration format'
    $null = Add-FixtureCommit $Root 'src/product.cpp' "feat: new storage`n`nBREAKING CHANGE: migrate saved profiles"
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.1.0-rc.2')
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'fix: release candidate correction'
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', '-a', 'v1.1.0-rc.10', '-m', 'annotated prerelease')
    $current = Add-FixtureCommit $Root 'src/product.cpp' 'feat: final release capability'
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', '-a', 'v1.1.0', '-m', 'annotated release')
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.1.0-rc.11')
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.0.9')
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.0.8')
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', 'continuous')
    $null = Invoke-FixtureGit $Root @('checkout', '--quiet', '-b', 'unrelated', $base)
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'feat: unrelated branch'
    $null = Invoke-FixtureGit $Root @('-c', 'tag.gpgsign=false', 'tag', 'v1.0.7')
    $null = Invoke-FixtureGit $Root @('checkout', '--quiet', 'main')
    # The current tag must bound the range even when HEAD has moved ahead.
    $null = Add-FixtureCommit $Root 'src/product.cpp' 'feat: future unreleased change'
    [pscustomobject]@{ Root = $Root; Base = $base; Commit = $current; Feature = $feature }
}
function New-FixtureRelease([string]$Tag, [bool]$Prerelease = $false, [bool]$Draft = $false, [string]$Body = '') {
    [pscustomobject]@{ tag_name = $Tag; draft = $Draft; prerelease = $Prerelease; body = $Body }
}
