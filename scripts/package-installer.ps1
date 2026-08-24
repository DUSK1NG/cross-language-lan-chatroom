[CmdletBinding()]
param(
    [string]$PackageDirectory = '',
    [string]$OutputDirectory = '',
    [string]$InnoCompiler = '',
    [ValidatePattern('^\d+\.\d+\.\d+(\.\d+)?$')]
    [string]$Version = '1.1.0',
    [switch]$SmokeTest,
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$ReleaseRoot = [System.IO.Path]::GetFullPath((Join-Path $Root 'release'))
if ([string]::IsNullOrWhiteSpace($PackageDirectory)) {
    $PackageDirectory = Join-Path $ReleaseRoot 'LANChat-Windows-x64'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = $ReleaseRoot
}

$PackageDirectory = [System.IO.Path]::GetFullPath($PackageDirectory)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$releasePrefix = $ReleaseRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
foreach ($path in @($PackageDirectory, $OutputDirectory)) {
    if ($path -ne $ReleaseRoot -and -not $path.StartsWith($releasePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Installer paths must stay under the repository release directory: $ReleaseRoot"
    }
}

function Write-Step([string]$Message) {
    Write-Host "[LAN Chat installer] $Message" -ForegroundColor Cyan
}

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Test-InstallerDefinition([string]$Path) {
    Require-File $Path 'Inno Setup definition'
    $content = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    foreach ($required in @(
        'PrivilegesRequired=lowest',
        'Uninstallable=yes',
        'UninstallDisplayIcon={app}\LANChat.exe',
        'Name: "{autoprograms}\LAN Chat"',
        'Name: "{autodesktop}\LAN Chat"',
        'Name: "desktopicon"',
        'Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs'
    )) {
        if ($content.IndexOf($required, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
            throw "Inno Setup definition is missing required installer behavior: $required"
        }
    }
    if ($content -match '(?im)^\s*\[UninstallDelete\]') {
        throw 'Installer definition must not silently delete host certificates or chat data during uninstall.'
    }
}

function Find-InnoCompiler([string]$RequestedCompiler) {
    $candidates = @(
        $RequestedCompiler,
        (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return ''
}

$definition = Join-Path $Root 'installer\LANChat.iss'
$runtimeValidator = Join-Path $PSScriptRoot 'test-member-package.ps1'
Test-InstallerDefinition $definition
Require-File $runtimeValidator 'Unified runtime validator'
if (-not (Test-Path -LiteralPath $PackageDirectory -PathType Container)) {
    throw "Unified runtime package directory was not found: $PackageDirectory"
}

Write-Step 'Validate unified runtime input and sensitive-file exclusions'
$validationArguments = @(
    '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runtimeValidator,
    '-PackageDirectory', $PackageDirectory, '-AllowLocalHost'
)
if ($SmokeTest.IsPresent) { $validationArguments += '-Smoke' }
& powershell.exe @validationArguments
if ($LASTEXITCODE -ne 0) { throw "Unified runtime package validation failed with exit code $LASTEXITCODE." }

if ($ValidateOnly.IsPresent) {
    Write-Host 'Installer definition and unified runtime validation passed.' -ForegroundColor Green
    exit 0
}

$iscc = Find-InnoCompiler $InnoCompiler
if ([string]::IsNullOrWhiteSpace($iscc)) {
    throw 'Inno Setup 6 compiler (ISCC.exe) was not found. Install Inno Setup 6 or pass -InnoCompiler <path-to-ISCC.exe>; use -ValidateOnly to run the preflight without compiling.'
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Write-Step 'Compile per-user installer'
& $iscc ("/DSourceDir={0}" -f $PackageDirectory) ("/DOutputDir={0}" -f $OutputDirectory) ("/DAppVersion={0}" -f $Version) $definition
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE." }

$installer = Join-Path $OutputDirectory 'LANChat-Setup-x64.exe'
Require-File $installer 'Compiled installer'
Write-Host "Installer: $installer" -ForegroundColor Green
Write-Host 'Installer behavior: per-user install, Start menu and optional desktop shortcut, Windows uninstall entry, and local host data preserved on uninstall.' -ForegroundColor Green
