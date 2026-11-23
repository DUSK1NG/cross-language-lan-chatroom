[CmdletBinding()]
param(
    [ValidateSet('lan', 'online')]
    [string]$Edition = 'lan',
    [string]$OutputDirectory = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $root $(if ($Edition -eq 'lan') { 'out\desktop' } else { 'out\desktop-online' })
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$manifest = Join-Path $root 'Cargo.toml'
$frontend = Join-Path $root 'frontend'
$rustTarget = if ($env:CARGO_TARGET_DIR) { [IO.Path]::GetFullPath($env:CARGO_TARGET_DIR) } else { Join-Path $root 'out\rust' }

pnpm.cmd --dir $frontend run build
if ($LASTEXITCODE -ne 0) { throw 'Frontend build failed.' }

$features = if ($Edition -eq 'online') { 'custom-protocol,online' } else { 'custom-protocol' }
cargo build --manifest-path $manifest --locked --release --target-dir $rustTarget -p lan-chat-desktop --features $features
if ($LASTEXITCODE -ne 0) { throw 'Tauri desktop build failed.' }

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Copy-Item -LiteralPath (Join-Path $rustTarget 'release\lan-chat-desktop.exe') -Destination (Join-Path $OutputDirectory 'LANChat.exe')
Copy-Item -LiteralPath (Join-Path $root 'assets\LANChat.ico') -Destination $OutputDirectory
if ($Edition -eq 'lan') {
    cargo build --manifest-path $manifest --locked --release --target-dir $rustTarget -p lan-chat-server
    if ($LASTEXITCODE -ne 0) { throw 'Rust server build failed.' }
    Copy-Item -LiteralPath (Join-Path $rustTarget 'release\lan-chat-server.exe') -Destination $OutputDirectory
}
Write-Host "Desktop build: $OutputDirectory"
