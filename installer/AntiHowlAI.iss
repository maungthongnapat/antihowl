[Setup]
AppName=AntiHowl AI
AppVersion=1.0.0
AppPublisher=AntiHowl
DefaultDirName={autopf}\AntiHowl AI
DefaultGroupName=AntiHowl AI
OutputBaseFilename=AntiHowlAI-Setup
OutputDir=Output
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\AntiHowl AI.exe
WizardStyle=modern
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\build\AntiHowlNative_artefacts\Release\AntiHowl AI.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\AntiHowl AI"; Filename: "{app}\AntiHowl AI.exe"
Name: "{autodesktop}\AntiHowl AI"; Filename: "{app}\AntiHowl AI.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\AntiHowl AI.exe"; Description: "Launch AntiHowl AI"; Flags: nowait postinstall skipifsilent
