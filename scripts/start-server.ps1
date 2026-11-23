[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8888,
    [string]$Database = '',
    [Parameter(Mandatory)]
    [ValidatePattern('^[A-Za-z0-9]{3,16}$')]
    [string]$AdminCode
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Server = Join-Path $Root 'out\desktop\lan-chat-server.exe'
if (!(Test-Path -LiteralPath $Server -PathType Leaf)) {
    cargo build --locked --release --manifest-path (Join-Path $Root 'Cargo.toml') -p lan-chat-server --target-dir (Join-Path $Root 'out\rust')
    if ($LASTEXITCODE -ne 0) { throw 'Rust server build failed.' }
    $Server = Join-Path $Root 'out\rust\release\lan-chat-server.exe'
}
$HostData = Join-Path $env:LOCALAPPDATA 'DUSK1NG\LAN Chat\host'
$CertDir = Join-Path $HostData 'certs'
if (!$Database) {
    $Database = Join-Path $HostData 'chat.db'
}
& $Server --mode lan --listen "0.0.0.0:$Port" --cert (Join-Path $CertDir 'server-lan.crt') --key (Join-Path $CertDir 'server-lan.key') --db $Database --admin-code $AdminCode --auto-cert
exit $LASTEXITCODE
