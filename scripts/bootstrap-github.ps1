[CmdletBinding()]
param(
    [switch]$NoLaunch,
    [switch]$NonInteractive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$BuildScript = Join-Path $PSScriptRoot 'build-modern.ps1'
$InstallScript = Join-Path $PSScriptRoot 'install-modern-toolchain.ps1'
if (-not (Test-Path -LiteralPath $BuildScript -PathType Leaf)) { throw "Missing build script: $BuildScript" }

Write-Host 'LAN Chat GitHub source launcher' -ForegroundColor Cyan
Write-Host "Source directory: $Root"
try {
    Write-Host 'Checking first-build dependencies...'
    $toolchainIncomplete = $false
    try {
        & $BuildScript -CheckOnly
    } catch {
        if ($_.Exception.Message -like 'LAN_CHAT_TOOLCHAIN_INCOMPLETE:*') {
            $toolchainIncomplete = $true
        } else {
            throw
        }
    }

    if ($toolchainIncomplete) {
        if ($NonInteractive) { throw 'The build toolchain is not installed. Run this script without -NonInteractive to configure it.' }
        Write-Host ''
        Write-Host 'First build installs Node.js, Go, MSVC Build Tools, Qt 6 WebEngine, and OpenSSL.' -ForegroundColor Yellow
        Write-Host 'The first setup downloads several GB and may request administrator approval.' -ForegroundColor Yellow
        $answer = Read-Host 'Install now? Type Y to continue'
        if ($answer -notmatch '^(?i:y|yes)$') {
            Write-Host 'Cancelled. Double-click LANChat-Launcher.exe later to resume.'
            return
        }
        & $InstallScript
        Write-Host 'Dependency installation finished. Checking again...'
        try {
            & $BuildScript -CheckOnly
        } catch {
            throw 'The toolchain is still incomplete. Keep this window open and follow the README first-build section.'
        }
    }

    if ($NoLaunch) {
        & $BuildScript -Action Build
    } else {
        & $BuildScript -Action Launch
    }
} catch {
    Write-Host ''
    Write-Host "Setup failed: $($_.Exception.Message)" -ForegroundColor Red
    [void](Read-Host 'Press Enter to close')
    exit 1
}
