[CmdletBinding()]
param(
    [switch]$Wait,
    [switch]$Rebuild,
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($CheckOnly -and $Rebuild) { throw '-CheckOnly and -Rebuild cannot be used together.' }

$root = Split-Path -Parent $PSScriptRoot
$directory = Join-Path $root 'out\desktop'
$executable = Join-Path $directory 'LANChat.exe'
$runtime = @('LANChat.exe', 'lan-chat-server.exe')
$missing = @($runtime | Where-Object { -not (Test-Path -LiteralPath (Join-Path $directory $_) -PathType Leaf) })
if ($CheckOnly) {
    if ($missing.Count) { throw "Desktop runtime is incomplete: $($missing -join ', ')" }
    Write-Host "Verified desktop client: $executable"
    return
}
if ($Rebuild -or $missing.Count) {
    & (Join-Path $PSScriptRoot 'build-desktop.ps1')
}
$process = Start-Process -FilePath $executable -WorkingDirectory $directory -PassThru
if ($Wait) {
    $process.WaitForExit()
    exit $process.ExitCode
}
