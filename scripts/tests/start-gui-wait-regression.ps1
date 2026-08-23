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
    New-Item -ItemType Directory -Force -Path $fixtureRoot | Out-Null
    Copy-Item -LiteralPath $sourceLauncher -Destination (Join-Path $fixtureRoot 'start-gui.ps1')

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
        (Join-Path $fixtureRoot 'build-modern.ps1'),
        $buildStub,
        [System.Text.UTF8Encoding]::new($false))

    $fixtureLauncher = Join-Path $fixtureRoot 'start-gui.ps1'
    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $fixtureLauncher
    if ($LASTEXITCODE -ne 0) {
        throw "Default launch should omit -Wait, but exited with $LASTEXITCODE."
    }

    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $fixtureLauncher -Wait
    if ($LASTEXITCODE -ne 12) {
        throw "-Wait was not forwarded as a switch; expected exit 12, got $LASTEXITCODE."
    }
} finally {
    if (Test-Path -LiteralPath $fixtureRoot -PathType Container) {
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}

Write-Host 'start-gui wait forwarding regression passed.' -ForegroundColor Green
