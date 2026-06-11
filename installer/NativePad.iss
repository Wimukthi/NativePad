; Inno Setup script for NativePad.
; The build wrapper passes AppVersion from the executable resource so installer
; names and Windows uninstall metadata stay aligned with the binary.

#ifndef AppVersion
#define AppVersion "1.0.0.0"
#endif

#ifndef Configuration
#define Configuration "Release"
#endif

#ifndef Platform
#define Platform "x64"
#endif

#define AppName "NativePad"
#define AppPublisher "Wimukthi Bandara"
#define SourceRoot ".."

[Setup]
AppId={{B94E98F3-7F1A-4479-A0F0-F8F52F3198DD}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\NativePad
DefaultGroupName=NativePad
DisableProgramGroupPage=yes
LicenseFile={#SourceRoot}\LICENSE
OutputDir=output
OutputBaseFilename=NativePadSetup-{#AppVersion}-win-x64
SetupIconFile={#SourceRoot}\Icons\NativePad.ico
UninstallDisplayIcon={app}\NativePad.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes
ChangesAssociations=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceRoot}\bin\{#Platform}\{#Configuration}\NativePad.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceRoot}\docs\*"; DestDir: "{app}\docs"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\NativePad"; Filename: "{app}\NativePad.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\NativePad"; Filename: "{app}\NativePad.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Registry]
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\NativePad.exe"; ValueType: string; ValueName: ""; ValueData: "{app}\NativePad.exe"; Flags: uninsdeletekey
Root: HKLM; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\NativePad.exe"; ValueType: string; ValueName: "Path"; ValueData: "{app}"; Flags: uninsdeletekey

[Run]
Filename: "{app}\NativePad.exe"; Description: "Launch NativePad"; Flags: nowait postinstall skipifsilent
