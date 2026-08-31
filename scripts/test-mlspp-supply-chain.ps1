[CmdletBinding()]
param(
    [string]$CacheDirectory = (Join-Path $env:TEMP 'lan-chat-mlspp-cache'),
    [string]$OpenSslRoot = (Join-Path $PSScriptRoot '..\.tools\vcpkg\installed\x64-windows'),
    [string]$ManifestPath = (Join-Path $PSScriptRoot '..\vendor\mls\manifest.json')
)
$ErrorActionPreference = 'Stop'
$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) { throw 'cmake.exe is required.' }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC cl.exe is required; run from an x64 VS Developer Command Prompt.' }
if (-not (Test-Path -LiteralPath (Join-Path $OpenSslRoot 'include\openssl\ssl.h') -PathType Leaf)) { throw "OpenSSL 3 headers are missing: $OpenSslRoot" }
$CacheDirectory = [IO.Path]::GetFullPath($CacheDirectory)
$OpenSslRoot = [IO.Path]::GetFullPath($OpenSslRoot)
$ManifestPath = [IO.Path]::GetFullPath($ManifestPath)
$smokeSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\vendor\mls\smoke'))
New-Item -ItemType Directory -Force -Path $CacheDirectory | Out-Null

function Get-VerifiedArchive($spec, [string]$name) {
    $archive = Join-Path $CacheDirectory ($name + '.tar.gz')
    if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
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
    $root = Get-ChildItem -LiteralPath $destination -Directory -ErrorAction SilentlyContinue | Where-Object Name -like ('*' + $manifest.$name.commit) | Select-Object -First 1
    if ($null -eq $root -or -not (Test-Path -LiteralPath (Join-Path $root.FullName 'CMakeLists.txt') -PathType Leaf)) {
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        tar -xf $archive -C $destination
        $root = Get-ChildItem -LiteralPath $destination -Directory | Where-Object Name -like ('*' + $manifest.$name.commit) | Select-Object -First 1
    }
    if ($null -eq $root -or -not (Test-Path -LiteralPath (Join-Path $root.FullName 'CMakeLists.txt') -PathType Leaf)) { throw "Extracted $name source is incomplete." }
    if ($name -eq 'mlspp') {
        $sourceDigest = Get-SourceDigest $root.FullName
        if ($sourceDigest -ne $manifest.mlspp.sourceDigest.sha256.ToUpperInvariant()) {
            throw "mlspp source digest mismatch: expected $($manifest.mlspp.sourceDigest.sha256), got $sourceDigest"
        }
        $proof = [ordered]@{
            repository = $manifest.mlspp.repository
            commit = $manifest.mlspp.commit
            archiveSha256 = $manifest.mlspp.archiveSha256.ToUpperInvariant()
            sourceDigest = $sourceDigest
        }
        $proof | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $root.FullName '.lan-chat-mlspp-source-proof.json')
    }
    return $root.FullName
}

function Get-SourceDigest([string]$sourceRoot) {
    $recordLines = [System.Collections.Generic.List[string]]::new()
    $excluded = @('.git', '.vs', 'build', 'out')
    foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -File -Recurse) {
        $relative = $file.FullName.Substring($sourceRoot.Length).TrimStart('\', '/') -replace '\\', '/'
        $parts = $relative.Split('/')
        if ($relative -eq '.lan-chat-mlspp-source-proof.json' -or
            ($parts | Where-Object { $_ -in $excluded -or $_ -like 'cmake-build-*' }).Count -gt 0) {
            continue
        }
        $fileHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $recordLines.Add("$relative`t$fileHash`n")
    }
    $records = $recordLines.ToArray()
    [Array]::Sort($records, [System.StringComparer]::Ordinal)
    $canonical = [string]::Concat($records)
    $hasher = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($canonical))) -replace '-', '').ToUpperInvariant()
    } finally {
        $hasher.Dispose()
    }
}

$mlsArchive = Get-VerifiedArchive $manifest.mlspp 'mlspp'
$jsonArchive = Get-VerifiedArchive $manifest.nlohmannJson 'nlohmannJson'
$mlsSource = Expand-Verified $mlsArchive 'mlspp'
$jsonSource = Expand-Verified $jsonArchive 'nlohmannJson'
$jsonBuild = Join-Path $CacheDirectory 'nlohmann-build'
$jsonInstall = Join-Path $CacheDirectory 'nlohmann-install'
$smokeBuild = Join-Path $CacheDirectory 'smoke-build'
$jsonInstall = [IO.Path]::GetFullPath($jsonInstall)
$jsonPrefix = $jsonInstall
cmake -S $jsonSource -B $jsonBuild -G Ninja -DJSON_BuildTests=OFF -DJSON_Install=ON "-DCMAKE_INSTALL_PREFIX=$jsonInstall"
if ($LASTEXITCODE -ne 0) { throw 'nlohmann_json configure failed.' }
cmake --build $jsonBuild --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'nlohmann_json build failed.' }
cmake --install $jsonBuild
if ($LASTEXITCODE -ne 0) { throw 'nlohmann_json install failed.' }
cmake -S $smokeSource -B $smokeBuild -G Ninja "-DMLSPP_SOURCE_DIR=$mlsSource" "-DOPENSSL_ROOT_DIR=$OpenSslRoot" "-DCMAKE_PREFIX_PATH=$jsonPrefix" -DTESTING=OFF
if ($LASTEXITCODE -ne 0) { throw 'MLS++ smoke configure failed.' }
cmake --build $smokeBuild --target mlspp-supply-chain-smoke --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'MLS++ smoke build failed.' }
& (Join-Path $smokeBuild 'mlspp-supply-chain-smoke.exe')
if ($LASTEXITCODE -ne 0) { throw 'MLS++ smoke execution failed.' }
Write-Output "MLSPP_SUPPLY_CHAIN_PASS commit=$($manifest.mlspp.commit) json=$($manifest.nlohmannJson.commit)"
