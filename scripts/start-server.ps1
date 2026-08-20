param(
    [int]$Port = 8888,
    [string]$Database = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ServerDir = Join-Path $Root "server-go"
$Server = Join-Path $ServerDir "chat-server.exe"
$Cert = Join-Path $Root "certs\server-lan.crt"
$Key = Join-Path $Root "certs\server-lan.key"
if ([string]::IsNullOrWhiteSpace($Database)) { $Database = Join-Path $ServerDir "chat.db" }

foreach ($path in @($Server, $Cert, $Key)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "缺少运行文件：$path" }
}

Set-Location $ServerDir
& $Server -addr ("0.0.0.0:{0}" -f $Port) -cert $Cert -key $Key -db $Database
exit $LASTEXITCODE
