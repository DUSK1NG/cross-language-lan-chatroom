[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$PackageDirectory,
    [ValidateSet('lan', 'online')]
    [string]$Edition = 'lan',
    [switch]$Smoke
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$PackageDirectory = [System.IO.Path]::GetFullPath($PackageDirectory)
if (-not (Test-Path -LiteralPath $PackageDirectory -PathType Container)) {
    throw "Desktop package directory was not found: $PackageDirectory"
}

function Require-File([string]$RelativePath) {
    $path = Join-Path $PackageDirectory $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Runtime package is missing required file: $RelativePath"
    }
}

$expectedFiles = @('LANChat.exe', 'LANChat.ico', 'README.md')
if ($Edition -eq 'lan') {
    $expectedFiles += 'lan-chat-server.exe'
}
foreach ($file in $expectedFiles) { Require-File $file }
$unexpected = @(Get-ChildItem -LiteralPath $PackageDirectory -File -Force -Recurse |
    Where-Object { $_.FullName.Substring($PackageDirectory.TrimEnd('\').Length + 1) -notin $expectedFiles })
if ($unexpected.Count) { throw "Unexpected desktop package files: $($unexpected.Name -join ', ')" }
if ($Smoke) {
    $previousProfile = $env:WEBVIEW2_USER_DATA_FOLDER
    $previousLocalData = $env:LOCALAPPDATA
    $fixture = Join-Path $env:TEMP ('LANChat-desktop-smoke-' + [Guid]::NewGuid().ToString('N'))
    $runtime = Join-Path $fixture 'runtime'
    New-Item -ItemType Directory -Path $runtime | Out-Null
    $env:WEBVIEW2_USER_DATA_FOLDER = Join-Path $fixture 'webview'
    $env:LOCALAPPDATA = Join-Path $fixture 'local'
    $stderr = Join-Path $fixture 'startup.txt'
    $title = if ($Edition -eq 'lan') { 'LAN Chat' } else { 'LAN Chat Online' }
    $launch = $null
    try {
        New-Item -ItemType Directory -Path $env:WEBVIEW2_USER_DATA_FOLDER, $env:LOCALAPPDATA | Out-Null
        foreach ($file in $expectedFiles) {
            Copy-Item -LiteralPath (Join-Path $PackageDirectory $file) -Destination $runtime
        }
        $launch = Start-Process -FilePath (Join-Path $runtime 'LANChat.exe') -WorkingDirectory $runtime -WindowStyle Hidden -RedirectStandardError $stderr -PassThru
        [void]$launch.Handle
        $deadline = [DateTime]::UtcNow.AddSeconds(12)
        do {
            $launch.Refresh()
            if ($launch.HasExited) { throw "Desktop startup exited with code $($launch.ExitCode). $(Get-Content -LiteralPath $stderr -Raw)" }
            $webviewLoaded = $launch.MainWindowTitle -eq $title -and
                @($launch.Modules | Where-Object ModuleName -eq 'EmbeddedBrowserWebView.dll').Count -gt 0
            if (!$webviewLoaded) { Start-Sleep -Milliseconds 300 }
        } until ($webviewLoaded -or [DateTime]::UtcNow -ge $deadline)
        if (!$webviewLoaded) { throw 'Desktop startup did not load WebView2.' }
        if (!$launch.CloseMainWindow() -or !$launch.WaitForExit(10000)) {
            throw 'Desktop did not close cleanly after the WebView2 startup check.'
        }
        if ($launch.ExitCode -ne 0) { throw "Desktop shutdown exited with code $($launch.ExitCode). $(Get-Content -LiteralPath $stderr -Raw)" }
    } finally {
        if ($null -ne $launch -and !$launch.HasExited) { Stop-Process -Id $launch.Id }
        $env:WEBVIEW2_USER_DATA_FOLDER = $previousProfile
        $env:LOCALAPPDATA = $previousLocalData
        if (Test-Path -LiteralPath $fixture -PathType Container) {
            $resolved = (Resolve-Path -LiteralPath $fixture).Path
            $temp = (Resolve-Path -LiteralPath $env:TEMP).Path.TrimEnd('\') + '\'
            if ($resolved.StartsWith($temp, [StringComparison]::OrdinalIgnoreCase)) {
                Remove-Item -LiteralPath $resolved -Recurse -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
Write-Host "Desktop runtime package validation passed: $PackageDirectory"
