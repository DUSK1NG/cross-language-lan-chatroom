param([switch]$Wait)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Gui = Join-Path $Root "client-cpp\gui\build\lan-chat-gui.exe"
if (-not (Test-Path -LiteralPath $Gui)) { throw "缺少 GUI：$Gui" }

$process = Start-Process -FilePath $Gui -WorkingDirectory (Split-Path -Parent $Gui) -PassThru
if ($Wait) { $process.WaitForExit(); exit $process.ExitCode }
