[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageDirectory,
    [switch]$Smoke,
    [switch]$AllowLocalHost
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
        throw "Runtime package is missing required file: $RelativePath"
    }
}

function Stop-SmokeRuntime([string]$Directory) {
    $resolvedDirectory = [System.IO.Path]::GetFullPath($Directory).TrimEnd([System.IO.Path]::DirectorySeparatorChar)
    $prefix = $resolvedDirectory + [System.IO.Path]::DirectorySeparatorChar
    $processes = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)
    })
    foreach ($runtimeProcess in $processes) {
        Stop-Process -Id $runtimeProcess.ProcessId -Force -ErrorAction SilentlyContinue
    }
    foreach ($runtimeProcess in $processes) {
        Wait-Process -Id $runtimeProcess.ProcessId -Timeout 3 -ErrorAction SilentlyContinue
    }
}

foreach ($relativePath in @(
    'LANChat.exe',
    'LANChat.ico',
    'lan-chat-gui.exe',
    'Qt6Core.dll',
    'Qt6WebChannel.dll',
    'Qt6WebEngineCore.dll',
    'Qt6WebEngineWidgets.dll',
    'QtWebEngineProcess.exe',
    'platforms\qwindows.dll',
    'resources\icudtl.dat',
    'resources\qtwebengine_resources.pak',
    'resources\v8_context_snapshot.bin'
)) {
    Require-File $relativePath
}

if ($AllowLocalHost.IsPresent) {
    Require-File 'server-go\chat-server.exe'
    Require-File 'README.md'
} else {
    Require-File 'README-member.md'
}

foreach ($pattern in @('libssl-*.dll', 'libcrypto-*.dll', 'msvcp140*.dll', 'vcruntime140*.dll')) {
    $searchDirectory = if ($pattern -like 'libssl-*' -or $pattern -like 'libcrypto-*') {
        Join-Path $PackageDirectory 'openssl'
    } else {
        $PackageDirectory
    }
    if (@(Get-ChildItem -LiteralPath $searchDirectory -Filter $pattern -File -ErrorAction SilentlyContinue).Count -eq 0) {
        throw "Runtime package is missing runtime matching: $pattern"
    }
}

$localesDirectory = Join-Path $PackageDirectory 'translations\qtwebengine_locales'
if (@(Get-ChildItem -LiteralPath $localesDirectory -Filter '*.pak' -File -ErrorAction SilentlyContinue).Count -eq 0) {
    throw 'Runtime package is missing Qt WebEngine locales.'
}

foreach ($forbiddenDirectory in @('.git', 'frontend', 'client-cpp', 'scripts', 'tools', '.tools', 'out', 'release',
                                  'logs', 'log', 'test-results', 'test-output', 'coverage', 'attachments',
                                  'attachment-cache', '__host-data', '.cache')) {
    if (Test-Path -LiteralPath (Join-Path $PackageDirectory $forbiddenDirectory)) {
        throw "Runtime package contains forbidden directory: $forbiddenDirectory"
    }
}
$nestedForbiddenDirectories = @(Get-ChildItem -LiteralPath $PackageDirectory -Recurse -Directory -Force |
    Where-Object {
        $_.Name -in @('logs', 'log', 'test-results', 'test-output', 'coverage', 'attachments',
                      'attachment-cache', '__host-data', '.cache')
    })
if ($nestedForbiddenDirectories.Count -gt 0) {
    throw "Runtime package contains forbidden data directories: $($nestedForbiddenDirectories.FullName -join '; ')"
}
if (-not $AllowLocalHost.IsPresent -and (Test-Path -LiteralPath (Join-Path $PackageDirectory 'server-go'))) {
    throw 'Member package contains forbidden directory: server-go'
}
if ($AllowLocalHost.IsPresent -and (Test-Path -LiteralPath (Join-Path $PackageDirectory 'server-go\certs'))) {
    throw 'Unified runtime package must not contain a host certificate directory.'
}

$forbiddenFiles = @(Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File |
    Where-Object {
        $_.Name -match '(?i)(\.key$|\.pem$|\.crt$|\.cer$|\.der$|\.pfx$|\.p12$|\.db(?:[-.]|$)|\.sqlite(?:[-.]|$)|\.sqlite3(?:[-.]|$)|\.log(?:[-.]|$)|\.test$|\.tap$|\.trx$|\.junit$)' -or
        ($_.Name -eq 'chat-server.exe' -and -not $AllowLocalHost.IsPresent) -or
        $_.Name -eq 'LANChat-Launcher.exe' -or
        $_.Name -eq 'node.exe' -or $_.Name -eq 'npm.cmd' -or $_.Name -eq 'pnpm.cmd' -or $_.Name -eq 'go.exe'
    })
if ($forbiddenFiles.Count -gt 0) {
    $names = ($forbiddenFiles | ForEach-Object { $_.FullName }) -join '; '
    throw "Runtime package contains forbidden files: $names"
}

if ($Smoke) {
    $smokeDirectory = Join-Path $env:TEMP ('LANChat-package-smoke-' + [Guid]::NewGuid().ToString('N'))
    $previousPath = $env:PATH
    $previousHostDataRoot = $env:LAN_CHAT_TEST_HOST_DATA_ROOT
    $launcherProcess = $null
    try {
        New-Item -ItemType Directory -Force -Path $smokeDirectory | Out-Null
        Copy-Item -Path (Join-Path $PackageDirectory '*') -Destination $smokeDirectory -Recurse -Force
        $launcherExe = Join-Path $smokeDirectory 'LANChat.exe'
        $guiExe = Join-Path $smokeDirectory 'lan-chat-gui.exe'
        # Deliberately omit the developer Qt path: the packaged directory must
        # provide every application runtime dependency by itself.
        $env:PATH = "$smokeDirectory;$env:SystemRoot\System32;$env:SystemRoot"
        $hostDataDirectory = Join-Path $smokeDirectory '__host-data'
        $env:LAN_CHAT_TEST_HOST_DATA_ROOT = $hostDataDirectory
        $launcherProcess = Start-Process -FilePath $launcherExe -WorkingDirectory $smokeDirectory -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 3000
        $guiProcess = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
            $_.ExecutablePath -and $_.ExecutablePath.Equals($guiExe, [System.StringComparison]::OrdinalIgnoreCase)
        } | Select-Object -First 1
        if ($null -eq $guiProcess) {
            throw 'Runtime entry point did not start the modern GUI during smoke test.'
        }
        if ($AllowLocalHost.IsPresent) {
            foreach ($relativePath in @(
                'certs\server-lan.crt',
                'certs\server-lan.key',
                'chat.db'
            )) {
                if (-not (Test-Path -LiteralPath (Join-Path $hostDataDirectory $relativePath) -PathType Leaf)) {
                    throw "First-run local host initialization did not create: $relativePath"
                }
            }
        }
    } finally {
        $env:PATH = $previousPath
        if ($null -eq $previousHostDataRoot) {
            Remove-Item Env:LAN_CHAT_TEST_HOST_DATA_ROOT -ErrorAction SilentlyContinue
        } else {
            $env:LAN_CHAT_TEST_HOST_DATA_ROOT = $previousHostDataRoot
        }
        if (Test-Path -LiteralPath $smokeDirectory -PathType Container) {
            $resolvedSmokeDirectory = (Resolve-Path -LiteralPath $smokeDirectory).Path
            $tempRoot = (Resolve-Path -LiteralPath $env:TEMP).Path
            if ($resolvedSmokeDirectory.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
                (Split-Path -Leaf $resolvedSmokeDirectory) -like 'LANChat-package-smoke-*') {
                Stop-SmokeRuntime $resolvedSmokeDirectory
                $removed = $false
                for ($attempt = 0; $attempt -lt 8; $attempt++) {
                    try {
                        Remove-Item -LiteralPath $resolvedSmokeDirectory -Recurse -Force -ErrorAction Stop
                        $removed = $true
                        break
                    } catch {
                        Start-Sleep -Milliseconds 250
                    }
                }
                if (-not $removed -and (Test-Path -LiteralPath $resolvedSmokeDirectory -PathType Container)) {
                    throw "Unable to remove temporary runtime smoke directory: $resolvedSmokeDirectory"
                }
            }
        }
    }
}

if ($AllowLocalHost.IsPresent) {
    Write-Host "Unified runtime package validation passed: $PackageDirectory" -ForegroundColor Green
} else {
    Write-Host "Member package validation passed: $PackageDirectory" -ForegroundColor Green
}
