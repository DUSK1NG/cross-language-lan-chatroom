$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root "client-cpp\gui\build"
$Server = Join-Path $Root "server-go\chat-server.exe"
$Cert = Join-Path $Root "certs\server-lan.crt"
$Key = Join-Path $Root "certs\server-lan.key"
$Release = Join-Path $Root "release\LANChat-v1"
$Alice = Join-Path $Release "alice-host"
$Bob = Join-Path $Release "bob-client"

foreach ($path in @($Build, $Server, $Cert, $Key)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "缺少发布文件：$path" }
}
if (Test-Path -LiteralPath $Release) { Remove-Item -LiteralPath $Release -Recurse -Force }
New-Item -ItemType Directory -Path $Alice, $Bob | Out-Null
Copy-Item -Path (Join-Path $Build '*') -Destination $Alice -Recurse -Force
Copy-Item -Path (Join-Path $Build '*') -Destination $Bob -Recurse -Force
New-Item -ItemType Directory -Path (Join-Path $Alice 'server-go'), (Join-Path $Alice 'certs'), (Join-Path $Bob 'certs') -Force | Out-Null
Copy-Item -LiteralPath $Server -Destination (Join-Path $Alice 'server-go\chat-server.exe') -Force
Copy-Item -LiteralPath $Cert -Destination (Join-Path $Alice 'certs\server-lan.crt') -Force
Copy-Item -LiteralPath $Key -Destination (Join-Path $Alice 'certs\server-lan.key') -Force
Copy-Item -LiteralPath $Cert -Destination (Join-Path $Bob 'certs\server-lan.crt') -Force
Write-Host "发布目录已生成：$Release"
Write-Host "Alice Host：$Alice"
Write-Host "Bob Client：$Bob"
