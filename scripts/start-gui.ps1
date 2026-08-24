[CmdletBinding()]
param(
    [switch]$Wait,
    [switch]$Rebuild,
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$buildScript = Join-Path $PSScriptRoot 'build-modern.ps1'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Missing modern build script: $buildScript"
}

function Get-ExistingModernClient {
    $buildDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'out\modern-msvc-x64'
    $manifestFile = Join-Path $buildDirectory 'lan-chat-build.json'
    if (-not (Test-Path -LiteralPath $manifestFile -PathType Leaf)) { return $null }

    try {
        $manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
    } catch {
        return $null
    }

    $gui = [string]$manifest.gui
    $launcher = [string]$manifest.launcher
    $qtPrefix = [string]$manifest.qtPrefix
    $openSslRoot = [string]$manifest.openSslRoot
    if ([string]::IsNullOrWhiteSpace($gui) -or
        [string]::IsNullOrWhiteSpace($launcher) -or
        [string]::IsNullOrWhiteSpace($qtPrefix) -or
        [string]::IsNullOrWhiteSpace($openSslRoot)) {
        return $null
    }

    $qtBin = Join-Path $qtPrefix 'bin'
    $openSslBin = Join-Path $openSslRoot 'bin'
    $required = @(
        $gui,
        $launcher,
        (Join-Path $qtBin 'Qt6Core.dll'),
        (Join-Path $qtBin 'Qt6WebEngineCore.dll'),
        (Join-Path $qtBin 'Qt6WebEngineWidgets.dll')
    )
    if (@($required | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -gt 0) {
        return $null
    }
    if (@(Get-ChildItem -LiteralPath $openSslBin -Filter 'libssl-*.dll' -File -ErrorAction SilentlyContinue).Count -eq 0 -or
        @(Get-ChildItem -LiteralPath $openSslBin -Filter 'libcrypto-*.dll' -File -ErrorAction SilentlyContinue).Count -eq 0) {
        return $null
    }

    return [PSCustomObject]@{
        Gui = $gui
        Launcher = $launcher
        WorkingDirectory = Split-Path -Parent $launcher
        QtBin = $qtBin
        OpenSslBin = $openSslBin
    }
}

if ($CheckOnly.IsPresent -and $Rebuild.IsPresent) {
    throw '-CheckOnly and -Rebuild cannot be used together.'
}

$existingClient = Get-ExistingModernClient
if (-not $Rebuild.IsPresent -and $null -ne $existingClient) {
    if ($CheckOnly.IsPresent) {
        Write-Host "Verified modern client: $($existingClient.Gui)" -ForegroundColor Green
        return
    }

    $previousPath = $env:PATH
    $previousOpenSslRuntimeDirectory = $env:LAN_CHAT_OPENSSL_RUNTIME_DIR
    try {
        # Qt WebEngine probes libraries when the process starts. Keep the
        # vcpkg OpenSSL DLLs out of PATH and let the connection worker load
        # them only when a secure connection is requested.
        $env:PATH = "$($existingClient.QtBin);$previousPath"
        $env:LAN_CHAT_OPENSSL_RUNTIME_DIR = $existingClient.OpenSslBin
        $process = Start-Process -FilePath $existingClient.Launcher -WorkingDirectory $existingClient.WorkingDirectory -PassThru
        if ($Wait.IsPresent) {
            $process.WaitForExit()
            exit $process.ExitCode
        }
    } finally {
        $env:PATH = $previousPath
        $env:LAN_CHAT_OPENSSL_RUNTIME_DIR = $previousOpenSslRuntimeDirectory
    }
    return
}

if ($CheckOnly.IsPresent) {
    throw 'No verified modern client was found. Run scripts\build-modern.ps1 -Action Build or use scripts\bootstrap-github.ps1 to prepare the toolchain.'
}

$buildArguments = @(
    '-NoLogo',
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $buildScript,
    '-Action', 'Launch'
)
if ($Wait.IsPresent) {
    $buildArguments += '-Wait'
}

& powershell.exe @buildArguments
exit $LASTEXITCODE
