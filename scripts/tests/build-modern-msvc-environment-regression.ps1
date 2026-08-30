[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scriptPath = Join-Path $root 'scripts\build-modern.ps1'
$qtPrefix = Join-Path $root '.tools\qt\6.10.3\msvc2022_64'
$openSslRoot = Join-Path $root '.tools\vcpkg\installed\x64-windows'
$mingwPrefix = 'C:\Users\Q1573\Documents\Codex\tools\msys64\mingw64'
$buildDirectory = Join-Path $root 'out\build-modern-msvc-environment-regression'

if (-not (Test-Path -LiteralPath $mingwPrefix -PathType Container)) {
    throw "Regression fixture is missing: $mingwPrefix"
}

$previousPrefix = $env:CMAKE_PREFIX_PATH
try {
    & $scriptPath -CheckOnly
    if ($LASTEXITCODE -ne 0) {
        throw 'Default tool discovery did not accept the repository Qt installation.'
    }

    $env:CMAKE_PREFIX_PATH = $mingwPrefix
    & $scriptPath -Action Build -BuildDirectory $buildDirectory -QtPrefix $qtPrefix -OpenSslRoot $openSslRoot
    if ($LASTEXITCODE -ne 0) { throw "build-modern exited $LASTEXITCODE under a MinGW CMAKE_PREFIX_PATH." }
    Write-Output 'PASS build-modern ignores an ambient MinGW CMAKE_PREFIX_PATH for the MSVC build.'
} finally {
    $env:CMAKE_PREFIX_PATH = $previousPrefix
}
