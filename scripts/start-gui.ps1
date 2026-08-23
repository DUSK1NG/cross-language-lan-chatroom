[CmdletBinding()]
param(
    [switch]$Wait
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$buildScript = Join-Path $PSScriptRoot 'build-modern.ps1'
if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
    throw "Missing modern build script: $buildScript"
}

$buildArguments = @(
    '-NoLogo',
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', $buildScript,
    '-Action', 'Launch'
)
if ($Wait.IsPresent) {
    $buildArguments += '-Wait'
}

& powershell.exe @buildArguments
exit $LASTEXITCODE
