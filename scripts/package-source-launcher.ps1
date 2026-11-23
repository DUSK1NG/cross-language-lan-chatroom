[CmdletBinding()]
param(
    [string]$LauncherBuildDirectory = '',
    [string]$ReleaseDirectory = '',
    [switch]$SkipLauncherBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$ReleaseRoot = Join-Path $Root 'release'
if ([string]::IsNullOrWhiteSpace($LauncherBuildDirectory)) {
    $LauncherBuildDirectory = Join-Path $Root 'out\launcher-release'
}
if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $ReleaseRoot 'LANChat-Source-Launcher-windows-x64'
}

$Root = [System.IO.Path]::GetFullPath($Root)
$ReleaseRoot = [System.IO.Path]::GetFullPath($ReleaseRoot)
$LauncherBuildDirectory = [System.IO.Path]::GetFullPath($LauncherBuildDirectory)
$ReleaseDirectory = [System.IO.Path]::GetFullPath($ReleaseDirectory)
$releasePrefix = $ReleaseRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
if (-not $ReleaseDirectory.StartsWith($releasePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "ReleaseDirectory must stay under the repository release directory: $ReleaseRoot"
}
$ArchivePath = "$ReleaseDirectory.zip"

function Write-Step([string]$Message) {
    Write-Host "[LAN Chat source package] $Message" -ForegroundColor Cyan
}

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Test-SourcePackageDirectory([string]$Directory) {
    foreach ($required in @(
        'README.md',
        'LANChat-Launcher.exe',
        'scripts\bootstrap-github.ps1',
        'scripts\build-desktop.ps1',
        'apps\desktop\src-tauri\Cargo.toml',
        'scripts\package-source-launcher.ps1',
        'docs\CODEX_HANDOFF.md',
        'tools\bootstrap\Cargo.toml',
        'tools\bootstrap\src\main.rs'
    )) {
        Require-File (Join-Path $Directory $required) "Source package required file '$required'"
    }

    $forbiddenDirectories = @(
        '.git', '.tools', 'out', 'release', 'frontend\node_modules', 'frontend\dist'
    )
    foreach ($relativePath in $forbiddenDirectories) {
        if (Test-Path -LiteralPath (Join-Path $Directory $relativePath)) {
            throw "Source package contains forbidden directory: $relativePath"
        }
    }

    $forbiddenFiles = @(Get-ChildItem -LiteralPath $Directory -Recurse -File |
        Where-Object {
            $_.Extension -in @('.key', '.pem', '.crt', '.db', '.log') -or
            $_.Name -in @('lan-chat-server.exe', 'node.exe', 'pnpm.cmd', 'npm.cmd') -or
            $_.DirectoryName -match '(^|[\\/])certs([\\/]|$)'
        })
    if ($forbiddenFiles.Count -gt 0) {
        throw "Source package contains forbidden files: $($forbiddenFiles.FullName -join '; ')"
    }
}

function Test-SourceArchive([string]$Archive) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $entryNames = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($required in @(
            'README.md',
            'LANChat-Launcher.exe',
            'scripts/bootstrap-github.ps1',
            'scripts/build-desktop.ps1',
            'apps/desktop/src-tauri/Cargo.toml',
            'scripts/package-source-launcher.ps1',
            'docs/CODEX_HANDOFF.md',
            'tools/bootstrap/Cargo.toml',
            'tools/bootstrap/src/main.rs'
        )) {
            if ($entryNames -notcontains $required) {
                throw "Source ZIP is missing required file: $required"
            }
        }

        $forbidden = @($entryNames | Where-Object {
            $_ -match '(^|/)(\.git|\.tools|out|release|frontend/node_modules|frontend/dist|certs)/' -or
            $_ -match '(?i)(\.(key|pem|crt|db|log)$|(^|/)(lan-chat-server|node)\.exe$|(^|/)(pnpm|npm)\.cmd$)'
        })
        if ($forbidden.Count -gt 0) {
            throw "Source ZIP contains forbidden paths: $($forbidden -join '; ')"
        }
    } finally {
        $zip.Dispose()
    }
}

$git = Get-Command git.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $git) { throw 'git.exe is required to make a clean source package.' }

if (-not $SkipLauncherBuild) {
    Write-Step 'Build static source launcher'
    $target = Join-Path $LauncherBuildDirectory 'cargo'
    & cargo build --locked --release --manifest-path (Join-Path $Root 'Cargo.toml') -p lan-chat-launcher --target-dir $target
    if ($LASTEXITCODE -ne 0) { throw "Source launcher build failed with exit code $LASTEXITCODE." }
    Copy-Item -LiteralPath (Join-Path $target 'release\lan-chat-launcher.exe') -Destination (Join-Path $LauncherBuildDirectory 'LANChat-Launcher.exe') -Force
}

$launcherSource = Join-Path $LauncherBuildDirectory 'LANChat-Launcher.exe'
Require-File $launcherSource 'Compiled LANChat-Launcher.exe'

if (Test-Path -LiteralPath $ReleaseDirectory) {
    Remove-Item -LiteralPath $ReleaseDirectory -Recurse -Force
}
if (Test-Path -LiteralPath $ArchivePath -PathType Leaf) {
    Remove-Item -LiteralPath $ArchivePath -Force
}
New-Item -ItemType Directory -Force -Path $ReleaseDirectory | Out-Null

Write-Step 'Copy tracked source files'
$trackedFiles = @(& $git.Source -C $Root ls-files)
if ($LASTEXITCODE -ne 0 -or $trackedFiles.Count -eq 0) {
    throw 'Unable to read tracked repository files for the source package.'
}
foreach ($relativePath in $trackedFiles) {
    $sourcePath = Join-Path $Root $relativePath
    Require-File $sourcePath "Tracked source file '$relativePath'"
    $targetPath = Join-Path $ReleaseDirectory $relativePath
    $targetDirectory = Split-Path -Parent $targetPath
    New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
}
Copy-Item -LiteralPath $launcherSource -Destination (Join-Path $ReleaseDirectory 'LANChat-Launcher.exe') -Force

Write-Step 'Validate source package security boundary'
Test-SourcePackageDirectory $ReleaseDirectory

Write-Step 'Verify launcher package root discovery'
$probe = Start-Process -FilePath (Join-Path $ReleaseDirectory 'LANChat-Launcher.exe') -ArgumentList '--dry-run' -WindowStyle Hidden -Wait -PassThru
if ($probe.ExitCode -ne 0) {
    throw 'Source launcher dry-run did not find scripts\\bootstrap-github.ps1 in the package root.'
}

Write-Step 'Create ZIP archive'
Compress-Archive -Path (Join-Path $ReleaseDirectory '*') -DestinationPath $ArchivePath -CompressionLevel Optimal -Force
Require-File $ArchivePath 'Source launcher ZIP archive'
Test-SourceArchive $ArchivePath

Write-Host "Source package directory: $ReleaseDirectory" -ForegroundColor Green
Write-Host "Source package archive:    $ArchivePath" -ForegroundColor Green
Write-Host 'Security: this package contains tracked source and the launcher only; no host certificate, private key, database, build output, or local dependency cache is included.' -ForegroundColor Green
