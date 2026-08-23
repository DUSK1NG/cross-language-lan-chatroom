[CmdletBinding()]
param(
    [string]$BuildDirectory = '',
    [string]$ReleaseDirectory = '',
    [switch]$SkipBuild,
    [switch]$SkipSmokeTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$ReleaseRoot = [System.IO.Path]::GetFullPath((Join-Path $Root 'release'))
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $Root 'out\modern-msvc-x64'
}
if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $ReleaseRoot 'LANChat-Windows-x64'
}

$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$ReleaseDirectory = [System.IO.Path]::GetFullPath($ReleaseDirectory)
$releasePrefix = $ReleaseRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $ReleaseDirectory.StartsWith($releasePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "ReleaseDirectory must stay under the repository release directory: $ReleaseRoot"
}
$ArchivePath = "$ReleaseDirectory.zip"

function Write-Step([string]$Message) {
    Write-Host "[LAN Chat unified package] $Message" -ForegroundColor Cyan
}

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Test-UnifiedArchive([string]$Archive) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($required in @(
            'lan-chat-gui.exe',
            'Qt6WebEngineCore.dll',
            'QtWebEngineProcess.exe',
            'resources/qtwebengine_resources.pak',
            'server-go/chat-server.exe',
            'README.md'
        )) {
            if ($entries -notcontains $required) {
                throw "Unified ZIP is missing required file: $required"
            }
        }
        $forbidden = @($entries | Where-Object {
            $_ -match '(^|/)(\.git|\.tools|out|release|frontend|client-cpp|scripts|tools|server-go/certs)/' -or
            $_ -match '(?i)(\.key$|\.pem$|\.crt$|\.db$|(^|/)LANChat-Launcher\.exe$|(^|/)(node|go)\.exe$|(^|/)(pnpm|npm)\.cmd$)'
        })
        if ($forbidden.Count -gt 0) {
            throw "Unified ZIP contains forbidden paths: $($forbidden -join '; ')"
        }
    } finally {
        $zip.Dispose()
    }
}

$memberPackager = Join-Path $PSScriptRoot 'package-release.ps1'
$packageTest = Join-Path $PSScriptRoot 'test-member-package.ps1'
$serverSource = Join-Path $Root 'server-go\chat-server.exe'
$guideSource = Join-Path $Root 'docs\unified-package.md'
foreach ($input in @($memberPackager, $packageTest, $guideSource)) {
    Require-File $input 'Unified package input'
}

Write-Step 'Create the verified GUI runtime base'
$memberArguments = @(
    '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $memberPackager,
    '-BuildDirectory', $BuildDirectory, '-ReleaseDirectory', $ReleaseDirectory
)
if ($SkipBuild.IsPresent) { $memberArguments += '-SkipBuild' }
if ($SkipSmokeTest.IsPresent) { $memberArguments += '-SkipSmokeTest' }
& powershell.exe @memberArguments
if ($LASTEXITCODE -ne 0) { throw "GUI runtime base packaging failed with exit code $LASTEXITCODE." }

Require-File $serverSource 'Built Go server for the unified package'
Write-Step 'Add the local-host server without host identity files'
$serverTargetDirectory = Join-Path $ReleaseDirectory 'server-go'
New-Item -ItemType Directory -Force -Path $serverTargetDirectory | Out-Null
Copy-Item -LiteralPath $serverSource -Destination (Join-Path $serverTargetDirectory 'chat-server.exe') -Force
$memberGuide = Join-Path $ReleaseDirectory 'README-member.md'
if (Test-Path -LiteralPath $memberGuide -PathType Leaf) {
    Remove-Item -LiteralPath $memberGuide -Force
}
Copy-Item -LiteralPath $guideSource -Destination (Join-Path $ReleaseDirectory 'README.md') -Force

if (Test-Path -LiteralPath $ArchivePath -PathType Leaf) {
    Remove-Item -LiteralPath $ArchivePath -Force
}

Write-Step 'Validate the unified host/member runtime'
$testArguments = @(
    '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $packageTest,
    '-PackageDirectory', $ReleaseDirectory, '-AllowLocalHost'
)
if (-not $SkipSmokeTest.IsPresent) { $testArguments += '-Smoke' }
& powershell.exe @testArguments
if ($LASTEXITCODE -ne 0) { throw "Unified package validation failed with exit code $LASTEXITCODE." }

Write-Step 'Create ZIP archive'
Compress-Archive -Path (Join-Path $ReleaseDirectory '*') -DestinationPath $ArchivePath -CompressionLevel Optimal -Force
Require-File $ArchivePath 'Unified ZIP archive'
Test-UnifiedArchive $ArchivePath

Write-Host "Unified package directory: $ReleaseDirectory" -ForegroundColor Green
Write-Host "Unified package archive:    $ArchivePath" -ForegroundColor Green
Write-Host 'Security: this package contains the GUI runtime and local server only. The host certificate, private key, database, chat history, source tree, compiler, Node.js, Go SDK, and auto-build launcher are excluded.' -ForegroundColor Green
