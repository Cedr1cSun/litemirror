; Inno Setup script for LiteMirror. build.ps1 -Package compiles it and passes
; /DVersion=<app version> and /DBin=<folder with the Release build>.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Bin
  #define Bin "..\build\bin\Release"
#endif

[Setup]
AppId={{2E322BED-CAC1-4D3A-AAB4-716A34CA3677}
AppName=LiteMirror
AppVersion={#Version}
AppPublisher=LiteMirror contributors
AppPublisherURL=https://github.com/Cedr1cSun/litemirror
DefaultDirName={autopf}\LiteMirror
DefaultGroupName=LiteMirror
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\build
OutputBaseFilename=LiteMirror-{#Version}-setup
SetupIconFile=..\src\app\LiteMirror\Resources\icon.ico
UninstallDisplayIcon={app}\LiteMirror.exe
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809: DnsServiceRegister, which the mDNS announcement uses
MinVersion=10.0.17763
PrivilegesRequired=admin
CloseApplications=yes
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zh"; MessagesFile: "ChineseSimplified.isl"

[CustomMessages]
en.NeedNet=LiteMirror needs .NET Framework 4.8, which Windows 10 version 1903 and later include. Install it from Windows Update and run Setup again.
zh.NeedNet=LiteMirror 需要 .NET Framework 4.8（Windows 10 1903 及更新版本自带）。请通过 Windows 更新安装后再运行安装程序。

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Bin}\LiteMirror.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\LiteMirror.exe.config"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\litemirror.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\HandyControl.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Bin}\zh-CN\LiteMirror.resources.dll"; DestDir: "{app}\zh-CN"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\LiteMirror"; Filename: "{app}\LiteMirror.exe"
Name: "{autodesktop}\LiteMirror"; Filename: "{app}\LiteMirror.exe"; Tasks: desktopicon

[Run]
; inbound rule on private networks, so the first phone is not stopped by the firewall prompt
Filename: "netsh"; Parameters: "advfirewall firewall add rule name=""LiteMirror"" dir=in action=allow program=""{app}\LiteMirror.exe"" enable=yes profile=private,domain"; Flags: runhidden
Filename: "{app}\LiteMirror.exe"; Description: "{cm:LaunchProgram,LiteMirror}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "netsh"; Parameters: "advfirewall firewall delete rule name=""LiteMirror"""; Flags: runhidden; RunOnceId: "firewall"

[Code]
function InitializeSetup: Boolean;
var
  Release: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full', 'Release', Release) and (Release >= 528040);
  if not Result then
    MsgBox(CustomMessage('NeedNet'), mbError, MB_OK);
end;
