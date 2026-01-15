#define Name "MusicPP"
#define ExeName "musicpp.exe"

#ifndef Version
  #define Version "0.0.0"
#endif

#ifndef SourceDir
  #define SourceDir "..\dist"
#endif

#ifndef LicenseFilePath
  #define LicenseFilePath "..\LICENSE.md"
#endif

#ifndef SetupIconFilePath
  #define SetupIconFilePath "..\resources\app-icon.ico"
#endif

#ifndef OutputDir
  #define OutputDir "..\installer_out"
#endif

#ifndef OutputBaseFilename
  #define OutputBaseFilename "musicpp-" + Version + "-setup"
#endif

[Setup]
AppId={{E01FBADD-C493-4950-8BDF-1095A962FCC9}}
AppName={#Name}
AppVersion={#Version}
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
DefaultDirName={autopf}\{#Name}
UninstallDisplayIcon={app}\{#ExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
LicenseFile={#LicenseFilePath}
PrivilegesRequired=lowest
SetupIconFile={#SetupIconFilePath}
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#ExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#Name}"; Filename: "{app}\{#ExeName}"
Name: "{autodesktop}\{#Name}"; Filename: "{app}\{#ExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#ExeName}"; Description: "{cm:LaunchProgram,{#StringChange(Name, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

