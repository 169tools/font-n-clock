; Arguments:
; - MyAppVersion: Version in buildspec.json
; - SourceDir: Output from `cmake --install`
; - OutputDir: Installer output location

#define MyAppName "Font-n-Clock"

[Setup]
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=mizznoff
AppPublisherURL=https://github.com/169tools/font-n-clock
DefaultDirName={commonappdata}\obs-studio\plugins\{#MyAppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesinstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFileName={#MyAppName}-{#MyAppVersion}-windows-x64-Installer
WizardStyle=modern

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
