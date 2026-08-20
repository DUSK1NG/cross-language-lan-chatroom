param(
    [string]$Version = "v1.0.1"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "client-cpp\gui\build"
$Server = Join-Path $Root "server-go\chat-server.exe"
$SetupGuide = Join-Path $Root "docs\release-setup.md"
$Release = Join-Path $Root ("release\LANChat-" + $Version + "-Windows-x64")

function Get-QtPrefixFromBuildCache {
    $cache = Join-Path $Build "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cache)) {
        throw "Missing CMake cache: $cache. Build the GUI with CMake before packaging."
    }

    $qt6Line = Select-String -LiteralPath $cache -Pattern '^Qt6_DIR:PATH=' | Select-Object -First 1
    if (-not $qt6Line) {
        throw "Qt6_DIR was not found in $cache."
    }

    # Qt6_DIR points to <QtPrefix>/lib/cmake/Qt6.
    return Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $qt6Line.Line.Split('=', 2)[1]))
}

function Ensure-QtRuntime {
    $qtPrefix = Get-QtPrefixFromBuildCache
    $guiExe = Join-Path $Build "lan-chat-gui.exe"
    $deployTool = Join-Path $qtPrefix "bin\windeployqt.exe"
    if (-not (Test-Path -LiteralPath $guiExe)) {
        throw "Missing GUI executable: $guiExe"
    }
    if (-not (Test-Path -LiteralPath $deployTool)) {
        throw "Missing Qt deployment tool: $deployTool"
    }

    & $deployTool --release --qmldir (Join-Path $Root "client-cpp\gui\qml") $guiExe
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed with exit code $LASTEXITCODE"
    }

    $svgPlugin = Join-Path $qtPrefix "plugins\imageformats\qsvg.dll"
    $svgLibrary = Join-Path $qtPrefix "bin\Qt6Svg.dll"
    foreach ($source in @($svgPlugin, $svgLibrary)) {
        if (-not (Test-Path -LiteralPath $source)) {
            throw "Missing Qt SVG runtime dependency: $source"
        }
    }

    $imageFormats = Join-Path $Build "imageformats"
    New-Item -ItemType Directory -Force -Path $imageFormats | Out-Null
    Copy-Item -LiteralPath $svgPlugin -Destination (Join-Path $imageFormats "qsvg.dll") -Force
    Copy-Item -LiteralPath $svgLibrary -Destination (Join-Path $Build "Qt6Svg.dll") -Force
}

function Ensure-OpenSslRuntime {
    $cache = Join-Path $Build "CMakeCache.txt"
    $includeLine = Select-String -LiteralPath $cache -Pattern '^OPENSSL_INCLUDE_DIR:PATH=' | Select-Object -First 1
    if (-not $includeLine) {
        throw "OPENSSL_INCLUDE_DIR was not found in $cache."
    }

    $openSslPrefix = Split-Path -Parent $includeLine.Line.Split('=', 2)[1]
    $openSslBin = Join-Path $openSslPrefix "bin"
    $runtimeFiles = @(
        Get-ChildItem -LiteralPath $openSslBin -Filter 'libssl-*.dll' | Select-Object -First 1
        Get-ChildItem -LiteralPath $openSslBin -Filter 'libcrypto-*.dll' | Select-Object -First 1
    )
    foreach ($runtimeFile in $runtimeFiles) {
        if ($null -eq $runtimeFile) {
            throw "Missing OpenSSL runtime dependency in $openSslBin"
        }
        Copy-Item -LiteralPath $runtimeFile.FullName -Destination (Join-Path $Build $runtimeFile.Name) -Force
    }
}

foreach ($path in @($Build, $Server, $SetupGuide)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing release input: $path"
    }
}

Ensure-QtRuntime
Ensure-OpenSslRuntime

if (Test-Path -LiteralPath $Release) {
    Remove-Item -LiteralPath $Release -Recurse -Force
}

New-Item -ItemType Directory -Path $Release | Out-Null
Copy-Item -Path (Join-Path $Build '*') -Destination $Release -Recurse -Force

New-Item -ItemType Directory -Force -Path `
    (Join-Path $Release 'server-go'), `
    (Join-Path $Release 'certs') | Out-Null

Get-ChildItem -LiteralPath (Join-Path $Release 'certs') -Force | Remove-Item -Recurse -Force

Copy-Item -LiteralPath $Server -Destination (Join-Path $Release 'server-go\chat-server.exe') -Force
Copy-Item -LiteralPath $SetupGuide -Destination (Join-Path $Release 'SETUP.md') -Force

$requiredRuntimeFiles = @(
    'lan-chat-gui.exe',
    'Qt6Core.dll',
    'Qt6Svg.dll',
    'libssl-3-x64.dll',
    'libcrypto-3-x64.dll',
    'platforms\qwindows.dll',
    'imageformats\qsvg.dll'
)
foreach ($relativePath in $requiredRuntimeFiles) {
    $requiredPath = Join-Path $Release $relativePath
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Release runtime validation failed: missing $relativePath"
    }
}

Write-Host "Release directory: $Release"
Write-Host "Mode selection: choose Host or Member from lan-chat-gui.exe."
Write-Host "Security: no certificate, private key, database, or chat history is included. Read SETUP.md before first use."
