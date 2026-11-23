[CmdletBinding()]
param(
    [ValidateSet('lan', 'online')]
    [string]$Edition = 'lan',
    [string]$BuildDirectory = '',
    [string]$ReleaseDirectory = '',
    [switch]$SkipBuild,
    [switch]$SkipSmokeTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$releaseRoot = [IO.Path]::GetFullPath((Join-Path $root 'release'))
if (!$BuildDirectory) { $BuildDirectory = Join-Path $root $(if ($Edition -eq 'lan') { 'out\desktop' } else { 'out\desktop-online' }) }
if (!$ReleaseDirectory) { $ReleaseDirectory = Join-Path $releaseRoot $(if ($Edition -eq 'lan') { 'LANChat-Desktop-Windows-x64' } else { 'LANChat-Online-Windows-x64' }) }
$ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
$releasePrefix = $releaseRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
if (!$ReleaseDirectory.StartsWith($releasePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "ReleaseDirectory must stay under $releaseRoot"
}
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build-desktop.ps1') -Edition $Edition -OutputDirectory $BuildDirectory }

$files = @('LANChat.exe', 'LANChat.ico')
New-Item -ItemType Directory -Path $ReleaseDirectory -Force | Out-Null
if ($Edition -eq 'lan') {
    $files += 'lan-chat-server.exe'
}
foreach ($file in $files) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory $file) -Destination (Join-Path $ReleaseDirectory $file) -Force
}
Copy-Item -LiteralPath (Join-Path $root 'docs\unified-package.md') -Destination (Join-Path $ReleaseDirectory 'README.md') -Force

$validate = @{ PackageDirectory = $ReleaseDirectory; Edition = $Edition; Smoke = -not $SkipSmokeTest }
& (Join-Path $PSScriptRoot 'test-member-package.ps1') @validate
$archive = "$ReleaseDirectory.zip"
Compress-Archive -Path (Join-Path $ReleaseDirectory '*') -DestinationPath $archive -CompressionLevel Optimal -Force
Write-Host "Desktop package: $archive"
