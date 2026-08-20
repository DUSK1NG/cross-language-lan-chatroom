param(
    [string]$Version = "v1.0.1"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "client-cpp\gui\build"
$Server = Join-Path $Root "server-go\chat-server.exe"
$SetupGuide = Join-Path $Root "docs\release-setup.md"
$Release = Join-Path $Root ("release\LANChat-" + $Version + "-Windows-x64")

foreach ($path in @($Build, $Server, $SetupGuide)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing release input: $path"
    }
}

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

Write-Host "Release directory: $Release"
Write-Host "Mode selection: choose Host or Member from lan-chat-gui.exe."
Write-Host "Security: no certificate, private key, database, or chat history is included. Read SETUP.md before first use."
