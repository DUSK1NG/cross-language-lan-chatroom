[CmdletBinding()]
param(
    [switch]$NoLaunch,
    [switch]$NonInteractive
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    if ($NoLaunch) {
        & (Join-Path $PSScriptRoot 'build-desktop.ps1')
    } else {
        & (Join-Path $PSScriptRoot 'start-gui.ps1')
    }
} catch {
    if ($NonInteractive) { throw }
    Write-Host "Build or launch failed: $($_.Exception.Message)" -ForegroundColor Red
    [void](Read-Host 'Press Enter to close')
    exit 1
}
