[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageDirectory,
    [switch]$Smoke
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PackageDirectory = [System.IO.Path]::GetFullPath($PackageDirectory)
if (-not (Test-Path -LiteralPath $PackageDirectory -PathType Container)) {
    throw "Member package directory was not found: $PackageDirectory"
}

function Require-File([string]$RelativePath) {
    $path = Join-Path $PackageDirectory $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Member package is missing required file: $RelativePath"
    }
}

foreach ($relativePath in @(
    'lan-chat-gui.exe',
    'Qt6Core.dll',
    'Qt6WebChannel.dll',
    'Qt6WebEngineCore.dll',
    'Qt6WebEngineWidgets.dll',
    'QtWebEngineProcess.exe',
    'platforms\qwindows.dll',
    'resources\icudtl.dat',
    'resources\qtwebengine_resources.pak',
    'resources\v8_context_snapshot.bin',
    'README-member.md'
)) {
    Require-File $relativePath
}

foreach ($pattern in @('libssl-*.dll', 'libcrypto-*.dll', 'msvcp140*.dll', 'vcruntime140*.dll')) {
    if (@(Get-ChildItem -LiteralPath $PackageDirectory -Filter $pattern -File).Count -eq 0) {
        throw "Member package is missing runtime matching: $pattern"
    }
}

$localesDirectory = Join-Path $PackageDirectory 'translations\qtwebengine_locales'
if (@(Get-ChildItem -LiteralPath $localesDirectory -Filter '*.pak' -File -ErrorAction SilentlyContinue).Count -eq 0) {
    throw 'Member package is missing Qt WebEngine locales.'
}

foreach ($forbiddenDirectory in @('server-go', '.git', 'frontend', 'client-cpp', 'scripts', 'tools')) {
    if (Test-Path -LiteralPath (Join-Path $PackageDirectory $forbiddenDirectory)) {
        throw "Member package contains forbidden directory: $forbiddenDirectory"
    }
}

$forbiddenFiles = @(Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File |
    Where-Object {
        $_.Name -like '*.key' -or $_.Name -like '*.pem' -or $_.Name -like '*.crt' -or
        $_.Name -like '*.db' -or $_.Name -eq 'chat-server.exe' -or $_.Name -eq 'LANChat-Launcher.exe' -or
        $_.Name -eq 'node.exe' -or $_.Name -eq 'npm.cmd' -or $_.Name -eq 'pnpm.cmd' -or $_.Name -eq 'go.exe'
    })
if ($forbiddenFiles.Count -gt 0) {
    $names = ($forbiddenFiles | ForEach-Object { $_.FullName }) -join '; '
    throw "Member package contains forbidden files: $names"
}

if ($Smoke) {
    $guiExe = Join-Path $PackageDirectory 'lan-chat-gui.exe'
    $previousPath = $env:PATH
    $process = $null
    try {
        # Deliberately omit the developer Qt path: the packaged directory must
        # provide every application runtime dependency by itself.
        $env:PATH = "$PackageDirectory;$env:SystemRoot\System32;$env:SystemRoot"
        $process = Start-Process -FilePath $guiExe -WorkingDirectory $PackageDirectory -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 3000
        $process.Refresh()
        if ($process.HasExited) {
            throw "Member GUI exited during smoke test with code $($process.ExitCode)."
        }
    } finally {
        $env:PATH = $previousPath
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
        }
    }
}

Write-Host "Member package validation passed: $PackageDirectory" -ForegroundColor Green
