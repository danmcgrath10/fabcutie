; Inno Setup script for the Windows installer: VST3, CLAP and the standalone
; app, each optional. Built by CI with
;   iscc /DAppVersion=<version> /DArtefacts=<artefacts dir> /O<output dir> packaging\windows\FabCutie.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\FabCutie_artefacts\Release"
#endif

[Setup]
AppId={{5E0C7B1A-3F7D-4C55-9A5E-FA8C0E1D2B47}
AppName=FabCutie
AppVersion={#AppVersion}
AppPublisher=FabCutie
AppPublisherURL=https://github.com/danmcgrath10/fabcutie
DefaultDirName={autopf}\FabCutie
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\..\LICENSE
OutputBaseFilename=FabCutie-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=FabCutie

[Types]
Name: "full"; Description: "Everything"
Name: "custom"; Description: "Choose formats"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom
Name: "app";  Description: "Standalone app"; Types: full custom

[Files]
Source: "{#Artefacts}\VST3\FabCutie.vst3\*"; DestDir: "{commoncf64}\VST3\FabCutie.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\CLAP\FabCutie.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\FabCutie.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\FabCutie"; Filename: "{app}\FabCutie.exe"; Components: app
