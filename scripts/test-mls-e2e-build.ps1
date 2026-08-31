[CmdletBinding()]
param(
    [string]$QtPrefix = '',
    [string]$OpenSslRoot = '',
    [string]$CacheDirectory = '',
    [string]$OutputRoot = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptRoot = if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) { Split-Path -Parent $PSCommandPath } else { $PSScriptRoot }
$Root = [IO.Path]::GetFullPath((Join-Path $ScriptRoot '..'))
$GuiSource = Join-Path $Root 'client-cpp\gui'
$ServerSource = Join-Path $Root 'server-go'
$ManifestPath = Join-Path $Root 'vendor\mls\manifest.json'
$SupplyScript = Join-Path $ScriptRoot 'test-mlspp-supply-chain.ps1'

if ([string]::IsNullOrWhiteSpace($QtPrefix)) { $QtPrefix = Join-Path $Root '.tools\qt\6.10.3\msvc2022_64' }
if ([string]::IsNullOrWhiteSpace($OpenSslRoot)) { $OpenSslRoot = Join-Path $Root '.tools\vcpkg\installed\x64-windows' }
if ([string]::IsNullOrWhiteSpace($CacheDirectory)) { $CacheDirectory = Join-Path $env:TEMP ('lan-chat-mlspp-e2e-cache-' + [Guid]::NewGuid().ToString('N')) }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $Root ('out\mls-e2e-fresh-' + [Guid]::NewGuid().ToString('N')) }
$QtPrefix = [IO.Path]::GetFullPath($QtPrefix)
$OpenSslRoot = [IO.Path]::GetFullPath($OpenSslRoot)
$CacheDirectory = [IO.Path]::GetFullPath($CacheDirectory)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)

function Require-Leaf([string]$Path, [string]$Description) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "$Description is missing: $Path" }
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

function Find-VsTool([string]$VsDevCmd, [string]$RelativePath) {
    $toolsDirectory = Split-Path -Parent $VsDevCmd
    $commonDirectory = Split-Path -Parent $toolsDirectory
    $installationDirectory = Split-Path -Parent $commonDirectory
    $candidate = Join-Path $installationDirectory $RelativePath
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate }
    return ''
}

function Quote-Cmd([string]$Value) { return '"' + $Value.Replace('"', '\"') + '"' }

function Resolve-PowerShellHost {
    $processPath = ''
    try { $processPath = (Get-Process -Id $PID -ErrorAction Stop).Path } catch { $processPath = '' }
    if (-not [string]::IsNullOrWhiteSpace($processPath) -and (Test-Path -LiteralPath $processPath -PathType Leaf)) {
        return [IO.Path]::GetFullPath($processPath)
    }
    foreach ($commandName in @('pwsh.exe', 'powershell.exe')) {
        $command = Get-Command $commandName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source) -and
            (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
            return [IO.Path]::GetFullPath($command.Source)
        }
    }
    throw 'PowerShell host executable could not be resolved; refusing to skip the supply-chain gate.'
}

$inheritedPath = if ([string]::IsNullOrWhiteSpace($env:Path)) { '' } else { $env:Path }
$cleanPath = (($inheritedPath -split ';') | Where-Object { $_ -and $_ -notmatch '(?i)(mingw|msys)' }) -join ';'
$PowerShellHost = Resolve-PowerShellHost

function Invoke-VsCommand([string]$VsDevCmd, [string]$Tool, [string[]]$Arguments, [string]$RuntimePath = '') {
    $quoted = ($Arguments | ForEach-Object { Quote-Cmd $_ }) -join ' '
    $pathSetup = 'set "PATH=' + $script:cleanPath + '"'
    if (-not [string]::IsNullOrWhiteSpace($RuntimePath)) { $pathSetup += ' && set "PATH=' + $RuntimePath + ';%PATH%"' }
    $line = $pathSetup + ' && set "CMAKE_PREFIX_PATH=" && set "CMAKE_TOOLCHAIN_FILE=" && set "VCPKG_ROOT=" && set VSLANG=1033 && call ' + (Quote-Cmd $VsDevCmd) + ' -arch=x64 -host_arch=x64 && ' + (Quote-Cmd $Tool) + ' ' + $quoted
    $previousErrorAction = $ErrorActionPreference
    try {
        # PowerShell 7 can promote native stderr records to terminating errors;
        # preserve the tool exit code and let callers apply the gate.
        $ErrorActionPreference = 'Continue'
        $output = & cmd.exe /d /s /c $line 2>&1
        $script:LastVsExitCode = $LASTEXITCODE
        return @($output)
    }
    finally { $ErrorActionPreference = $previousErrorAction }
}

function Assert-Sha256([string]$Path, [string]$Expected, [string]$Description) {
    Require-Leaf $Path $Description
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actual -ne $Expected.ToUpperInvariant()) { throw "$Description SHA-256 mismatch: expected $Expected, got $actual" }
}

function Get-SourceDigest([string]$SourceRoot) {
    $records = [System.Collections.Generic.List[string]]::new()
    $excluded = @('.git', '.vs', 'build', 'out')
    foreach ($file in Get-ChildItem -LiteralPath $SourceRoot -File -Recurse) {
        $relative = $file.FullName.Substring($SourceRoot.Length).TrimStart('\', '/') -replace '\\', '/'
        $parts = $relative.Split('/')
        $excludedParts = @($parts | Where-Object { $_ -in $excluded -or $_ -like 'cmake-build-*' })
        if ($relative -eq '.lan-chat-mlspp-source-proof.json' -or $excludedParts.Count -gt 0) { continue }
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $records.Add($relative + "`t" + $hash + "`n")
    }
    $array = $records.ToArray()
    [Array]::Sort($array, [System.StringComparer]::Ordinal)
    $canonical = [string]::Concat($array)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return (([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical))) -replace '-', '').ToUpperInvariant()) }
    finally { $sha.Dispose() }
}

Require-Leaf $ManifestPath 'MLS manifest'
Require-Leaf (Join-Path $QtPrefix 'lib\cmake\Qt6\Qt6Config.cmake') 'Qt 6.10.3 configuration'
Require-Leaf (Join-Path $OpenSslRoot 'include\openssl\ssl.h') 'OpenSSL 3 headers'
Require-Leaf (Join-Path $OpenSslRoot 'lib\libssl.lib') 'OpenSSL SSL import library'
Require-Leaf (Join-Path $OpenSslRoot 'lib\libcrypto.lib') 'OpenSSL Crypto import library'
Require-Leaf $SupplyScript 'MLS++ supply-chain script'
Require-Leaf (Join-Path $ServerSource 'go.mod') 'server-go checkout'
if (Test-Path -LiteralPath $OutputRoot) { throw "OutputRoot must be new; refusing to reuse $OutputRoot" }
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$mlsSpec = $manifest.mlspp
$jsonSpec = $manifest.nlohmannJson
foreach ($spec in @($mlsSpec, $jsonSpec)) {
    foreach ($field in @('repository', 'commit', 'archiveSha256', 'license')) {
        if ([string]::IsNullOrWhiteSpace([string]$spec.$field)) { throw "Manifest field is missing: $field" }
    }
}
if ([string]::IsNullOrWhiteSpace([string]$mlsSpec.sourceDigest.sha256)) { throw 'Manifest MLS++ sourceDigest.sha256 is missing.' }

$vsDevCmd = Find-VsDevCmd
$cmake = if ($vsDevCmd) { Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' } else { '' }
$ninja = if ($vsDevCmd) { Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' } else { '' }
$ctest = if ($vsDevCmd) { Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' } else { '' }
if ([string]::IsNullOrWhiteSpace($vsDevCmd) -or [string]::IsNullOrWhiteSpace($cmake) -or [string]::IsNullOrWhiteSpace($ninja) -or [string]::IsNullOrWhiteSpace($ctest)) { throw 'x64 VS DevCmd, CMake, Ninja, and CTest are required.' }

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
New-Item -ItemType Directory -Path $CacheDirectory -Force | Out-Null
$supplyArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $SupplyScript, '-CacheDirectory', $CacheDirectory, '-OpenSslRoot', $OpenSslRoot, '-ManifestPath', $ManifestPath)
$supplyOutput = Invoke-VsCommand $vsDevCmd $PowerShellHost $supplyArgs (Join-Path $OpenSslRoot 'bin')
if ($script:LastVsExitCode -ne 0) { throw "MLS++ supply-chain verification failed (exit $script:LastVsExitCode): $($supplyOutput -join [Environment]::NewLine)" }

$mlsArchive = Join-Path $CacheDirectory 'mlspp.tar.gz'
$jsonArchive = Join-Path $CacheDirectory 'nlohmannJson.tar.gz'
Assert-Sha256 $mlsArchive ([string]$mlsSpec.archiveSha256) 'MLS++ archive'
Assert-Sha256 $jsonArchive ([string]$jsonSpec.archiveSha256) 'nlohmann_json archive'
$mlsSourceParent = Join-Path $CacheDirectory 'mlspp-source'
$mlsSourceCandidates = @(Get-ChildItem -LiteralPath $mlsSourceParent -Directory | Where-Object { $_.Name -like ('*' + $mlsSpec.commit) })
if ($mlsSourceCandidates.Count -ne 1) { throw "Expected exactly one locked MLS++ source directory, found $($mlsSourceCandidates.Count)." }
$mlsSource = $mlsSourceCandidates[0].FullName
Require-Leaf (Join-Path $mlsSource 'CMakeLists.txt') 'MLS++ source'
$proofPath = Join-Path $mlsSource '.lan-chat-mlspp-source-proof.json'
Require-Leaf $proofPath 'MLS++ source proof'
$proof = Get-Content -LiteralPath $proofPath -Raw | ConvertFrom-Json
foreach ($field in @('repository', 'commit', 'archiveSha256', 'sourceDigest')) {
    if ([string]$proof.$field -ne [string]$mlsSpec.$field -and -not ($field -eq 'sourceDigest' -and [string]$proof.$field -eq [string]$mlsSpec.sourceDigest.sha256)) { throw "MLS++ proof $field does not match manifest." }
}
if ((Get-SourceDigest $mlsSource) -ne ([string]$mlsSpec.sourceDigest.sha256).ToUpperInvariant()) { throw 'MLS++ source digest does not match the locked manifest.' }
$jsonInstall = Join-Path $CacheDirectory 'nlohmann-install'
$jsonConfigs = @(Get-ChildItem -LiteralPath $jsonInstall -Filter 'nlohmann_jsonConfig.cmake' -File -Recurse -ErrorAction SilentlyContinue)
if ($jsonConfigs.Count -ne 1) { throw "Expected one nlohmann_jsonConfig.cmake, found $($jsonConfigs.Count)." }

$go = (Get-Command go.exe -ErrorAction Stop).Source
$serverExe = Join-Path $OutputRoot 'chat-server.exe'
Push-Location $ServerSource
try { & $go build -o $serverExe .; if ($LASTEXITCODE -ne 0) { throw "server-go build failed with exit code $LASTEXITCODE." } }
finally { Pop-Location }
Require-Leaf $serverExe 'fresh server-go build'

$prefixPath = "$QtPrefix;$jsonInstall"
$common = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_PREFIX_PATH=$prefixPath", "-DOPENSSL_ROOT_DIR=$OpenSslRoot", "-DOPENSSL_INCLUDE_DIR=$OpenSslRoot\include", "-DOPENSSL_SSL_LIBRARY=$OpenSslRoot\lib\libssl.lib", "-DOPENSSL_CRYPTO_LIBRARY=$OpenSslRoot\lib\libcrypto.lib", '-DOPENSSL_USE_STATIC_LIBS=FALSE', '-DLAN_CHAT_ENABLE_WEB_UI=ON', '-DLAN_CHAT_ENABLE_PERF_OVERLAY=OFF')
$expected = @{ ON = 18; OFF = 17 }
$builds = @{}
foreach ($mode in @('ON', 'OFF')) {
    $build = Join-Path $OutputRoot ('build-' + $mode.ToLowerInvariant())
    $builds[$mode] = $build
    $configure = @('-S', $GuiSource, '-B', $build, '--fresh') + $common + "-DLAN_CHAT_ENABLE_MLSPP=$mode"
    if ($mode -eq 'ON') { $configure += '-DLAN_CHAT_ENABLE_TEST_HOOKS=ON'; $configure += "-DMLSPP_SOURCE_DIR=$mlsSource" }
    $configureRuntimePath = "$QtPrefix\bin;$OpenSslRoot\bin"
    $configureOutput = Invoke-VsCommand $vsDevCmd $cmake $configure $configureRuntimePath
    if ($script:LastVsExitCode -ne 0) { throw "CMake $mode configure failed (exit $script:LastVsExitCode): $($configureOutput -join [Environment]::NewLine)" }
    $buildOutput = Invoke-VsCommand $vsDevCmd $cmake @('--build', $build, '--parallel', '4')
    if ($script:LastVsExitCode -ne 0) { throw "CMake $mode build failed (exit $script:LastVsExitCode): $($buildOutput -join [Environment]::NewLine)" }
    $serverDirectory = Join-Path $build 'server-go'
    New-Item -ItemType Directory -Path $serverDirectory -Force | Out-Null
    Copy-Item -LiteralPath $serverExe -Destination (Join-Path $serverDirectory 'chat-server.exe') -Force
    Require-Leaf (Join-Path $serverDirectory 'chat-server.exe') "$mode controlled server layout"

    $modeRuntimePath = "$QtPrefix\bin;$OpenSslRoot\bin;$build"
    $showOutput = Invoke-VsCommand $vsDevCmd $ctest @('--test-dir', $build, '--show-only=json-v1') $modeRuntimePath
    if ($script:LastVsExitCode -ne 0) { throw "CTest $mode discovery failed with exit $script:LastVsExitCode." }
    $jsonText = ($showOutput -join [Environment]::NewLine)
    $jsonStart = $jsonText.IndexOf('{')
    if ($jsonStart -lt 0) { throw "CTest $mode discovery did not return JSON." }
    $discovery = $jsonText.Substring($jsonStart) | ConvertFrom-Json
    $discovered = @($discovery.tests)
    if ($discovered.Count -ne $expected[$mode]) { throw "CTest $mode discovered $($discovered.Count) tests; expected $($expected[$mode])." }
    if (@($discovered | Where-Object { $_.name -eq 'chat-bridge-tests' }).Count -ne 1) { throw "CTest $mode discovery missing chat-bridge-tests." }
    $junit = Join-Path $OutputRoot ('ctest-' + $mode.ToLowerInvariant() + '.xml')
    $runOutput = Invoke-VsCommand $vsDevCmd $ctest @('--test-dir', $build, '--output-on-failure', '--output-junit', $junit) $modeRuntimePath
    if ($script:LastVsExitCode -ne 0) { throw "CTest $mode failed with exit $($script:LastVsExitCode): $($runOutput -join [Environment]::NewLine)" }
    Require-Leaf $junit "CTest $mode JUnit output"
    [xml]$junitDocument = Get-Content -LiteralPath $junit -Raw
    $cases = @($junitDocument.SelectNodes('//testcase'))
    if ($cases.Count -ne $expected[$mode]) { throw "CTest $mode JUnit recorded $($cases.Count) cases; expected $($expected[$mode])." }
    if (@($junitDocument.SelectNodes('//failure|//error|//skipped')).Count -ne 0) { throw "CTest $mode JUnit contains failure, error, or skipped cases." }
}

$testExe = Join-Path $builds.ON 'chat-bridge-tests.exe'
Require-Leaf $testExe 'MLS++ bridge test executable'
$runtimePath = "$QtPrefix\bin;$OpenSslRoot\bin;$($builds.ON)"
foreach ($testName in @('mlsControlRoundTripUsesProposalCommitWelcomeOrder', 'authenticatedRawTlsMLSFramesAreRejected')) {
    $resultPath = Join-Path $env:TEMP ('lan-chat-mls-focused-' + [Guid]::NewGuid().ToString('N') + '.txt')
    try {
        $testOutput = Invoke-VsCommand $vsDevCmd $testExe @('-repeat', '3', $testName, '-o', "$resultPath,txt") $runtimePath
        if ($script:LastVsExitCode -ne 0) { throw "Focused $testName failed with exit $($script:LastVsExitCode): $($testOutput -join [Environment]::NewLine)" }
        $resultText = ''
        if (Test-Path -LiteralPath $resultPath -PathType Leaf) { $resultText = Get-Content -LiteralPath $resultPath -Raw }
        $combined = ($testOutput -join [Environment]::NewLine) + "`n" + $resultText
        $match = [regex]::Match($combined, 'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped')
        if (-not $match.Success -or $match.Groups[1].Value -ne '9' -or $match.Groups[2].Value -ne '0' -or $match.Groups[3].Value -ne '0') { throw "Focused $testName did not record 9/0/0 totals." }
    }
    finally { if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath -Force } }
}

$forbidden = @(Get-ChildItem -LiteralPath $OutputRoot -File -Recurse | Where-Object { $_.Extension -in @('.key', '.crt', '.db') -or $_.Name -match '(?i)(private|secret|password)' })
if ($forbidden.Count -gt 0) { throw "Build output contains forbidden secret/state files: $($forbidden.FullName -join '; ')" }
foreach ($file in Get-ChildItem -LiteralPath $OutputRoot -File -Recurse) {
    foreach ($entry in (Get-Acl -LiteralPath $file.FullName).Access) {
        if ($entry.AccessControlType -eq 'Allow' -and $entry.IdentityReference -match '(?i)Everyone' -and $entry.FileSystemRights.ToString() -match '(?i)FullControl') { throw "Build output is too permissive: $($file.FullName) grants Everyone FullControl" }
    }
}
Write-Output "MLS_E2E_BUILD_PASS output=$OutputRoot powershell=$PowerShellHost mlspp_commit=$($mlsSpec.commit) qt=$QtPrefix openssl=$OpenSslRoot nlohmann=$jsonInstall chat_server=$serverExe ON=$($expected.ON)/$($expected.ON) OFF=$($expected.OFF)/$($expected.OFF) focused=9/0/0+9/0/0"
