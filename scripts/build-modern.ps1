[CmdletBinding()]
param(
    [ValidateSet('Build', 'Test', 'Launch')]
    [string]$Action = 'Launch',
    [string]$BuildDirectory = '',
    [string]$QtPrefix = $env:LAN_CHAT_QT_PREFIX,
    [string]$OpenSslRoot = $env:LAN_CHAT_OPENSSL_ROOT,
    [switch]$Wait,
    [switch]$CheckOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$FrontendDirectory = Join-Path $Root 'frontend'
$ServerDirectory = Join-Path $Root 'server-go'
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $Root 'out\modern-msvc-x64'
}

function Write-Step([string]$Message) {
    Write-Host "[LAN Chat] $Message" -ForegroundColor Cyan
}

function Find-CommandPath([string]$Name, [string[]]$ExtraPaths = @()) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) { return $command.Source }
    foreach ($candidate in $ExtraPaths) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    }
    return ''
}

function Test-DependencyMarker([string]$LockFile, [string]$MarkerFile) {
    if (-not (Test-Path -LiteralPath $MarkerFile -PathType Leaf)) { return $false }
    $expectedHash = (Get-FileHash -LiteralPath $LockFile -Algorithm SHA256).Hash
    $actualHash = (Get-Content -LiteralPath $MarkerFile -Raw).Trim()
    return $actualHash -eq $expectedHash
}

function Set-DependencyMarker([string]$LockFile, [string]$MarkerFile) {
    $markerDirectory = Split-Path -Parent $MarkerFile
    New-Item -ItemType Directory -Force -Path $markerDirectory | Out-Null
    (Get-FileHash -LiteralPath $LockFile -Algorithm SHA256).Hash | Set-Content -LiteralPath $MarkerFile -Encoding ASCII
}

function Find-VsDevCmd {
    $programFilesX86 = ${env:ProgramFiles(x86)}
    $vsWhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vsWhere -PathType Leaf) {
        $installation = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not [string]::IsNullOrWhiteSpace($installation)) {
            $candidate = Join-Path $installation.Trim() 'Common7\Tools\VsDevCmd.bat'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
        }
    }
    $fallback = Join-Path $programFilesX86 'Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
    if (Test-Path -LiteralPath $fallback -PathType Leaf) { return $fallback }
    return ''
}

function Find-VsCmakeTool([string]$VsDevCmd, [string]$RelativePath) {
    if ([string]::IsNullOrWhiteSpace($VsDevCmd)) { return '' }
    $toolsDirectory = Split-Path -Parent $VsDevCmd
    $commonDirectory = Split-Path -Parent $toolsDirectory
    $installationDirectory = Split-Path -Parent $commonDirectory
    $candidate = Join-Path $installationDirectory $RelativePath
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    return ''
}

function Test-QtPrefix([string]$Prefix) {
    if ([string]::IsNullOrWhiteSpace($Prefix)) { return $false }
    $required = @(
        'lib\cmake\Qt6\Qt6Config.cmake',
        'lib\cmake\Qt6WebEngineWidgets\Qt6WebEngineWidgetsConfig.cmake',
        'lib\cmake\Qt6WebChannel\Qt6WebChannelConfig.cmake',
        'bin\windeployqt.exe'
    )
    $missingQtFiles = @(
        $required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Prefix $_) -PathType Leaf) }
    )
    return $missingQtFiles.Count -eq 0
}

function Find-QtPrefix([string]$RequestedPrefix) {
    $candidates = @(
        $RequestedPrefix,
        $env:LAN_CHAT_QT_PREFIX,
        (Join-Path $Root '.tools\qt\6.11.2\msvc2022_64'),
        'C:\Qt\6.11.2\msvc2022_64'
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    foreach ($candidate in $candidates) {
        if (Test-QtPrefix $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    return ''
}

function Test-OpenSslRoot([string]$Prefix) {
    if ([string]::IsNullOrWhiteSpace($Prefix)) { return $false }
    return (Test-Path -LiteralPath (Join-Path $Prefix 'include\openssl\ssl.h') -PathType Leaf) -and
           (Test-Path -LiteralPath (Join-Path $Prefix 'bin') -PathType Container)
}

function Find-OpenSslRoot([string]$RequestedRoot) {
    $candidates = @(
        $RequestedRoot,
        $env:LAN_CHAT_OPENSSL_ROOT,
        (Join-Path $Root '.tools\vcpkg\installed\x64-windows'),
        (Join-Path $Root '.tools\vcpkg\installed\x64-windows-static')
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    foreach ($candidate in $candidates) {
        if (Test-OpenSslRoot $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    return ''
}

function Quote-CmdArgument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Get-ShowIncludesPrefix([string]$VsDevCmd) {
    # MSVC localizes /showIncludes independently from PowerShell's UI culture.
    # Probe the actual compiler, then give its prefix to CMake so dependency
    # lines are consumed instead of flooding the first-build console.
    $fallback = 'Note: including file: '
    if ([string]::IsNullOrWhiteSpace($VsDevCmd)) { return $fallback }

    $probeDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('lan-chat-showincludes-' + [Guid]::NewGuid().ToString('N'))
    $sourceFile = Join-Path $probeDirectory 'probe.cpp'
    $objectFile = Join-Path $probeDirectory 'probe.obj'
    try {
        New-Item -ItemType Directory -Force -Path $probeDirectory | Out-Null
        [System.IO.File]::WriteAllText($sourceFile, "#include <cstddef>`r`nint main() { return 0; }`r`n")
        $commandLine = ('set VSLANG=1033 && call {0} -arch=x64 -host_arch=x64 && cl.exe /nologo /showIncludes /c {1} /Fo{2}' -f (Quote-CmdArgument $VsDevCmd), (Quote-CmdArgument $sourceFile), (Quote-CmdArgument $objectFile))
        $compilerOutput = @(& cmd.exe /d /s /c $commandLine 2>&1)
        if ($LASTEXITCODE -ne 0) { return $fallback }

        foreach ($line in $compilerOutput) {
            $match = [regex]::Match([string]$line, '^(?<prefix>.+?:\s+)(?:[A-Za-z]:\\)')
            if ($match.Success) { return $match.Groups['prefix'].Value }
        }
    } catch {
        return $fallback
    } finally {
        if (Test-Path -LiteralPath $probeDirectory -PathType Container) {
            Remove-Item -LiteralPath $probeDirectory -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    return $fallback
}

function Invoke-VsCmake([string]$VsDevCmd, [string]$Cmake, [string[]]$Arguments, [string]$ShowIncludesPrefix = '') {
    $quotedArguments = ($Arguments | ForEach-Object { Quote-CmdArgument $_ }) -join ' '
    $commandLine = ('set VSLANG=1033 && call {0} -arch=x64 -host_arch=x64 && {1} {2}' -f (Quote-CmdArgument $VsDevCmd), (Quote-CmdArgument $Cmake), $quotedArguments)
    if (-not [string]::IsNullOrWhiteSpace($ShowIncludesPrefix)) {
        $prefix = $ShowIncludesPrefix.TrimEnd()
        & cmd.exe /d /s /c $commandLine 2>&1 | ForEach-Object {
            $line = [string]$_
            if (-not $line.TrimStart().StartsWith($prefix, [System.StringComparison]::Ordinal)) {
                Write-Host $line
            }
        }
        if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE." }
        return
    }
    & cmd.exe /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) { throw "CMake failed with exit code $LASTEXITCODE." }
}

$vsDevCmd = Find-VsDevCmd
$vsCmake = Find-VsCmakeTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$vsNinja = Find-VsCmakeTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$cmake = if (-not [string]::IsNullOrWhiteSpace($vsCmake)) { $vsCmake } else { Find-CommandPath 'cmake.exe' }
$ninja = if (-not [string]::IsNullOrWhiteSpace($vsNinja)) { $vsNinja } else { Find-CommandPath 'ninja.exe' }
$node = Find-CommandPath 'node.exe' @('C:\Program Files\nodejs\node.exe')
$pnpmUserDirectory = [Environment]::GetFolderPath([Environment+SpecialFolder]::ApplicationData)
$pnpm = Find-CommandPath 'pnpm.cmd' @(
    (Join-Path $pnpmUserDirectory 'npm\pnpm.cmd'),
    (Join-Path $Root '.tools\pnpm\pnpm.cmd')
)
$bundledGo = Join-Path $Root '.tools\go1.25.5\go\bin\go.exe'
$go = if (Test-Path -LiteralPath $bundledGo -PathType Leaf) {
    $bundledGo
} else {
    Find-CommandPath 'go.exe' @('C:\Program Files\Go\bin\go.exe')
}
$resolvedQtPrefix = Find-QtPrefix $QtPrefix
$resolvedOpenSslRoot = Find-OpenSslRoot $OpenSslRoot

$requirements = [ordered]@{
    'Node.js LTS (node.exe)' = $node
    'pnpm package manager (pnpm.cmd)' = $pnpm
    'Go toolchain (go.exe)' = $go
    'CMake (cmake.exe)' = $cmake
    'Ninja (ninja.exe)' = $ninja
    'MSVC Build Tools x64' = $vsDevCmd
    'Qt 6.11.2 MSVC + WebEngine + WebChannel' = $resolvedQtPrefix
    'OpenSSL x64 (vcpkg)' = $resolvedOpenSslRoot
}
$missing = @($requirements.GetEnumerator() | Where-Object { [string]::IsNullOrWhiteSpace($_.Value) } | ForEach-Object { $_.Key })

if ($CheckOnly) {
    foreach ($item in $requirements.GetEnumerator()) {
        $state = if ([string]::IsNullOrWhiteSpace($item.Value)) { 'MISSING' } else { $item.Value }
        Write-Host ('{0}: {1}' -f $item.Key, $state)
    }
    if ($missing.Count -gt 0) { throw "LAN_CHAT_TOOLCHAIN_INCOMPLETE: $($missing -join '; ')" }
    return
}

if ($missing.Count -gt 0) {
    throw "Missing build dependencies: $($missing -join '; '). Run scripts\\bootstrap-github.ps1 from the source package."
}

$showIncludesPrefix = Get-ShowIncludesPrefix $vsDevCmd

$dependencyMarker = Join-Path $FrontendDirectory 'node_modules\.lan-chat-pnpm-lock.sha256'
$dependencyInput = Join-Path $FrontendDirectory 'pnpm-lock.yaml'
if (-not (Test-DependencyMarker $dependencyInput $dependencyMarker)) {
    Write-Step 'Install React dependencies (pnpm install)'
    & $pnpm --dir $FrontendDirectory install --frozen-lockfile
    if ($LASTEXITCODE -ne 0) { throw "pnpm install failed with exit code $LASTEXITCODE." }
    Set-DependencyMarker $dependencyInput $dependencyMarker
}
Write-Step 'Build React interface (pnpm run build)'
& $pnpm --dir $FrontendDirectory run build
if ($LASTEXITCODE -ne 0) { throw "pnpm run build failed with exit code $LASTEXITCODE." }

Write-Step 'Build Go server'
$goBuildCache = Join-Path $Root '.tools\go-build-cache'
$goModuleCache = Join-Path $Root '.tools\go-module-cache'
New-Item -ItemType Directory -Force -Path $goBuildCache, $goModuleCache | Out-Null
$previousGoCache = $env:GOCACHE
$previousGoModuleCache = $env:GOMODCACHE
$env:GOCACHE = $goBuildCache
$env:GOMODCACHE = $goModuleCache
Push-Location $ServerDirectory
try {
    & $go build -o (Join-Path $ServerDirectory 'chat-server.exe') .
    if ($LASTEXITCODE -ne 0) { throw "go build failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
    $env:GOCACHE = $previousGoCache
    $env:GOMODCACHE = $previousGoModuleCache
}

New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null
$guiSource = Join-Path $Root 'client-cpp\gui'
$configureArguments = @(
    '-S', $guiSource,
    '-B', $BuildDirectory,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_PREFIX_PATH=$resolvedQtPrefix",
    "-DOPENSSL_ROOT_DIR=$resolvedOpenSslRoot",
    '-DOPENSSL_USE_STATIC_LIBS=FALSE',
    '-DLAN_CHAT_ENABLE_WEB_UI=ON',
    '-DLAN_CHAT_ENABLE_PERF_OVERLAY=OFF'
)
Write-Step "Configure modern Qt WebEngine client: $BuildDirectory"
Invoke-VsCmake $vsDevCmd $cmake $configureArguments

Write-Step 'Build modern client'
if ($Action -eq 'Test') {
    Invoke-VsCmake $vsDevCmd $cmake @('--build', $BuildDirectory, '--parallel', '4') $showIncludesPrefix
} else {
    Invoke-VsCmake $vsDevCmd $cmake @('--build', $BuildDirectory, '--target', 'lan-chat-gui', '--parallel', '4') $showIncludesPrefix
}

if ($Action -eq 'Test') {
    Write-Step 'Run CTest'
    $ctest = Find-CommandPath 'ctest.exe'
    if ([string]::IsNullOrWhiteSpace($ctest)) { throw 'ctest.exe was not found.' }
    $ctestArguments = @('--test-dir', $BuildDirectory, '--output-on-failure')
    $quotedArguments = ($ctestArguments | ForEach-Object { Quote-CmdArgument $_ }) -join ' '
    $testCommand = ('set VSLANG=1033 && call {0} -arch=x64 -host_arch=x64 && {1} {2}' -f (Quote-CmdArgument $vsDevCmd), (Quote-CmdArgument $ctest), $quotedArguments)
    & cmd.exe /d /s /c $testCommand
    if ($LASTEXITCODE -ne 0) { throw "CTest failed with exit code $LASTEXITCODE." }
}

$manifest = [ordered]@{
    format = 1
    builtAt = (Get-Date).ToUniversalTime().ToString('o')
    gui = (Join-Path $BuildDirectory 'lan-chat-gui.exe')
    qtPrefix = $resolvedQtPrefix
    openSslRoot = $resolvedOpenSslRoot
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $BuildDirectory 'lan-chat-build.json') -Encoding UTF8

if ($Action -eq 'Launch') {
    $guiExe = Join-Path $BuildDirectory 'lan-chat-gui.exe'
    if (-not (Test-Path -LiteralPath $guiExe -PathType Leaf)) { throw "Modern client was not generated: $guiExe" }
    Write-Step 'Launch modern client'
    $runtimeDirectories = @(
        (Join-Path $resolvedQtPrefix 'bin'),
        (Join-Path $resolvedOpenSslRoot 'bin')
    ) | Where-Object { Test-Path -LiteralPath $_ -PathType Container }
    $previousPath = $env:PATH
    try {
        $env:PATH = (($runtimeDirectories + $previousPath) -join ';')
        $process = Start-Process -FilePath $guiExe -WorkingDirectory $BuildDirectory -PassThru
        if ($Wait) {
            $process.WaitForExit()
            exit $process.ExitCode
        }
    } finally {
        $env:PATH = $previousPath
    }
}
