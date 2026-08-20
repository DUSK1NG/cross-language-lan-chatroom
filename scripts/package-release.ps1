param(
    [string]$Version = "v1.0.1"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "client-cpp\gui\build"
$Server = Join-Path $Root "server-go\chat-server.exe"
$SetupGuide = Join-Path $Root "docs\release-setup.md"
$Release = Join-Path $Root ("release\LANChat-" + $Version)
$Alice = Join-Path $Release "alice-host"
$Bob = Join-Path $Release "bob-client"

foreach ($path in @($Build, $Server, $SetupGuide)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing release input: $path"
    }
}

if (Test-Path -LiteralPath $Release) {
    Remove-Item -LiteralPath $Release -Recurse -Force
}

New-Item -ItemType Directory -Path $Alice, $Bob | Out-Null
Copy-Item -Path (Join-Path $Build '*') -Destination $Alice -Recurse -Force
Copy-Item -Path (Join-Path $Build '*') -Destination $Bob -Recurse -Force

New-Item -ItemType Directory -Force -Path `
    (Join-Path $Alice 'server-go'), `
    (Join-Path $Alice 'certs'), `
    (Join-Path $Bob 'certs') | Out-Null

foreach ($certificateDirectory in @((Join-Path $Alice 'certs'), (Join-Path $Bob 'certs'))) {
    Get-ChildItem -LiteralPath $certificateDirectory -Force | Remove-Item -Recurse -Force
}

Copy-Item -LiteralPath $Server -Destination (Join-Path $Alice 'server-go\chat-server.exe') -Force
Copy-Item -LiteralPath $SetupGuide -Destination (Join-Path $Alice 'SETUP.md') -Force
Copy-Item -LiteralPath $SetupGuide -Destination (Join-Path $Bob 'SETUP.md') -Force

Write-Host "Release directory: $Release"
Write-Host "Alice Host: $Alice"
Write-Host "Bob Client: $Bob"
Write-Host "Security: no certificate, private key, database, or chat history is included. Read SETUP.md on the Host machine."
