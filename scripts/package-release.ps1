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
$ReleaseRoot = Join-Path $Root 'release'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $Root 'out\modern-msvc-x64'
}
if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $ReleaseRoot 'LANChat-member-modern-x64'
}

$BuildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$ReleaseDirectory = [System.IO.Path]::GetFullPath($ReleaseDirectory)
$ReleaseRoot = [System.IO.Path]::GetFullPath($ReleaseRoot)
$releasePrefix = $ReleaseRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $ReleaseDirectory.StartsWith($releasePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "ReleaseDirectory must stay under the repository release directory: $ReleaseRoot"
}
$ArchivePath = "$ReleaseDirectory.zip"

function Write-Step([string]$Message) {
    Write-Host "[LAN Chat package] $Message" -ForegroundColor Cyan
}

function Get-CacheValue([string]$CacheFile, [string]$Name) {
    $line = Select-String -LiteralPath $CacheFile -Pattern ("^{0}:PATH=" -f [regex]::Escape($Name)) |
        Select-Object -First 1
    if ($null -eq $line) { throw "$Name was not found in $CacheFile" }
    return $line.Line.Split('=', 2)[1]
}

function Get-QtPrefixFromBuildCache([string]$CacheFile) {
    $qt6Directory = Get-CacheValue $CacheFile 'Qt6_DIR'
    # Qt6_DIR is <QtPrefix>/lib/cmake/Qt6.
    return Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qt6Directory))
}

function Get-OpenSslRootFromBuildCache([string]$CacheFile) {
    $includeDirectory = Get-CacheValue $CacheFile 'OPENSSL_INCLUDE_DIR'
    return Split-Path -Parent $includeDirectory
}

function Copy-OpenSslRuntime([string]$OpenSslRoot, [string]$Destination) {
    $binDirectory = Join-Path $OpenSslRoot 'bin'
    $runtimeDirectory = Join-Path $Destination 'openssl'
    New-Item -ItemType Directory -Force -Path $runtimeDirectory | Out-Null
    foreach ($pattern in @('libssl-*.dll', 'libcrypto-*.dll')) {
        $runtime = Get-ChildItem -LiteralPath $binDirectory -Filter $pattern -File | Select-Object -First 1
        if ($null -eq $runtime) { throw "Missing OpenSSL runtime '$pattern' in $binDirectory" }
        Copy-Item -LiteralPath $runtime.FullName -Destination (Join-Path $runtimeDirectory $runtime.Name) -Force
    }
}

function Copy-MsvcRuntime([string]$Destination) {
    $programFilesX86 = ${env:ProgramFiles(x86)}
    $vsWhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vsWhere -PathType Leaf)) {
        throw "Visual Studio locator was not found: $vsWhere"
    }

    $installation = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) {
        throw 'MSVC Build Tools installation was not found for member runtime deployment.'
    }
    $redistRoot = Join-Path $installation.Trim() 'VC\Redist\MSVC'
    $runtimeDirectory = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object {
            $candidate = Join-Path $_.FullName 'x64\Microsoft.VC143.CRT'
            if (Test-Path -LiteralPath $candidate -PathType Container) { $candidate }
        } |
        Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($runtimeDirectory)) {
        throw "MSVC x64 redistributable files were not found under: $redistRoot"
    }

    $runtimeFiles = @(Get-ChildItem -LiteralPath $runtimeDirectory -Filter '*.dll' -File)
    if ($runtimeFiles.Count -eq 0) { throw "No MSVC runtime DLLs were found in $runtimeDirectory" }
    foreach ($runtime in $runtimeFiles) {
        Copy-Item -LiteralPath $runtime.FullName -Destination (Join-Path $Destination $runtime.Name) -Force
    }
}

function Test-MemberArchive([string]$Archive) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entryNames = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($required in @(
            'LANChat.exe',
            'lan-chat-gui.exe',
            'Qt6WebEngineCore.dll',
            'QtWebEngineProcess.exe',
            'resources/qtwebengine_resources.pak',
            'README-member.md'
        )) {
            if ($entryNames -notcontains $required) {
                throw "Member ZIP is missing required file: $required"
            }
        }

        $forbidden = @($entryNames | Where-Object {
            $_ -match '(^|/)(server-go|frontend|client-cpp|scripts|tools|\.git|\.tools|out|release|logs?|test-results|test-output|coverage|attachments|attachment-cache|__host-data|\.cache)/' -or
            $_ -match '(?i)(\.key$|\.pem$|\.crt$|\.cer$|\.der$|\.pfx$|\.p12$|\.db(?:[-.]|$)|\.sqlite(?:[-.]|$)|\.sqlite3(?:[-.]|$)|\.log(?:[-.]|$)|\.test$|\.tap$|\.trx$|\.junit$|(^|/)chat-server\.exe$|(^|/)LANChat-Launcher\.exe$|(^|/)node\.exe$|(^|/)go\.exe$)'
        })
        if ($forbidden.Count -gt 0) {
            throw "Member ZIP contains forbidden paths: $($forbidden -join '; ')"
        }
    } finally {
        $zip.Dispose()
    }
}

if (-not $SkipBuild) {
    $buildScript = Join-Path $PSScriptRoot 'build-modern.ps1'
    Write-Step 'Build the current modern React + Qt WebEngine client'
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $buildScript -Action Build -BuildDirectory $BuildDirectory
    if ($LASTEXITCODE -ne 0) { throw "Modern build failed with exit code $LASTEXITCODE." }
}

$cacheFile = Join-Path $BuildDirectory 'CMakeCache.txt'
$guiSource = Join-Path $BuildDirectory 'lan-chat-gui.exe'
$launcherSource = Join-Path $BuildDirectory 'lan-chat-launcher.exe'
$memberGuide = Join-Path $Root 'docs\member-package.md'
$iconSource = Join-Path $Root 'assets\LANChat.ico'
foreach ($requiredInput in @($cacheFile, $guiSource, $launcherSource, $memberGuide, $iconSource)) {
    if (-not (Test-Path -LiteralPath $requiredInput -PathType Leaf)) {
        throw "Missing member-package input: $requiredInput"
    }
}

$qtPrefix = Get-QtPrefixFromBuildCache $cacheFile
$deployTool = Join-Path $qtPrefix 'bin\windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "Qt deployment tool was not found: $deployTool"
}
$openSslRoot = Get-OpenSslRootFromBuildCache $cacheFile
if (-not (Test-Path -LiteralPath (Join-Path $openSslRoot 'include\openssl\ssl.h') -PathType Leaf)) {
    throw "OpenSSL build input is invalid: $openSslRoot"
}

if (Test-Path -LiteralPath $ReleaseDirectory) {
    Remove-Item -LiteralPath $ReleaseDirectory -Recurse -Force
}
if (Test-Path -LiteralPath $ArchivePath -PathType Leaf) {
    Remove-Item -LiteralPath $ArchivePath -Force
}
New-Item -ItemType Directory -Force -Path $ReleaseDirectory | Out-Null

Write-Step 'Copy modern GUI executable and runtime entry point'
$guiTarget = Join-Path $ReleaseDirectory 'lan-chat-gui.exe'
Copy-Item -LiteralPath $guiSource -Destination $guiTarget -Force
Copy-Item -LiteralPath $launcherSource -Destination (Join-Path $ReleaseDirectory 'LANChat.exe') -Force

Write-Step 'Deploy Qt WebEngine and MSVC runtime'
& $deployTool --release --compiler-runtime --no-quick-import $guiTarget
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE." }

Write-Step 'Copy OpenSSL, MSVC runtime, member instructions, and shortcut icon'
Copy-OpenSslRuntime $openSslRoot $ReleaseDirectory
Copy-MsvcRuntime $ReleaseDirectory
Copy-Item -LiteralPath $memberGuide -Destination (Join-Path $ReleaseDirectory 'README-member.md') -Force
Copy-Item -LiteralPath $iconSource -Destination (Join-Path $ReleaseDirectory 'LANChat.ico') -Force

$packageTest = Join-Path $PSScriptRoot 'test-member-package.ps1'
$testArguments = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $packageTest,
    '-PackageDirectory', $ReleaseDirectory)
if (-not $SkipSmokeTest) { $testArguments += '-Smoke' }
Write-Step 'Validate member package contents'
& powershell.exe @testArguments
if ($LASTEXITCODE -ne 0) { throw "Member package validation failed with exit code $LASTEXITCODE." }

Write-Step 'Create ZIP archive'
Compress-Archive -Path (Join-Path $ReleaseDirectory '*') -DestinationPath $ArchivePath -CompressionLevel Optimal -Force
if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf)) {
    throw "Member ZIP archive was not created: $ArchivePath"
}
Test-MemberArchive $ArchivePath

Write-Host "Member package directory: $ReleaseDirectory" -ForegroundColor Green
Write-Host "Member package archive:    $ArchivePath" -ForegroundColor Green
Write-Host 'Security: this package contains no server, private key, database, chat history, certificate, source tree, compiler, Node.js, Go SDK, or auto-build launcher.' -ForegroundColor Green
