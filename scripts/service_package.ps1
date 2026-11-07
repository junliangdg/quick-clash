#Requires -Version 5.1
# Shared by the no-elevation validation kit and the package integrity tests.
function Get-StreamSha256Hex([System.IO.Stream]$Stream) {
    $taskHasher = [Security.Cryptography.SHA256]::Create()
    try {
        $taskBytes = $taskHasher.ComputeHash($Stream)
        return (-join ($taskBytes | ForEach-Object { $_.ToString('x2') }))
    } finally { $taskHasher.Dispose() }
}
function Get-PackageRelativePath([string]$Root, [string]$FullPath) {
    $taskPrefix = $Root.TrimEnd([char[]]"\/") + [IO.Path]::DirectorySeparatorChar
    if (-not $FullPath.StartsWith($taskPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Package path escape.' }
    return $FullPath.Substring($taskPrefix.Length).Replace([char]92, [char]47)
}
function Assert-ServicePackagePath([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { throw "Package path not found: $Path" }
    $taskAncestor = [IO.Path]::GetFullPath($Path)
    while ($taskAncestor) {
        $taskInfo = Get-Item -LiteralPath $taskAncestor -Force -ErrorAction Stop
        if ($taskInfo.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Package reparse point rejected.' }
        $taskParent = [IO.Directory]::GetParent($taskAncestor)
        $taskAncestor = if ($taskParent) { $taskParent.FullName } else { $null }
    }
}
function Open-ServicePackage([string]$Directory) {
    $taskRoot = (Resolve-Path -LiteralPath $Directory -ErrorAction Stop).Path
    Assert-ServicePackagePath $taskRoot
    $taskManifestPath = Join-Path $taskRoot 'metadata/package_manifest.json'
    if (-not (Test-Path -LiteralPath $taskManifestPath -PathType Leaf)) {
        throw "Package manifest not found: $taskManifestPath. Pass -PackageDirectory with the extracted package root that contains metadata/package_manifest.json."
    }
    $taskFiles = [Collections.Generic.List[object]]::new()
    try {
        Assert-ServicePackagePath $taskManifestPath
        $taskManifestStream = [IO.File]::Open($taskManifestPath, 'Open', 'Read', 'Read')
        $taskFiles.Add([pscustomobject]@{ Path = 'metadata/package_manifest.json'; Stream = $taskManifestStream; Entry = $null })
        if ($taskManifestStream.Length -gt 4MB) { throw 'Package manifest limit exceeded.' }
        $taskReader = [IO.StreamReader]::new($taskManifestStream, [Text.UTF8Encoding]::new($false, $true), $true, 4096, $true)
        try { $taskManifest = $taskReader.ReadToEnd() | ConvertFrom-Json } finally { $taskReader.Dispose() }
        if ($taskManifest.schema_version -cne 1 -or $taskManifest.files -isnot [Array] -or
            $taskManifest.files.Count -eq 0 -or $taskManifest.files.Count -gt 10000) { throw 'Invalid package manifest schema.' }
        $taskPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        $taskTotal = [long]0
        foreach ($taskEntry in $taskManifest.files) {
            $taskRelative = $taskEntry.path
            # Root documentation and package scripts are not installed by this copier.
            if ($taskRelative -cin 'readme.md', 'readme_zh_cn.md', 'service_package.ps1') { continue }
            if ($taskRelative -isnot [string] -or $taskRelative.Length -gt 240 -or
                $taskRelative -cnotmatch '^(bin|core|geo|metadata|licenses|plugins|qml|translations)/[A-Za-z0-9_ .()/+-]+$' -or
                @($taskRelative.Split('/') | Where-Object { $_ -in '', '.', '..' -or $_ -match '[. ]$|^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])([.]|$)' }).Count) { throw 'Unsafe package path.' }
            if (-not $taskPaths.Add($taskRelative) -or $taskRelative -ieq 'metadata/package_manifest.json' -or
                $taskEntry.sha256 -isnot [string] -or $taskEntry.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
                ($taskEntry.bytes -isnot [long] -and $taskEntry.bytes -isnot [int]) -or $taskEntry.bytes -lt 0) { throw 'Invalid package entry.' }
            $taskTotal += $taskEntry.bytes
            if ($taskTotal -gt 2GB) { throw 'Package size limit exceeded.' }
            $taskSource = Join-Path $taskRoot $taskRelative
            Assert-ServicePackagePath $taskSource
            $taskStream = [IO.File]::Open($taskSource, 'Open', 'Read', 'Read')
            $taskFiles.Add([pscustomobject]@{ Path = $taskRelative; Stream = $taskStream; Entry = $taskEntry })
            if ($taskStream.Length -ne $taskEntry.bytes -or
                (Get-StreamSha256Hex $taskStream) -cne $taskEntry.sha256) { throw 'Package integrity check failed.' }
            $taskStream.Position = 0
        }
        foreach ($taskRequired in 'bin/quickclash.exe', 'bin/quickclash_service.exe', 'bin/quickclash_network.exe', 'core/mihomo.exe', 'bin/Qt6Core.dll') {
            if (-not $taskPaths.Contains($taskRequired)) { throw "Required package file missing from manifest: $taskRequired" }
        }
        foreach ($taskTop in 'bin', 'core', 'geo', 'metadata', 'licenses', 'plugins', 'qml', 'translations') {
            $taskTopPath = Join-Path $taskRoot $taskTop
            if (-not (Test-Path -LiteralPath $taskTopPath)) { continue }
            $taskQueue = [Collections.Generic.Queue[string]]::new()
            $taskQueue.Enqueue($taskTopPath)
            $taskEntries = 0
            while ($taskQueue.Count) {
                foreach ($taskChild in Get-ChildItem -LiteralPath $taskQueue.Dequeue() -Force) {
                    if (++$taskEntries -gt 20000 -or $taskChild.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe package inventory.' }
                    if ($taskChild.PSIsContainer) { $taskQueue.Enqueue($taskChild.FullName); continue }
                    $taskRelative = Get-PackageRelativePath $taskRoot $taskChild.FullName
                    if ($taskRelative -ine 'metadata/package_manifest.json' -and -not $taskPaths.Contains($taskRelative)) { throw 'Unlisted package file rejected.' }
                }
            }
        }
        return [pscustomobject]@{ Root = $taskRoot; Files = $taskFiles; Bytes = $taskTotal }
    } catch {
        foreach ($taskFile in $taskFiles) { $taskFile.Stream.Dispose() }
        throw
    }
}
function Close-ServicePackage($Package) {
    foreach ($taskFile in $Package.Files) { $taskFile.Stream.Dispose() }
}
function Copy-ServicePackage($Package, [string]$Destination) {
    foreach ($taskFile in $Package.Files) {
        if (-not $taskFile.Entry) { continue }
        $taskTarget = Join-Path $Destination $taskFile.Path
        $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskTarget))
        $taskOutput = [IO.File]::Open($taskTarget, 'CreateNew', 'ReadWrite', 'None')
        try {
            $taskFile.Stream.Position = 0
            $taskFile.Stream.CopyTo($taskOutput, 65536)
            $taskOutput.Flush($true)
            $taskOutput.Position = 0
            if ($taskOutput.Length -ne $taskFile.Entry.bytes -or
                (Get-StreamSha256Hex $taskOutput) -cne $taskFile.Entry.sha256) { throw 'Installed package integrity check failed.' }
        } finally { $taskOutput.Dispose() }
    }
}
