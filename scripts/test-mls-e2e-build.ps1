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
$FrontendSource = Join-Path $Root 'frontend'
$ManifestPath = Join-Path $Root 'vendor\mls\manifest.json'
$SupplyScript = Join-Path $ScriptRoot 'test-mlspp-supply-chain.ps1'

if ([string]::IsNullOrWhiteSpace($QtPrefix)) { $QtPrefix = Join-Path $Root '.tools\qt\6.10.3\msvc2022_64' }
if ([string]::IsNullOrWhiteSpace($OpenSslRoot)) { $OpenSslRoot = Join-Path $Root '.tools\vcpkg\installed\x64-windows' }
if ([string]::IsNullOrWhiteSpace($CacheDirectory)) { $CacheDirectory = Join-Path $env:TEMP ('lan-chat-mlspp-e2e-cache-' + [Guid]::NewGuid().ToString('N')) }
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = Join-Path $env:TEMP ('lan-chat-mls-e2e-output-' + [Guid]::NewGuid().ToString('N')) }
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

function Resolve-Pnpm {
    foreach ($commandName in @('pnpm.cmd', 'pnpm.exe', 'pnpm')) {
        $command = Get-Command $commandName -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -ne $command -and -not [string]::IsNullOrWhiteSpace($command.Source) -and
            (Test-Path -LiteralPath $command.Source -PathType Leaf)) {
            return [IO.Path]::GetFullPath($command.Source)
        }
    }
    throw 'Locked pnpm executable could not be resolved.'
}

$inheritedPath = if ([string]::IsNullOrWhiteSpace($env:Path)) { '' } else { $env:Path }
$cleanPath = (($inheritedPath -split ';') | Where-Object { $_ -and $_ -notmatch '(?i)(mingw|msys)' }) -join ';'
$PowerShellHost = Resolve-PowerShellHost

function Invoke-VsCommand([string]$VsDevCmd, [string]$Tool, [string[]]$Arguments, [string]$RuntimePath = '', [hashtable]$Environment = @{}) {
    $quoted = ($Arguments | ForEach-Object { Quote-Cmd $_ }) -join ' '
    $pathSetup = 'set "PATH=' + $script:cleanPath + '"'
    if (-not [string]::IsNullOrWhiteSpace($RuntimePath)) { $pathSetup += ' && set "PATH=' + $RuntimePath + ';%PATH%"' }
    $environmentSetup = @($Environment.Keys | Sort-Object | ForEach-Object { 'set "' + $_ + '=' + [string]$Environment[$_] + '"' })
    $line = $pathSetup + ' && set "CMAKE_PREFIX_PATH=" && set "CMAKE_TOOLCHAIN_FILE=" && set "VCPKG_ROOT=" && set VSLANG=1033'
    if ($environmentSetup.Count -gt 0) { $line += ' && ' + ($environmentSetup -join ' && ') }
    $line += ' && call ' + (Quote-Cmd $VsDevCmd) + ' -arch=x64 -host_arch=x64 && ' + (Quote-Cmd $Tool) + ' ' + $quoted
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
Require-Leaf (Join-Path $FrontendSource 'package.json') 'frontend package manifest'
Require-Leaf (Join-Path $FrontendSource 'pnpm-lock.yaml') 'frontend pnpm lockfile'
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
$pnpm = Resolve-Pnpm
$cmake = if ($vsDevCmd) { Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' } else { '' }
$ninja = if ($vsDevCmd) { Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' } else { '' }
$ctest = if ($vsDevCmd) { Find-VsTool $vsDevCmd 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' } else { '' }
if ([string]::IsNullOrWhiteSpace($vsDevCmd) -or [string]::IsNullOrWhiteSpace($cmake) -or [string]::IsNullOrWhiteSpace($ninja) -or [string]::IsNullOrWhiteSpace($ctest)) { throw 'x64 VS DevCmd, CMake, Ninja, and CTest are required.' }

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
New-Item -ItemType Directory -Path $CacheDirectory -Force | Out-Null
$frontendWork = Join-Path $OutputRoot 'frontend'
$frontendDist = Join-Path $frontendWork 'dist'
$pnpmStore = Join-Path $CacheDirectory 'pnpm-store'
$pnpmCache = Join-Path $CacheDirectory 'pnpm-cache'
New-Item -ItemType Directory -Path $frontendWork -Force | Out-Null
foreach ($sourceItem in Get-ChildItem -LiteralPath $FrontendSource -Force) {
    if ($sourceItem.Name -in @('dist', 'node_modules', '.cache')) { continue }
    Copy-Item -LiteralPath $sourceItem.FullName -Destination (Join-Path $frontendWork $sourceItem.Name) -Recurse -Force
}
$frontendEnvironment = @{
    npm_config_cache = $pnpmCache
    COREPACK_HOME = (Join-Path $CacheDirectory 'corepack')
    PNPM_HOME = (Join-Path $CacheDirectory 'pnpm-home')
}
$packageJson = Get-Content -LiteralPath (Join-Path $FrontendSource 'package.json') -Raw | ConvertFrom-Json
$packageManager = [string]$packageJson.packageManager
if ($packageManager -notmatch '^pnpm@(?<version>[^\s]+)$') { throw "Frontend packageManager must pin pnpm: $packageManager" }
$lockedPnpmVersion = $Matches.version
$pnpmVersionOutput = Invoke-VsCommand $vsDevCmd $pnpm @('--version') (Join-Path $OpenSslRoot 'bin') $frontendEnvironment
if ($script:LastVsExitCode -ne 0) { throw "Locked pnpm version check failed with exit $($script:LastVsExitCode): $($pnpmVersionOutput -join [Environment]::NewLine)" }
$actualPnpmVersion = (($pnpmVersionOutput -join [Environment]::NewLine) -split '\r?\n' | Where-Object { $_ -match '^\d+\.\d+\.\d+$' } | Select-Object -Last 1).Trim()
if ($actualPnpmVersion -ne $lockedPnpmVersion) { throw "pnpm version mismatch: packageManager requires $lockedPnpmVersion, got $actualPnpmVersion" }
$pnpmInstallOutput = Invoke-VsCommand $vsDevCmd $pnpm @('--dir', $frontendWork, 'install', '--frozen-lockfile', '--store-dir', $pnpmStore) (Join-Path $OpenSslRoot 'bin') $frontendEnvironment
if ($script:LastVsExitCode -ne 0) { throw "frontend pnpm install failed with exit $($script:LastVsExitCode): $($pnpmInstallOutput -join [Environment]::NewLine)" }
$pnpmTestOutput = Invoke-VsCommand $vsDevCmd $pnpm @('--dir', $frontendWork, 'run', 'test', '--', '--run') (Join-Path $OpenSslRoot 'bin') $frontendEnvironment
if ($script:LastVsExitCode -ne 0) { throw "frontend pnpm test --run failed with exit $($script:LastVsExitCode): $($pnpmTestOutput -join [Environment]::NewLine)" }
$pnpmBuildOutput = Invoke-VsCommand $vsDevCmd $pnpm @('--dir', $frontendWork, 'run', 'build') (Join-Path $OpenSslRoot 'bin') $frontendEnvironment
if ($script:LastVsExitCode -ne 0) { throw "frontend pnpm build failed with exit $($script:LastVsExitCode): $($pnpmBuildOutput -join [Environment]::NewLine)" }
Require-Leaf (Join-Path $frontendDist 'index.html') 'temporary React release UI'
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
$goCache = Join-Path $CacheDirectory 'go-build-cache'
$goModuleCache = Join-Path $CacheDirectory 'go-module-cache'
$previousGoCache = $env:GOCACHE
$previousGoModuleCache = $env:GOMODCACHE
$env:GOCACHE = $goCache
$env:GOMODCACHE = $goModuleCache
Push-Location $ServerSource
try {
    & $go build -o $serverExe .
    if ($LASTEXITCODE -ne 0) { throw "server-go build failed with exit code $LASTEXITCODE." }
}
finally {
    Pop-Location
    $env:GOCACHE = $previousGoCache
    $env:GOMODCACHE = $previousGoModuleCache
}
Require-Leaf $serverExe 'fresh server-go build'

$prefixPath = "$QtPrefix;$jsonInstall"
$common = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$ninja", "-DCMAKE_PREFIX_PATH=$prefixPath", "-DOPENSSL_ROOT_DIR=$OpenSslRoot", "-DOPENSSL_INCLUDE_DIR=$OpenSslRoot\include", "-DOPENSSL_SSL_LIBRARY=$OpenSslRoot\lib\libssl.lib", "-DOPENSSL_CRYPTO_LIBRARY=$OpenSslRoot\lib\libcrypto.lib", '-DOPENSSL_USE_STATIC_LIBS=FALSE', '-DLAN_CHAT_ENABLE_WEB_UI=ON', '-DLAN_CHAT_ENABLE_PERF_OVERLAY=OFF', "-DLAN_CHAT_FRONTEND_DIST_DIR=$frontendDist")
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
    $passed = 0
    $failed = 0
    $skipped = 0
    for ($iteration = 1; $iteration -le 3; $iteration++) {
        $resultPath = Join-Path $env:TEMP ('lan-chat-mls-focused-' + [Guid]::NewGuid().ToString('N') + '.txt')
        try {
            # Run each fixture in a fresh process. Repeating inside one QtTest
            # process can retain a just-stopped local server between rounds.
            $testOutput = Invoke-VsCommand $vsDevCmd $testExe @('-repeat', '1', $testName, '-o', "$resultPath,txt") $runtimePath
            if ($script:LastVsExitCode -ne 0) {
                # Preserve the native process output and QtTest result before
                # the per-iteration temporary result is removed below. This
                # is failure-only diagnostics; successful runs remain clean.
                $failurePath = Join-Path $OutputRoot ("focused-$testName-$iteration-failure.txt")
                $failureLines = @(
                    "test=$testName"
                    "iteration=$iteration"
                    "exit=$script:LastVsExitCode"
                    "command=$testExe -repeat 1 $testName -o $resultPath,txt"
                    '--- process-output ---'
                    ($testOutput | Out-String)
                    '--- qttest-result ---'
                )
                if (Test-Path -LiteralPath $resultPath -PathType Leaf) {
                    $failureLines += Get-Content -LiteralPath $resultPath
                } else {
                    $failureLines += '(QtTest result file was not created)'
                }
                Set-Content -LiteralPath $failurePath -Value $failureLines -Encoding UTF8
                throw "Focused $testName iteration $iteration failed with exit $($script:LastVsExitCode): $($testOutput -join [Environment]::NewLine)"
            }
            $resultText = ''
            if (Test-Path -LiteralPath $resultPath -PathType Leaf) { $resultText = Get-Content -LiteralPath $resultPath -Raw }
            $combined = ($testOutput -join [Environment]::NewLine) + "`n" + $resultText
            $match = [regex]::Match($combined, 'Totals:\s*(\d+) passed,\s*(\d+) failed,\s*(\d+) skipped')
            if (-not $match.Success) { throw "Focused $testName iteration $iteration did not record QtTest totals." }
            $iterationPassed = [int]$match.Groups[1].Value
            $iterationFailed = [int]$match.Groups[2].Value
            $iterationSkipped = [int]$match.Groups[3].Value
            if ($iterationPassed -ne 3 -or $iterationFailed -ne 0 -or $iterationSkipped -ne 0) {
                throw "Focused $testName iteration $iteration did not record 3/0/0 totals."
            }
            $passed += $iterationPassed
            $failed += $iterationFailed
            $skipped += $iterationSkipped
        }
        finally { if (Test-Path -LiteralPath $resultPath) { Remove-Item -LiteralPath $resultPath -Force } }
    }
    if ($passed -ne 9 -or $failed -ne 0 -or $skipped -ne 0) { throw "Focused $testName did not record 9/0/0 totals." }
}

$outputFiles = @(Get-ChildItem -LiteralPath $OutputRoot -File -Recurse | Where-Object { $_.FullName -notlike ($frontendWork + '\node_modules\*') })
$forbidden = @($outputFiles | Where-Object { $_.Extension -in @('.key', '.crt', '.db') -or $_.Name -match '(?i)(private|secret|password)' })
if ($forbidden.Count -gt 0) { throw "Build output contains forbidden secret/state files: $($forbidden.FullName -join '; ')" }
foreach ($file in $outputFiles) {
    foreach ($entry in (Get-Acl -LiteralPath $file.FullName).Access) {
        if ($entry.AccessControlType -eq 'Allow' -and $entry.IdentityReference -match '(?i)Everyone' -and $entry.FileSystemRights.ToString() -match '(?i)FullControl') { throw "Build output is too permissive: $($file.FullName) grants Everyone FullControl" }
    }
}
Write-Output "MLS_E2E_BUILD_PASS output=$OutputRoot powershell=$PowerShellHost mlspp_commit=$($mlsSpec.commit) qt=$QtPrefix openssl=$OpenSslRoot nlohmann=$jsonInstall chat_server=$serverExe ON=$($expected.ON)/$($expected.ON) OFF=$($expected.OFF)/$($expected.OFF) focused=9/0/0+9/0/0"
