#ifndef AppName
  #error AppName must be supplied from branding.json
#endif
#ifndef AppPrefix
  #error AppPrefix must be supplied from branding.json
#endif
#ifndef AppVersion
  #error AppVersion must be supplied from VERSION
#endif
#ifndef AppRepository
  #error AppRepository must be supplied from branding.json
#endif
#ifndef AppPublisher
  #error AppPublisher must be supplied from branding.json
#endif
#ifndef AppNameZh
  #define AppNameZh AppName
#endif
#ifndef SourceDir
  #error SourceDir must contain the complete release payload
#endif
#ifndef OutputDir
  #error OutputDir must be supplied
#endif
#ifndef TargetArch
  #error TargetArch must be x64 or arm64
#endif
#ifndef ApplicationId
  #define ApplicationId "{{BAE3DE00-B354-46A9-BD28-99AC2A89FAE0}"
#endif
#ifndef AppExeName
  #define AppExeName AppPrefix + "_pp.exe"
#endif
#ifndef OutputName
  #define OutputName AppPrefix + "_setup-release-" + AppVersion + "-" + TargetArch
#endif
#ifndef StartMenuDir
  #define StartMenuDir "{autoprograms}\" + AppName
#endif
#ifndef DesktopDir
  #define DesktopDir "{autodesktop}"
#endif
#ifndef StartupValue
  #define StartupValue "Jietuba"
#endif
#ifndef AdminTaskName
  #define AdminTaskName "Jietuba RunAsAdmin ({username})"
#endif

[Setup]
AppId={#ApplicationId}
AppName={cm:ProductName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/{#AppRepository}
AppSupportURL=https://github.com/{#AppRepository}/issues
AppUpdatesURL=https://github.com/{#AppRepository}/releases
DefaultDirName={localappdata}\Programs\{#AppName}
UsePreviousAppDir=yes
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
#if TargetArch == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#elif TargetArch == "x64"
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#else
  #error Unsupported architecture
#endif
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
DisableWelcomePage=no
SetupIconFile=..\app.ico
UninstallDisplayIcon={app}\{#AppExeName}
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
VersionInfoVersion={#AppVersion}
VersionInfoOriginalFileName={#OutputName}.exe
VersionInfoCompany={#AppPublisher}
LicenseFile=..\LICENSE
; Check the exact installed executable ourselves; never force-close a tray app.
CloseApplications=no
RestartApplications=no
SetupLogging=yes

[Languages]
Name: "chinesesimp"; MessagesFile: "languages\ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
english.ProductName={#AppName}
chinesesimp.ProductName={#AppNameZh}
english.AppRunning=Please exit %1 from its system tray menu, then retry.
chinesesimp.AppRunning=请从系统托盘退出 %1 后重试。
english.ProcessCheckFailed=Unable to check whether %1 is running. Close it and retry, or check Windows Management Instrumentation.
chinesesimp.ProcessCheckFailed=无法确认 %1 是否正在运行。请退出程序并重试，或检查 Windows 管理规范服务。
english.StartupTaskRemovalFailed=The elevated startup task could not be removed. Remove it in Windows Task Scheduler if it still points to this installation.
chinesesimp.StartupTaskRemovalFailed=无法删除提权开机启动任务。如果它仍指向此安装目录，请在 Windows 任务计划程序中删除。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\THIRD-PARTY-NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "languages\ChineseSimplified-LICENSE.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
; Models can be replaced by users or shared with a portable installation.
Source: "{#SourceDir}\models\PP-OCRv6_det_small.onnx"; DestDir: "{app}\models"; Flags: ignoreversion onlyifdoesntexist uninsneveruninstall
Source: "{#SourceDir}\models\PP-OCRv6_rec_small.onnx"; DestDir: "{app}\models"; Flags: ignoreversion onlyifdoesntexist uninsneveruninstall

[Icons]
Name: "{#StartMenuDir}\{cm:ProductName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{#StartMenuDir}\{cm:UninstallProgram,{cm:ProductName}}"; Filename: "{uninstallexe}"
Name: "{#DesktopDir}\{cm:ProductName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{cm:ProductName}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
function InstalledExecutableIsRunning: Boolean;
var
  Locator, Service, Processes: Variant;
  Path, Query: String;
begin
  Path := ExpandConstant('{app}\{#AppExeName}');
  StringChangeEx(Path, '\', '\\', True);
  Query := 'SELECT ProcessId FROM Win32_Process WHERE ExecutablePath = "' + Path + '"';
  Locator := CreateOleObject('WbemScripting.SWbemLocator');
  Service := Locator.ConnectServer('', 'root\CIMV2', '', '');
  Processes := Service.ExecQuery(Query);
  Result := Processes.Count > 0;
end;

function StopCheckMessage: String;
begin
  Result := '';
  try
    if InstalledExecutableIsRunning then
      Result := FmtMessage(CustomMessage('AppRunning'), [CustomMessage('ProductName')]);
  except
    Log('Could not query the installed executable: ' + GetExceptionMessage);
    Result := FmtMessage(CustomMessage('ProcessCheckFailed'), [CustomMessage('ProductName')]);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := StopCheckMessage;
end;

function InitializeUninstall: Boolean;
var
  Error: String;
begin
  Error := StopCheckMessage;
  Result := Error = '';
  if not Result then begin
    Log(Error);
    if not UninstallSilent then MsgBox(Error, mbError, MB_OK);
  end;
end;

procedure RemoveOwnedStartupEntries;
var
  Value, Target: String;
  Scheduler, Folder, Task, Action: Variant;
  TaskExists: Boolean;
begin
  Target := ExpandConstant('{app}\{#AppExeName}');
  if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#StartupValue}', Value) then
    if CompareText(RemoveQuotes(Trim(Value)), Target) = 0 then
      RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', '{#StartupValue}');
  TaskExists := False;
  try
    Scheduler := CreateOleObject('Schedule.Service');
    Scheduler.Connect;
    Folder := Scheduler.GetFolder('\');
    Task := Folder.GetTask(ExpandConstant('{#AdminTaskName}'));
    TaskExists := True;
    Action := Task.Definition.Actions.Item(1);
    if CompareText(RemoveQuotes(Trim(String(Action.Path))), Target) = 0 then begin
      Folder.DeleteTask(ExpandConstant('{#AdminTaskName}'), 0);
      TaskExists := False;
    end else
      TaskExists := False;
  except
    if TaskExists then begin
      Log('Could not remove the matching elevated startup task: ' + GetExceptionMessage);
      if not UninstallSilent then MsgBox(CustomMessage('StartupTaskRemovalFailed'), mbInformation, MB_OK);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then RemoveOwnedStartupEntries;
end;
