[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$BootstrapDirectory = Join-Path $Root 'tools\bootstrap'
$VsConfig = Join-Path $BootstrapDirectory 'lan-chat.vsconfig'
$ToolsDirectory = Join-Path $Root '.tools'
$QtRoot = Join-Path $ToolsDirectory 'qt'
$VcpkgRoot = Join-Path $ToolsDirectory 'vcpkg'

function Write-Step([string]$Message) {
    Write-Host "[LAN Chat setup] $Message" -ForegroundColor Cyan
}

function Require-Winget {
    if ($null -eq (Get-Command winget.exe -ErrorAction SilentlyContinue)) {
        throw 'winget was not found. Install Microsoft App Installer from the Microsoft Store, then run LANChat-Launcher.exe again.'
    }
}

function Install-WingetPackage([string]$Id, [string[]]$ExtraArguments = @()) {
    Write-Step "Install $Id"
    & winget.exe install --exact --id $Id --source winget --accept-source-agreements --accept-package-agreements @ExtraArguments
    if ($LASTEXITCODE -ne 0) { throw "winget failed for $Id with exit code $LASTEXITCODE." }
}

function Resolve-Python {
    foreach ($name in @('py.exe', 'python.exe')) {
        $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command) { return $command.Source }
    }
    $candidate = Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    return ''
}

function Resolve-Git {
    $command = Get-Command 'git.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) { return $command.Source }
    $candidate = 'C:\Program Files\Git\cmd\git.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    throw 'Git was installed but is not available to this process. Close this window and run LANChat-Launcher.exe again.'
}

function Resolve-Npm {
    $command = Get-Command 'npm.cmd' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) { return $command.Source }
    $candidate = 'C:\Program Files\nodejs\npm.cmd'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    throw 'Node.js was installed but npm.cmd is not available to this process. Close this window and run LANChat-Launcher.exe again.'
}

Require-Winget
Install-WingetPackage 'OpenJS.NodeJS.LTS'
Install-WingetPackage 'GoLang.Go'
Install-WingetPackage 'Git.Git'
Install-WingetPackage 'Python.Python.3.12'

$npm = Resolve-Npm
Write-Step 'Install pnpm 11.19.0'
& $npm install --global 'pnpm@11.19.0'
if ($LASTEXITCODE -ne 0) { throw "pnpm installation failed with exit code $LASTEXITCODE." }

if (-not (Test-Path -LiteralPath $VsConfig -PathType Leaf)) { throw "Missing MSVC configuration: $VsConfig" }
Install-WingetPackage 'Microsoft.VisualStudio.2022.BuildTools' @('--override', "--wait --passive --norestart --config `"$VsConfig`"")

$python = Resolve-Python
if ([string]::IsNullOrWhiteSpace($python)) {
    throw 'Python was installed but is not available to this process. Close this window and run LANChat-Launcher.exe again.'
}

Write-Step 'Install Qt downloader'
& $python -m pip install --user 'aqtinstall==3.3.0'
if ($LASTEXITCODE -ne 0) { throw "aqtinstall failed with exit code $LASTEXITCODE." }

Write-Step 'Install Qt 6.11.2 MSVC, WebEngine, and WebChannel (large first download)'
& $python -m aqt install-qt --outputdir $QtRoot windows desktop 6.11.2 win64_msvc2022_64 -m qtwebengine qtwebchannel
if ($LASTEXITCODE -ne 0) { throw "Qt installation failed with exit code $LASTEXITCODE." }

if (-not (Test-Path -LiteralPath $VcpkgRoot -PathType Container)) {
    Write-Step 'Download vcpkg'
    $git = Resolve-Git
    & $git clone --depth 1 https://github.com/microsoft/vcpkg.git $VcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw "vcpkg download failed with exit code $LASTEXITCODE." }
}

$vcpkg = Join-Path $VcpkgRoot 'vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkg -PathType Leaf)) {
    Write-Step 'Bootstrap vcpkg'
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed with exit code $LASTEXITCODE." }
}

Write-Step 'Install OpenSSL x64'
& $vcpkg install 'openssl:x64-windows'
if ($LASTEXITCODE -ne 0) { throw "OpenSSL installation failed with exit code $LASTEXITCODE." }

Write-Host ''
Write-Host 'Dependency installation completed. The launcher will now compile and run the modern client.' -ForegroundColor Green
