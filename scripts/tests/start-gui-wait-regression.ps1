[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$fixture = Join-Path $env:TEMP ('lan-chat-start-gui-' + [Guid]::NewGuid().ToString('N'))
try {
    $scripts = Join-Path $fixture 'scripts'
    $runtime = Join-Path $fixture 'out\desktop'
    New-Item -ItemType Directory -Path $scripts, $runtime -Force | Out-Null
    $launcher = Join-Path $scripts 'start-gui.ps1'
    Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'start-gui.ps1') -Destination $launcher
    New-Item -ItemType File -Path (Join-Path $runtime 'lan-chat-server.exe') | Out-Null
    $binary = Join-Path $runtime 'LANChat.exe'
    Add-Type -TypeDefinition 'public class WaitFixture { public static int Main() { return 12; } }' -OutputAssembly $binary -OutputType ConsoleApplication
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $launcher -CheckOnly
    if ($LASTEXITCODE -ne 0) { throw "CheckOnly exited with $LASTEXITCODE" }
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $launcher -Wait
    if ($LASTEXITCODE -ne 12) { throw "Wait did not return the desktop exit code: $LASTEXITCODE" }
} finally {
    if (Test-Path -LiteralPath $fixture -PathType Container) {
        $resolved = (Resolve-Path -LiteralPath $fixture).Path
        $tempPrefix = (Resolve-Path -LiteralPath $env:TEMP).Path.TrimEnd('\') + '\'
        if ($resolved.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
Write-Host 'start-gui wait regression passed.'
