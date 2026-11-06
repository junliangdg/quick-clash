#Requires -Version 7.2
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Destination
)
$ErrorActionPreference = 'Stop'
$taskResources = Join-Path $PSScriptRoot '../resources'
$taskReadmes = [ordered]@{
    'windows_readme.md' = 'readme.md'
    'windows_readme_zh_cn.md' = 'readme_zh_cn.md'
}
$taskLinks = [ordered]@{
    'windows_readme.md' = 'readme.md'
    'windows_readme_zh_cn.md' = 'readme_zh_cn.md'
    '../LICENSE' = 'licenses/quickclash_gpl_3.txt'
    '../docs/baseline/licenses.md' = 'licenses/source_obligations.md'
}
foreach ($taskSource in $taskReadmes.Keys) {
    $taskText = Get-Content -LiteralPath (Join-Path $taskResources $taskSource) -Raw
    foreach ($taskLink in $taskLinks.Keys) {
        $taskText = $taskText.Replace("]($taskLink)", "]($($taskLinks[$taskLink]))")
    }
    Set-Content -LiteralPath (Join-Path $Destination $taskReadmes[$taskSource]) -Value $taskText -Encoding utf8NoBOM -NoNewline
}
