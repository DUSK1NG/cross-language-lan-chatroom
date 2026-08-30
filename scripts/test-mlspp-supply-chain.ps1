[CmdletBinding()]
param(
    [string]$CacheDirectory = (Join-Path $env:TEMP 'lan-chat-mlspp-cache'),
    [string]$OpenSslRoot = (Join-Path $PSScriptRoot '..\.tools\vcpkg\installed\x64-windows'),
    [string]$ManifestPath = (Join-Path $PSScriptRoot '..\vendor\mls\manifest.json')
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content -Raw $ManifestPath | ConvertFrom-Json
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw 'cmake.exe is required.' }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC cl.exe is required; run from an x64 VS Developer Command Prompt.' }
if (-not (Test-Path (Join-Path $OpenSslRoot 'include\openssl\ssl.h'))) { throw "OpenSSL 3 headers are missing: $OpenSslRoot" }
New-Item -ItemType Directory -Force -Path $CacheDirectory | Out-Null

function Get-VerifiedArchive($spec, [string]$name) {
    $archive = Join-Path $CacheDirectory ($name + '.tar.gz')
    if (-not (Test-Path $archive)) {
        Invoke-WebRequest -Uri ($spec.repository + '/archive/' + $spec.commit + '.tar.gz') -OutFile $archive
    }
    $actual = (Get-FileHash $archive -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($actual -ne $spec.archiveSha256.ToUpperInvariant()) {
        throw "$name archive SHA-256 mismatch: expected $($spec.archiveSha256), got $actual"
    }
    return $archive
}

function Expand-Verified($archive, [string]$name) {
    $destination = Join-Path $CacheDirectory ($name + '-source')
    $root = Get-ChildItem $destination -Directory -ErrorAction SilentlyContinue | Where-Object Name -like ('*' + $manifest.$name.commit) | Select-Object -First 1
    if ($null -eq $root -or -not (Test-Path (Join-Path $root.FullName 'CMakeLists.txt'))) {
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        tar -xf $archive -C $destination
        $root = Get-ChildItem $destination -Directory | Where-Object Name -like ('*' + $manifest.$name.commit) | Select-Object -First 1
    }
    if ($null -eq $root -or -not (Test-Path (Join-Path $root.FullName 'CMakeLists.txt'))) { throw "Extracted $name source is incomplete." }
    return $root.FullName
}

$mlsArchive = Get-VerifiedArchive $manifest.mlspp 'mlspp'
$jsonArchive = Get-VerifiedArchive $manifest.nlohmannJson 'nlohmannJson'
$mlsSource = Expand-Verified $mlsArchive 'mlspp'
$jsonSource = Expand-Verified $jsonArchive 'nlohmannJson'
$jsonBuild = Join-Path $CacheDirectory 'nlohmann-build'
$jsonInstall = Join-Path $CacheDirectory 'nlohmann-install'
$smokeBuild = Join-Path $CacheDirectory 'smoke-build'
cmake -S $jsonSource -B $jsonBuild -G Ninja -DJSON_BuildTests=OFF -DJSON_Install=ON "-DCMAKE_INSTALL_PREFIX=$jsonInstall"
if ($LASTEXITCODE -ne 0) { throw 'nlohmann_json configure failed.' }
cmake --build $jsonBuild --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'nlohmann_json build failed.' }
cmake --install $jsonBuild
if ($LASTEXITCODE -ne 0) { throw 'nlohmann_json install failed.' }
cmake -S (Join-Path $PSScriptRoot '..\vendor\mls\smoke') -B $smokeBuild -G Ninja "-DMLSPP_SOURCE_DIR=$mlsSource" "-DOPENSSL_ROOT_DIR=$OpenSslRoot" "-DCMAKE_PREFIX_PATH=$jsonInstall" -DTESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'MLS++ smoke configure failed.' }
cmake --build $smokeBuild --target mlspp-supply-chain-smoke --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'MLS++ smoke build failed.' }
& (Join-Path $smokeBuild 'mlspp-supply-chain-smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'MLS++ smoke execution failed.' }
Write-Output "MLSPP_SUPPLY_CHAIN_PASS commit=$($manifest.mlspp.commit) json=$($manifest.nlohmannJson.commit)"
