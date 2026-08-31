[CmdletBinding()]
param(
    [string]$MlsSourceDir = $env:LAN_CHAT_MLSPP_SOURCE_DIR,
    [string]$QtPrefix = '',
    [string]$OpenSslRoot = '',
    [string]$NlohmannPrefix = '',
    [string]$ChatServerExe = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptRoot = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
    Split-Path -Parent $PSCommandPath
} else {
    $PSScriptRoot
}
$Root = [IO.Path]::GetFullPath((Join-Path $ScriptRoot '..'))
$QtPrefix = if ([string]::IsNullOrWhiteSpace($QtPrefix)) { Join-Path $Root '.tools\qt\6.10.3\msvc2022_64' } else { $QtPrefix }
$OpenSslRoot = if ([string]::IsNullOrWhiteSpace($OpenSslRoot)) { Join-Path $Root '.tools\vcpkg\installed\x64-windows' } else { $OpenSslRoot }
$NlohmannPrefix = if ([string]::IsNullOrWhiteSpace($NlohmannPrefix)) { Join-Path $Root '.tools\vcpkg\installed\x64-windows' } else { $NlohmannPrefix }
$ChatServerExe = if ([string]::IsNullOrWhiteSpace($ChatServerExe)) { Join-Path $Root 'server-go\chat-server.exe' } else { $ChatServerExe }
$GuiSource = Join-Path $Root 'client-cpp\gui'

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

function Find-VsTool([string]$VsDevCmd, [string]$RelativePath) {
    $toolsDirectory = Split-Path -Parent $VsDevCmd
    $commonDirectory = Split-Path -Parent $toolsDirectory
    $installationDirectory = Split-Path -Parent $commonDirectory
    $candidate = Join-Path $installationDirectory $RelativePath
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    return ''
}

function Quote-Cmd([string]$Value) { return '"' + $Value.Replace('"', '\"') + '"' }

function Invoke-VsCmake([string]$VsDevCmd, [string]$Cmake, [string[]]$Arguments) {
    $quoted = ($Arguments | ForEach-Object { Quote-Cmd $_ }) -join ' '
    $line = ('set VSLANG=1033 && set "CMAKE_PREFIX_PATH=" && set "CMAKE_TOOLCHAIN_FILE=" && set "VCPKG_ROOT=" && call {0} -arch=x64 -host_arch=x64 && {1} {2}' -f (Quote-Cmd $VsDevCmd), (Quote-Cmd $Cmake), $quoted)
    & cmd.exe /d /s /c $line
    if ($LASTEXITCODE -ne 0) { throw "CMake command failed with exit code $LASTEXITCODE." }
}

if ([string]::IsNullOrWhiteSpace($MlsSourceDir)) { throw 'LAN_CHAT_MLSPP_SOURCE_DIR is required and must point at the verified locked source.' }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $Root ('out\mls-e2e-fresh-' + [Guid]::NewGuid().ToString('N'))
}
$MlsSourceDir = [IO.Path]::GetFullPath($MlsSourceDir)
$QtPrefix = [IO.Path]::GetFullPath($QtPrefix)
$OpenSslRoot = [IO.Path]::GetFullPath($OpenSslRoot)
$NlohmannPrefix = [IO.Path]::GetFullPath($NlohmannPrefix)
$ChatServerExe = [IO.Path]::GetFullPath($ChatServerExe)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)

if (Test-Path -LiteralPath $OutputRoot) { throw "OutputRoot must be new; refusing to reuse $OutputRoot" }
if (-not (Test-Path -LiteralPath (Join-Path $MlsSourceDir 'CMakeLists.txt') -PathType Leaf)) { throw "MLS++ source is incomplete: $MlsSourceDir" }
if (-not (Test-Path -LiteralPath (Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake') -PathType Leaf)) { throw "Qt 6.10.3 prefix is missing: $QtPrefix" }
if (-not (Test-Path -LiteralPath (Join-Path $OpenSslRoot 'include\openssl\ssl.h') -PathType Leaf)) { throw "OpenSSL 3 headers are missing: $OpenSslRoot" }
if (-not (Test-Path -LiteralPath (Join-Path $OpenSslRoot 'lib\libssl.lib') -PathType Leaf)) { throw "OpenSSL SSL import library is missing: $OpenSslRoot" }
if (-not (Test-Path -LiteralPath (Join-Path $OpenSslRoot 'lib\libcrypto.lib') -PathType Leaf)) { throw "OpenSSL Crypto import library is missing: $OpenSslRoot" }
$nlohmannConfig = Get-ChildItem -LiteralPath $NlohmannPrefix -Filter 'nlohmann_jsonConfig.cmake' -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
if ($null -eq $nlohmannConfig) { throw "nlohmann_jsonConfig.cmake is missing below: $NlohmannPrefix" }
if (-not (Test-Path -LiteralPath $ChatServerExe -PathType Leaf)) { throw "chat-server.exe is missing: $ChatServerExe" }

$vsDevCmd = Find-VsDevCmd
$cmake = Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$ctest = Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
if ([string]::IsNullOrWhiteSpace($vsDevCmd) -or [string]::IsNullOrWhiteSpace($cmake) -or
    [string]::IsNullOrWhiteSpace($ninja) -or [string]::IsNullOrWhiteSpace($ctest)) {
    throw 'x64 VS DevCmd, CMake, Ninja, and CTest are required.'
}

New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$prefixPath = "$QtPrefix;$NlohmannPrefix"
$common = @(
    '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_PREFIX_PATH=$prefixPath",
    "-DOPENSSL_ROOT_DIR=$OpenSslRoot", "-DOPENSSL_INCLUDE_DIR=$OpenSslRoot\include",
    "-DOPENSSL_SSL_LIBRARY=$OpenSslRoot\lib\libssl.lib",
    "-DOPENSSL_CRYPTO_LIBRARY=$OpenSslRoot\lib\libcrypto.lib",
    '-DOPENSSL_USE_STATIC_LIBS=FALSE', '-DLAN_CHAT_ENABLE_WEB_UI=ON', '-DLAN_CHAT_ENABLE_PERF_OVERLAY=OFF'
)
$manifest = Get-Content -Raw (Join-Path $Root 'vendor\mls\manifest.json') | ConvertFrom-Json

foreach ($mode in @('ON', 'OFF')) {
    $build = Join-Path $OutputRoot ('build-' + $mode.ToLowerInvariant())
    $configure = @('-S', $GuiSource, '-B', $build, '--fresh') + $common
    $configure += "-DLAN_CHAT_ENABLE_MLSPP=$mode"
    if ($mode -eq 'ON') { $configure += "-DMLSPP_SOURCE_DIR=$MlsSourceDir" }
    Invoke-VsCmake $vsDevCmd $cmake $configure
    Invoke-VsCmake $vsDevCmd $cmake @('--build', $build, '--parallel', '4')
    # The bridge tests resolve the host relative to the test executable and
    # require the controlled package layout (server-go/chat-server.exe).
    # Copy only the explicitly validated server input; do not rely on PATH.
    $serverDirectory = Join-Path $build 'server-go'
    New-Item -ItemType Directory -Path $serverDirectory -Force | Out-Null
    Copy-Item -LiteralPath $ChatServerExe -Destination (Join-Path $serverDirectory 'chat-server.exe') -Force
    if (-not (Test-Path -LiteralPath (Join-Path $serverDirectory 'chat-server.exe') -PathType Leaf)) {
        throw "Controlled host layout is missing server-go/chat-server.exe in $build"
    }
    & cmd.exe /d /s /c ('set VSLANG=1033 && set "CMAKE_PREFIX_PATH=" && set "CMAKE_TOOLCHAIN_FILE=" && set "VCPKG_ROOT=" && call {0} -arch=x64 -host_arch=x64 && {1} --test-dir {2} -C Debug --output-on-failure' -f (Quote-Cmd $vsDevCmd), (Quote-Cmd $ctest), (Quote-Cmd $build))
    if ($LASTEXITCODE -ne 0) { throw "CTest $mode failed with exit code $LASTEXITCODE." }
}

$forbidden = @(Get-ChildItem -LiteralPath $OutputRoot -File -Recurse | Where-Object {
    $_.Extension -in @('.key', '.crt', '.db') -or $_.Name -match '(?i)(private|secret|password)'
})
if ($forbidden.Count -gt 0) { throw "Build output contains forbidden secret/state files: $($forbidden.FullName -join '; ')" }
foreach ($file in Get-ChildItem -LiteralPath $OutputRoot -File -Recurse) {
    foreach ($entry in (Get-Acl -LiteralPath $file.FullName).Access) {
        if ($entry.AccessControlType -eq 'Allow' -and $entry.IdentityReference -match '(?i)Everyone' -and
            $entry.FileSystemRights.ToString() -match '(?i)FullControl') {
            throw "Build output is too permissive: $($file.FullName) grants Everyone FullControl"
        }
    }
}
Write-Output "MLS_E2E_BUILD_PASS output=$OutputRoot mlspp_commit=$($manifest.mlspp.commit) qt=$QtPrefix openssl=$OpenSslRoot nlohmann=$NlohmannPrefix chat_server=$ChatServerExe"
