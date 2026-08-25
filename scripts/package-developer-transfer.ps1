[CmdletBinding()]
param(
    [string]$SourcePackageDirectory = '',
    [string]$OutputDirectory = '',
    [string]$CodexRoot = '',
    [string]$Stage9Worktree = '',
    [string]$RememberConnectionWorktree = '',
    [switch]$SkipSourcePackageBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = [System.IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$ReleaseRoot = [System.IO.Path]::GetFullPath((Join-Path $Root 'release'))
if ([string]::IsNullOrWhiteSpace($SourcePackageDirectory)) { $SourcePackageDirectory = Join-Path $ReleaseRoot 'LANChat-Source-Launcher-windows-x64' }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $ReleaseRoot 'LANChat-Developer-Transfer-windows-x64' }
if ([string]::IsNullOrWhiteSpace($CodexRoot)) { $CodexRoot = Join-Path $env:USERPROFILE '.codex' }
if ([string]::IsNullOrWhiteSpace($Stage9Worktree)) { $Stage9Worktree = Join-Path $CodexRoot 'worktrees\06d8\chat_X' }
if ([string]::IsNullOrWhiteSpace($RememberConnectionWorktree)) { $RememberConnectionWorktree = Join-Path $Root '.worktrees\remember-connection' }

$SourcePackageDirectory = [System.IO.Path]::GetFullPath($SourcePackageDirectory)
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$CodexRoot = [System.IO.Path]::GetFullPath($CodexRoot)
$Stage9Worktree = [System.IO.Path]::GetFullPath($Stage9Worktree)
$RememberConnectionWorktree = [System.IO.Path]::GetFullPath($RememberConnectionWorktree)
$ArchivePath = "$OutputDirectory.zip"

function Write-Step([string]$Message) { Write-Host "[LAN Chat developer transfer] $Message" -ForegroundColor Cyan }
function Require-File([string]$Path, [string]$Description) { if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Description was not found: $Path" } }
function Require-Directory([string]$Path, [string]$Description) { if (-not (Test-Path -LiteralPath $Path -PathType Container)) { throw "$Description was not found: $Path" } }
function Require-UnderRelease([string]$Path, [string]$Description) {
    $releasePrefix = $ReleaseRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $Path.StartsWith($releasePrefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw "$Description must stay under the repository release directory: $ReleaseRoot" }
}
function Test-SafePathName([string]$Path, [string]$Description) {
    if ($Path -match '(?i)(\.key$|\.pem$|\.crt$|\.db$|(^|[\\/])(auth\.json|config\.toml)$|token|credential)') { throw "$Description contains a forbidden sensitive path: $Path" }
}
function Copy-FileToSnapshot([string]$SourceRoot, [string]$RelativePath, [string]$DestinationRoot) {
    $sourcePath = Join-Path $SourceRoot $RelativePath
    Require-File $sourcePath "Snapshot source '$RelativePath'"
    Test-SafePathName $RelativePath "Snapshot source '$RelativePath'"
    $targetPath = Join-Path $DestinationRoot $RelativePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $targetPath) | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $targetPath -Force
}
function Save-WorktreePatch([string]$Worktree, [string]$Destination, [string]$Name) {
    Require-Directory $Worktree "$Name worktree"
    $head = @(& git.exe -C $Worktree rev-parse HEAD)
    if ($LASTEXITCODE -ne 0 -or $head.Count -ne 1) { throw "Unable to read HEAD for $Name worktree: $Worktree" }
    $patch = @(& git.exe -C $Worktree diff --binary)
    if ($LASTEXITCODE -ne 0) { throw "Unable to create the tracked-file patch for $Name worktree." }
    $status = @(& git.exe -C $Worktree status --short)
    if ($LASTEXITCODE -ne 0) { throw "Unable to read status for $Name worktree." }
    Set-Content -LiteralPath (Join-Path $Destination 'base-commit.txt') -Value $head[0] -Encoding utf8
    Set-Content -LiteralPath (Join-Path $Destination 'working.patch') -Value $patch -Encoding utf8
    Set-Content -LiteralPath (Join-Path $Destination 'status.txt') -Value $status -Encoding utf8
}

Require-UnderRelease $SourcePackageDirectory 'SourcePackageDirectory'
Require-UnderRelease $OutputDirectory 'OutputDirectory'
Require-Directory $CodexRoot 'Codex profile root'
Require-Directory $Stage9Worktree 'Stage 9 worktree'
Require-Directory $RememberConnectionWorktree 'Remember-connection worktree'
$git = Get-Command git.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $git) { throw 'git.exe is required to create a developer transfer package.' }

Write-Step 'Check complete Git history for local secrets'
$historyPaths = @(& $git.Source -C $Root log --all --pretty=format: --name-only)
if ($LASTEXITCODE -ne 0) { throw 'Unable to read Git history for the transfer-package safety check.' }
$unsafeHistoryPaths = @($historyPaths | Where-Object { $_ -match '(?i)(\.key$|\.pem$|\.crt$|\.db$|(^|[\\/])(auth\.json|config\.toml)$|token|credential)' })
if ($unsafeHistoryPaths.Count -gt 0) { throw "Git bundle was not created because repository history contains potentially sensitive paths: $($unsafeHistoryPaths -join '; ')" }

if (-not $SkipSourcePackageBuild) {
    Write-Step 'Refresh the clean source developer package'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\package-source-launcher.ps1') -ReleaseDirectory $SourcePackageDirectory
    if ($LASTEXITCODE -ne 0) { throw "Source developer package build failed with exit code $LASTEXITCODE." }
}
Require-Directory $SourcePackageDirectory 'Clean source developer package'

if (Test-Path -LiteralPath $OutputDirectory) { Remove-Item -LiteralPath $OutputDirectory -Recurse -Force }
if (Test-Path -LiteralPath $ArchivePath -PathType Leaf) { Remove-Item -LiteralPath $ArchivePath -Force }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

Write-Step 'Copy clean source package'
Copy-Item -LiteralPath $SourcePackageDirectory -Destination (Join-Path $OutputDirectory 'LANChat-Source') -Recurse -Force

Write-Step 'Create verifiable Git history bundle'
$bundlePath = Join-Path $OutputDirectory 'git-history.bundle'
& $git.Source -C $Root bundle create $bundlePath --all
if ($LASTEXITCODE -ne 0) { throw "git bundle create failed with exit code $LASTEXITCODE." }
& $git.Source -C $Root bundle verify $bundlePath
if ($LASTEXITCODE -ne 0) { throw "git bundle verify failed with exit code $LASTEXITCODE." }

Write-Step 'Copy safe optional Codex rules and user-installed skills'
$codexDestination = Join-Path $OutputDirectory 'Codex-optional'
New-Item -ItemType Directory -Force -Path $codexDestination | Out-Null
Copy-FileToSnapshot $CodexRoot 'AGENTS.md' $codexDestination
$skillsRoot = Join-Path $CodexRoot 'skills'
Require-Directory $skillsRoot 'Codex skills directory'
$skillsDestination = Join-Path $codexDestination 'skills'
New-Item -ItemType Directory -Force -Path $skillsDestination | Out-Null
foreach ($skillDirectory in @(Get-ChildItem -LiteralPath $skillsRoot -Directory | Where-Object { $_.Name -ne '.system' })) {
    $unsafeSkillFiles = @(Get-ChildItem -LiteralPath $skillDirectory.FullName -Recurse -Force | Where-Object { $_.Name -match '(?i)(\.key$|\.pem$|\.crt$|\.db$|auth\.json|config\.toml|token|credential)' })
    if ($unsafeSkillFiles.Count -gt 0) { throw "Optional Codex skill '$($skillDirectory.Name)' was not copied because it contains potentially sensitive local content: $($unsafeSkillFiles.FullName -join '; ')" }
    Copy-Item -LiteralPath $skillDirectory.FullName -Destination (Join-Path $skillsDestination $skillDirectory.Name) -Recurse -Force
}

Write-Step 'Snapshot uncommitted external worktree changes without build caches'
$snapshotsRoot = Join-Path $OutputDirectory 'worktree-snapshots'
$stage9Destination = Join-Path $snapshotsRoot 'codex-06d8'
$rememberDestination = Join-Path $snapshotsRoot 'remember-connection'
New-Item -ItemType Directory -Force -Path $stage9Destination, $rememberDestination | Out-Null
Save-WorktreePatch $Stage9Worktree $stage9Destination 'codex-06d8'
foreach ($relativePath in @('client-cpp\CMakeLists.txt', 'client-cpp\include\command.hpp', 'client-cpp\src\command.cpp', 'client-cpp\tests\command_tests.cpp', 'docs\stage9-lan-test.md')) {
    Copy-FileToSnapshot $Stage9Worktree $relativePath (Join-Path $stage9Destination 'untracked')
}
Save-WorktreePatch $RememberConnectionWorktree $rememberDestination 'remember-connection'

$snapshotReadme = @'
# Worktree snapshots

These snapshots preserve **uncommitted work only** from old worktrees that are
already ancestors of `master`. They are archival material, not a request to
merge stale code automatically.

- `codex-06d8`: tracked changes are in `working.patch`; the five intentional
  untracked C++/documentation files are under `untracked/`. The old Go build
  cache is deliberately excluded.
- `remember-connection`: tracked Go changes are in `working.patch`. Its Qt
  build directories are deliberately excluded.
- The historical `glass-ui` and `gui-polish` worktrees are already represented
  by the Git bundle and are not duplicated. `glass-ui` is additionally excluded
  because it contains a local host private key.

Before applying any patch, create a branch and review it with `git apply --stat`.
Do not apply a patch directly to a current working tree without review.
'@
Set-Content -LiteralPath (Join-Path $snapshotsRoot 'README.md') -Value $snapshotReadme -Encoding utf8

$mainCommit = @(& $git.Source -C $Root rev-parse HEAD)
if ($LASTEXITCODE -ne 0 -or $mainCommit.Count -ne 1) { throw 'Unable to determine the current main commit.' }
$transferReadme = @"
# LAN Chat developer transfer package

Created from commit: `$($mainCommit[0])

## Start on the new PC

1. Extract this archive.
2. Open `LANChat-Source` and read `docs/CODEX_HANDOFF.md` first.
3. Run `LANChat-Launcher.exe`; confirm its first-run toolchain setup when prompted.
4. To recover branch history, create a new repository from `LANChat-Source` and import `git-history.bundle` after reviewing its refs.
5. Optionally copy `Codex-optional/AGENTS.md` and the folders inside `Codex-optional/skills` into the new PC's Codex profile. Do not copy an old Codex configuration or authentication file; sign in again locally.

## Security boundary

This archive intentionally contains no TLS private key, certificate, database, Codex credentials, Git credentials, chat logs, sessions, build output, or local dependency cache. A new host generates its own certificate and private key on first use.

## Old worktree snapshots

`worktree-snapshots/README.md` explains the isolated uncommitted work that was preserved for manual review. It is not part of the supported current build.
"@
Set-Content -LiteralPath (Join-Path $OutputDirectory 'README-TRANSFER.md') -Value $transferReadme -Encoding utf8
$manifest = @"
main_commit=$($mainCommit[0])
source_package=LANChat-Source
git_bundle=git-history.bundle
codex_rules=Codex-optional/AGENTS.md
codex_skills=Codex-optional/skills (excluding .system)
snapshot_codex_06d8=worktree-snapshots/codex-06d8
snapshot_remember_connection=worktree-snapshots/remember-connection
excluded=private keys, certificates, databases, auth/config files, logs, sessions, caches, build output
"@
Set-Content -LiteralPath (Join-Path $OutputDirectory 'transfer-manifest.txt') -Value $manifest -Encoding utf8

Write-Step 'Run directory and ZIP safety checks'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\test-developer-transfer-package.ps1') -PackageDirectory $OutputDirectory
if ($LASTEXITCODE -ne 0) { throw "Developer transfer directory validation failed with exit code $LASTEXITCODE." }
Compress-Archive -Path (Join-Path $OutputDirectory '*') -DestinationPath $ArchivePath -CompressionLevel Optimal -Force
Require-File $ArchivePath 'Developer transfer ZIP archive'
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root 'scripts\test-developer-transfer-package.ps1') -PackageDirectory $OutputDirectory -ArchivePath $ArchivePath
if ($LASTEXITCODE -ne 0) { throw "Developer transfer archive validation failed with exit code $LASTEXITCODE." }

Write-Host "Developer transfer directory: $OutputDirectory" -ForegroundColor Green
Write-Host "Developer transfer archive:   $ArchivePath" -ForegroundColor Green
Write-Host 'Security: excludes private keys, certificates, databases, credentials, sessions, logs, caches, and build output.' -ForegroundColor Green
