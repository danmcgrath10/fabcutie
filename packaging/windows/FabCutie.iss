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
DisableDirPage=no
UsePreviousAppDir=yes
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
Source: "{#Artefacts}\VST3\FabCutie.vst3\*"; DestDir: "{code:PluginDir|vst3}\FabCutie.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\CLAP\FabCutie.clap"; DestDir: "{code:PluginDir|clap}"; Components: clap; Flags: ignoreversion
Source: "{#Artefacts}\Standalone\FabCutie.exe"; DestDir: "{app}"; Components: app; Flags: ignoreversion

[Messages]
SelectDirLabel3=Setup will install the FabCutie standalone app into the following folder. You can pick the VST3 and CLAP folders after choosing the formats.

[Icons]
Name: "{autoprograms}\FabCutie"; Filename: "{app}\FabCutie.exe"; Components: app

[Code]
// The first page (the usual folder page) picks where the standalone app
// goes. After the formats are chosen, this page picks the VST3 and CLAP
// folders. They default to the standard shared folders that every host
// scans, and the last choice is remembered for the next update.
var
  PluginDirPage: TInputDirWizardPage;

procedure InitializeWizard;
begin
  PluginDirPage := CreateInputDirPage(wpSelectComponents,
    'Plug-in folders', 'Where should the plug-ins be installed?',
    'Most hosts look in the standard folders below. Only change them if your host scans a different folder.',
    False, '');
  PluginDirPage.Add('VST3 folder:');
  PluginDirPage.Add('CLAP folder:');
  PluginDirPage.Values[0] := GetPreviousData('VST3Dir', ExpandConstant('{commoncf64}\VST3'));
  PluginDirPage.Values[1] := GetPreviousData('CLAPDir', ExpandConstant('{commoncf64}\CLAP'));

  // Silent installs can set them too: /VST3DIR="..." /CLAPDIR="..."
  // (and /DIR="..." for the app).
  if ExpandConstant('{param:VST3DIR}') <> '' then
    PluginDirPage.Values[0] := ExpandConstant('{param:VST3DIR}');
  if ExpandConstant('{param:CLAPDIR}') <> '' then
    PluginDirPage.Values[1] := ExpandConstant('{param:CLAPDIR}');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = PluginDirPage.ID) and not (WizardIsComponentSelected('vst3') or WizardIsComponentSelected('clap'));
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'VST3Dir', PluginDirPage.Values[0]);
  SetPreviousData(PreviousDataKey, 'CLAPDir', PluginDirPage.Values[1]);
end;

function PluginDir(Format: String): String;
begin
  if Format = 'vst3' then
    Result := PluginDirPage.Values[0]
  else
    Result := PluginDirPage.Values[1];
end;
