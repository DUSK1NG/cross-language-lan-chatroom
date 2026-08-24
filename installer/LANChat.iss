; LAN Chat per-user Windows installer.
; Build with scripts\package-installer.ps1 so SourceDir always points at the
; already security-validated unified runtime package.

#ifndef SourceDir
  #define SourceDir "..\release\LANChat-Windows-x64"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif
#ifndef AppVersion
  #define AppVersion "1.1.0"
#endif

[Setup]
AppId={{B9E987BA-7785-40E1-9D12-2E3C1AC7E1F8}
AppName=LAN Chat
AppVersion={#AppVersion}
AppPublisher=LAN Chat
DefaultDirName={localappdata}\Programs\LAN Chat
DefaultGroupName=LAN Chat
DisableProgramGroupPage=yes
UninstallDisplayName=LAN Chat
UninstallDisplayIcon={app}\LANChat.exe
Uninstallable=yes
OutputDir={#OutputDir}
OutputBaseFilename=LANChat-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: checkedonce

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\LAN Chat"; Filename: "{app}\LANChat.exe"; WorkingDir: "{app}"; Comment: "启动 LAN Chat"
Name: "{autoprograms}\LAN Chat\卸载 LAN Chat"; Filename: "{uninstallexe}"; Comment: "卸载 LAN Chat"
Name: "{autodesktop}\LAN Chat"; Filename: "{app}\LANChat.exe"; WorkingDir: "{app}"; Comment: "启动 LAN Chat"; Tasks: desktopicon

[Run]
Filename: "{app}\LANChat.exe"; Description: "启动 LAN Chat"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

; The uninstaller removes files installed above and the Windows uninstall entry.
; Host-created TLS keys and chat data live under the current user's LocalAppData
; directory and are intentionally outside the install tree. The uninstaller
; therefore never silently deletes a host identity or chat history.
