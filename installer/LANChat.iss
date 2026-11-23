; LAN Chat per-user Windows installer.
; Build with scripts\package-installer.ps1 so SourceDir always points at the
; already security-validated unified runtime package.

#ifndef SourceDir
  #define SourceDir "..\release\LANChat-Desktop-Windows-x64"
#endif
#ifndef OutputDir
  #define OutputDir "..\release"
#endif
#ifndef AppVersion
  #define AppVersion "1.4.0"
#endif
#ifndef Edition
  #define Edition "lan"
#endif
#if Edition == "online"
  #define DisplayName "LAN Chat Online"
  #define PackageName "LANChat-Online-Setup-x64"
#else
  #define DisplayName "LAN Chat"
  #define PackageName "LANChat-Setup-x64"
#endif

[Setup]
#if Edition == "online"
AppId={{D8430C8E-C155-4D53-A708-73A98ADA144E}
#else
AppId={{B9E987BA-7785-40E1-9D12-2E3C1AC7E1F8}
#endif
AppName={#DisplayName}
AppVersion={#AppVersion}
AppPublisher=LAN Chat
DefaultDirName={localappdata}\Programs\{#DisplayName}
DefaultGroupName={#DisplayName}
DisableProgramGroupPage=yes
UninstallDisplayName={#DisplayName}
SetupIconFile={#SourceDir}\LANChat.ico
UninstallDisplayIcon={app}\LANChat.ico
Uninstallable=yes
OutputDir={#OutputDir}
OutputBaseFilename={#PackageName}
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
Name: "{autoprograms}\{#DisplayName}"; Filename: "{app}\LANChat.exe"; WorkingDir: "{app}"; IconFilename: "{app}\LANChat.ico"; Comment: "启动 {#DisplayName}"
Name: "{autoprograms}\{#DisplayName}\卸载 {#DisplayName}"; Filename: "{uninstallexe}"; Comment: "卸载 {#DisplayName}"
Name: "{autodesktop}\{#DisplayName}"; Filename: "{app}\LANChat.exe"; WorkingDir: "{app}"; IconFilename: "{app}\LANChat.ico"; Comment: "启动 {#DisplayName}"; Tasks: desktopicon

[Run]
Filename: "{app}\LANChat.exe"; Description: "启动 {#DisplayName}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

; The uninstaller removes files installed above and the Windows uninstall entry.
; Host-created TLS keys and chat data live under the current user's LocalAppData
; directory and are intentionally outside the install tree. The uninstaller
; therefore never silently deletes a host identity or chat history.
