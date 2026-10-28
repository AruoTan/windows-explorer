#Requires -Version 5.1
[CmdletBinding()]
param([string]$Destination)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$inputDirectory = Join-Path $taskRoot 'out/explorer-extensions'
$templateDirectory = Join-Path $taskRoot 'packaging/explorer-extensions'
if (-not $Destination) { $Destination = Join-Path $taskRoot 'dist/ExplorerExtensions-x64.zip' }
$Destination = [IO.Path]::GetFullPath($Destination)
$entries = @(Get-Content -LiteralPath (Join-Path $inputDirectory 'SHA256SUMS'))
if ($entries.Count -ne 2) { throw 'Expected checksums for two Windows binaries' }
$seen = @{}
foreach ($entry in $entries) {
    if ($entry -notmatch '^([0-9a-f]{64})  (ExplorerExtensions\.(dll|exe))$') { throw 'Invalid checksum entry' }
    $expected = $Matches[1]
    $name = $Matches[2]
    if ($seen.ContainsKey($name)) { throw "Duplicate checksum entry: $name" }
    $seen[$name] = $true
    $actual = (Get-FileHash -LiteralPath (Join-Path $inputDirectory $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) { throw "Checksum mismatch: $name" }
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$temporary = Join-Path ([IO.Path]::GetTempPath()) ('explorer-extensions-package-' + [guid]::NewGuid().ToString('N') + '.zip')
$staging = Join-Path ([IO.Path]::GetTempPath()) ('explorer-extensions-staging-' + [guid]::NewGuid().ToString('N'))
try {
    [void][IO.Directory]::CreateDirectory($staging)
    foreach ($name in @('ExplorerExtensions.dll', 'ExplorerExtensions.exe', 'SHA256SUMS')) {
        Copy-Item -LiteralPath (Join-Path $inputDirectory $name) -Destination (Join-Path $staging $name)
    }
    foreach ($name in @('README.md', 'THIRD_PARTY_NOTICES.txt', 'MinHook-LICENSE.txt')) {
        Copy-Item -LiteralPath (Join-Path $templateDirectory $name) -Destination (Join-Path $staging $name)
    }
    [IO.Compression.ZipFile]::CreateFromDirectory($staging, $temporary)
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination))
    [IO.File]::WriteAllBytes($Destination, [IO.File]::ReadAllBytes($temporary))
    Write-Output "Packaged: $Destination"
} finally {
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
