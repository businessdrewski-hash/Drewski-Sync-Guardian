; Sync Guardian installer
#define MyAppName "Sync Guardian"
#ifndef MyAppVersion
  #define MyAppVersion "0.3.3"
#endif
#ifndef SourceRoot
  #define SourceRoot "obs-install"
#endif
#ifndef OutputRoot
  #define OutputRoot "dist"
#endif

[Setup]
AppId={{A9DFA6EC-2694-4D5A-BD92-5A2D27DA31B8}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher=Sync Guardian contributors
DefaultDirName={autopf}\obs-studio
DefaultGroupName={#MyAppName}
DisableDirPage=no
DisableProgramGroupPage=yes
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin
OutputDir={#OutputRoot}
OutputBaseFilename=SyncGuardian-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\obs-plugins\64bit\sync-guardian.dll
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "launchobs"; Description: "Launch OBS after setup"; Flags: unchecked

[Files]
Source: "{#SourceRoot}\obs-plugins\64bit\sync-guardian.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
Source: "{#SourceRoot}\data\obs-plugins\sync-guardian\locale\en-US.ini"; DestDir: "{app}\data\obs-plugins\sync-guardian\locale"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\INSTALL.txt"; DestDir: "{app}\data\obs-plugins\sync-guardian"; Flags: ignoreversion

[Dirs]
Name: "{app}\obs-plugins\64bit"
Name: "{app}\data\obs-plugins\sync-guardian\locale"

[Code]
function InitializeSetup(): Boolean;
var
  ResultCode: Integer;
begin
  if Exec(ExpandConstant('{cmd}'), '/c tasklist /FI "IMAGENAME eq obs64.exe" | find /I "obs64.exe"', '', SW_HIDE,
      ewWaitUntilTerminated, ResultCode) and (ResultCode = 0) then begin
    MsgBox('Please close OBS before installing Sync Guardian.', mbError, MB_OK);
    Result := False;
  end else begin
    Result := True;
  end;
end;

[Run]
Filename: "{app}\bin\64bit\obs64.exe"; Description: "Launch OBS"; Flags: postinstall skipifsilent; Tasks: launchobs
