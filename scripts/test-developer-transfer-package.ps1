[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory,
    [string]$ArchivePath = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Require-File([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Require-Directory([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
        throw "$Description was not found: $Path"
    }
}

$PackageDirectory = [System.IO.Path]::GetFullPath($PackageDirectory)
Require-Directory $PackageDirectory 'Developer transfer package directory'

foreach ($required in @(
    'README-TRANSFER.md',
    'LANChat-Source\README.md',
    'LANChat-Source\LANChat-Launcher.exe',
    'LANChat-Source\docs\CODEX_HANDOFF.md',
    'git-history.bundle',
    'Codex-optional\AGENTS.md',
    'worktree-snapshots\README.md',
    'worktree-snapshots\codex-06d8\working.patch',
    'worktree-snapshots\remember-connection\working.patch'
)) {
    Require-File (Join-Path $PackageDirectory $required) "Developer transfer required file '$required'"
}

Require-Directory (Join-Path $PackageDirectory 'Codex-optional\skills') 'Optional Codex skills directory'
$skillFiles = @(Get-ChildItem -LiteralPath (Join-Path $PackageDirectory 'Codex-optional\skills') -Filter 'SKILL.md' -Recurse -File)
if ($skillFiles.Count -eq 0) {
    throw 'Developer transfer package does not contain any optional Codex skills.'
}

$forbidden = @(
    Get-ChildItem -LiteralPath $PackageDirectory -Recurse -Force | Where-Object {
        $_.Name -in @('auth.json', 'config.toml') -or
        $_.Name -match '(?i)(\.key$|\.pem$|\.crt$|\.db$)' -or
        $_.FullName -match '(?i)[\\/](\.git|\.tools|node_modules|build-[^\\/]+|sessions|memories|logs|attachments|generated_images)[\\/]'
    }
)
if ($forbidden.Count -gt 0) {
    throw "Developer transfer package contains forbidden local or sensitive content: $($forbidden.FullName -join '; ')"
}

if (-not [string]::IsNullOrWhiteSpace($ArchivePath)) {
    $ArchivePath = [System.IO.Path]::GetFullPath($ArchivePath)
    Require-File $ArchivePath 'Developer transfer ZIP archive'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($ArchivePath)
    try {
        $entries = @($zip.Entries | ForEach-Object { $_.FullName.Replace('\', '/') })
        foreach ($required in @(
            'README-TRANSFER.md',
            'LANChat-Source/docs/CODEX_HANDOFF.md',
            'git-history.bundle',
            'Codex-optional/AGENTS.md',
            'worktree-snapshots/README.md'
        )) {
            if ($entries -notcontains $required) {
                throw "Developer transfer ZIP is missing required file: $required"
            }
        }

        $forbiddenEntries = @($entries | Where-Object {
            $_ -match '(?i)(^|/)(auth\.json|config\.toml)$' -or
            $_ -match '(?i)(\.key$|\.pem$|\.crt$|\.db$)' -or
            $_ -match '(?i)(^|/)(\.git|\.tools|node_modules|build-[^/]+|sessions|memories|logs|attachments|generated_images)/'
        })
        if ($forbiddenEntries.Count -gt 0) {
            throw "Developer transfer ZIP contains forbidden local or sensitive content: $($forbiddenEntries -join '; ')"
        }
    } finally {
        $zip.Dispose()
    }
}

Write-Host "Developer transfer package verification passed: $PackageDirectory" -ForegroundColor Green
