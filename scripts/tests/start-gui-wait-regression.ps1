[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$sourceLauncher = Join-Path (Split-Path -Parent $PSScriptRoot) 'start-gui.ps1'
if (-not (Test-Path -LiteralPath $sourceLauncher -PathType Leaf)) {
    throw "Source launcher was not found: $sourceLauncher"
}

$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('lan-chat-start-gui-' + [Guid]::NewGuid().ToString('N'))
try {
    $fixtureScripts = Join-Path $fixtureRoot 'scripts'
    New-Item -ItemType Directory -Force -Path $fixtureScripts | Out-Null
    Copy-Item -LiteralPath $sourceLauncher -Destination (Join-Path $fixtureScripts 'start-gui.ps1')

    $buildStub = @'
param(
    [ValidateSet('Build', 'Test', 'Launch')]
    [string]$Action,
    [switch]$Wait
)

if ($Action -ne 'Launch') { exit 11 }
if ($Wait.IsPresent) { exit 12 }
exit 0
'@
    [System.IO.File]::WriteAllText(
        (Join-Path $fixtureScripts 'build-modern.ps1'),
        $buildStub,
        [System.Text.UTF8Encoding]::new($false))

    $fixtureLauncher = Join-Path $fixtureScripts 'start-gui.ps1'
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $fixtureLauncher
    if ($LASTEXITCODE -ne 0) {
        throw "Default launch should omit -Wait, but exited with $LASTEXITCODE."
    }

    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $fixtureLauncher -Wait
    if ($LASTEXITCODE -ne 12) {
        throw "-Wait was not forwarded as a switch; expected exit 12, got $LASTEXITCODE."
    }

    $buildDirectory = Join-Path $fixtureRoot 'out\modern-msvc-x64'
    $qtPrefix = Join-Path $fixtureRoot 'qt'
    $openSslRoot = Join-Path $fixtureRoot 'openssl'
    New-Item -ItemType Directory -Force -Path $buildDirectory, (Join-Path $qtPrefix 'bin'), (Join-Path $openSslRoot 'bin') | Out-Null
    foreach ($path in @(
        (Join-Path $buildDirectory 'lan-chat-gui.exe'),
        (Join-Path $buildDirectory 'lan-chat-launcher.exe'),
        (Join-Path $qtPrefix 'bin\Qt6Core.dll'),
        (Join-Path $qtPrefix 'bin\Qt6WebEngineCore.dll'),
        (Join-Path $qtPrefix 'bin\Qt6WebEngineWidgets.dll'),
        (Join-Path $openSslRoot 'bin\libssl-3-x64.dll'),
        (Join-Path $openSslRoot 'bin\libcrypto-3-x64.dll')
    )) {
        New-Item -ItemType File -Force -Path $path | Out-Null
    }
    [PSCustomObject]@{
        format = 1
        gui = Join-Path $buildDirectory 'lan-chat-gui.exe'
        launcher = Join-Path $buildDirectory 'lan-chat-launcher.exe'
        qtPrefix = $qtPrefix
        openSslRoot = $openSslRoot
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildDirectory 'lan-chat-build.json') -Encoding UTF8

    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $fixtureLauncher -CheckOnly
    if ($LASTEXITCODE -ne 0) {
        throw "Existing modern client should bypass the build toolchain, but exited with $LASTEXITCODE."
    }
} finally {
    if (Test-Path -LiteralPath $fixtureRoot -PathType Container) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}

Write-Host 'start-gui wait forwarding regression passed.' -ForegroundColor Green
