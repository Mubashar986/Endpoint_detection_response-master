; ============================================================================
; EDR Agent Installer - Inno Setup Script
; ============================================================================
; Creates a Windows installer that:
; - Bundles EDR Agent, Sysmon, and configuration
; - Prompts for authentication token
; - Installs Sysmon with custom configuration
; - Registers and starts Windows service
; - Provides uninstall capability
; ============================================================================

#define MyAppName "EDR Agent"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "Your Organization"
#define MyAppExeName "edr-agent.exe"
#define MyServiceName "EDRAgent"

[Setup]
; Application information
AppId={{A1B2C3D4-E5F6-7890-ABCD-EF1234567890}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\EDRAgent
DefaultGroupName={#MyAppName}
AllowNoIcons=yes

; Output settings
OutputDir=output
OutputBaseFilename=EDRAgent-Setup-{#MyAppVersion}
SetupIconFile=resources\icon.ico
Compression=lzma2
SolidCompression=yes

; Privileges
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog

; Installer appearance
WizardStyle=modern
WizardSizePercent=120

; Uninstall settings
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "installsysmon"; Description: "Install/Update Sysmon (Required for event monitoring)"; GroupDescription: "Components:"; Flags: checkedonce

[Files]
; Main application files
Source: "..\edr-agent\build\Release\edr-agent.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\edr-agent\config.json"; DestDir: "{app}"; Flags: ignoreversion onlyifdoesntexist
Source: "..\edr-agent\sysmon-config.xml"; DestDir: "{app}"; Flags: ignoreversion

; Sysmon files (user must provide Sysmon64.exe)
Source: "resources\Sysmon64.exe"; DestDir: "{app}"; Flags: ignoreversion; Tasks: installsysmon

[Dirs]
; Create logs directory
Name: "{app}\logs"

[Icons]
Name: "{group}\EDR Agent Status"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--status"
Name: "{group}\Uninstall EDR Agent"; Filename: "{uninstallexe}"

[Code]
var
  AuthTokenPage: TInputQueryWizardPage;
  AuthToken: String;

// Create custom page for auth token input
procedure InitializeWizard;
begin
  AuthTokenPage := CreateInputQueryPage(wpSelectTasks,
    'Authentication Token',
    'Enter your EDR server authentication token',
    'The token is required for the agent to communicate with the server. ' +
    'You can find this token in your EDR Dashboard under Settings > Agent Tokens.');
  AuthTokenPage.Add('Auth Token:', False);
  AuthTokenPage.Values[0] := '';
end;

// Validate auth token is provided
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  
  if CurPageID = AuthTokenPage.ID then
  begin
    AuthToken := AuthTokenPage.Values[0];
    if AuthToken = '' then
    begin
      MsgBox('Please enter an authentication token. This is required for the agent to function.', mbError, MB_OK);
      Result := False;
    end
    else if Length(AuthToken) < 10 then
    begin
      MsgBox('The authentication token appears too short. Please verify you have the correct token.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

// Stop existing service before installation
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Result := '';
  
  // Try to stop existing service
  Exec('net', 'stop ' + '{#MyServiceName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  
  // Try to delete existing service
  Exec('sc', 'delete ' + '{#MyServiceName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  
  // Wait a moment for service to be removed
  Sleep(1000);
end;

// Write auth token and install service after files are copied
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  AuthSecretPath: String;
begin
  if CurStep = ssPostInstall then
  begin
    // Write auth.secret file
    AuthSecretPath := ExpandConstant('{app}\auth.secret');
    SaveStringToFile(AuthSecretPath, AuthToken, False);
    
    // Install Sysmon if selected
    if WizardIsTaskSelected('installsysmon') then
    begin
      // Uninstall existing Sysmon first (ignore errors)
      Exec(ExpandConstant('{app}\Sysmon64.exe'), '-u force', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
      Sleep(1000);
      
      // Install Sysmon with our configuration
      if not Exec(ExpandConstant('{app}\Sysmon64.exe'), 
                  '-accepteula -i "' + ExpandConstant('{app}\sysmon-config.xml') + '"',
                  '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
      begin
        MsgBox('Warning: Sysmon installation may have failed. Error code: ' + IntToStr(ResultCode), mbError, MB_OK);
      end;
    end;
    
    // Install EDR Agent service
    if not Exec(ExpandConstant('{app}\{#MyAppExeName}'), '--install', 
                ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    begin
      MsgBox('Warning: Service installation may have failed. Error code: ' + IntToStr(ResultCode), mbError, MB_OK);
    end;
    
    // Start the service
    Exec('net', 'start ' + '{#MyServiceName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

// Uninstall: Stop service, remove service, optionally remove Sysmon
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
  RemoveSysmon: Boolean;
begin
  if CurUninstallStep = usUninstall then
  begin
    // Stop the service
    Exec('net', 'stop ' + '{#MyServiceName}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(1000);
    
    // Uninstall the service
    Exec(ExpandConstant('{app}\{#MyAppExeName}'), '--uninstall', 
         ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, ResultCode);
    
    // Ask user about Sysmon
    RemoveSysmon := MsgBox('Do you want to also remove Sysmon?' + #13#10 + 
                           'Note: Other applications may depend on Sysmon.',
                           mbConfirmation, MB_YESNO) = IDYES;
    
    if RemoveSysmon then
    begin
      Exec(ExpandConstant('{app}\Sysmon64.exe'), '-u force', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    end;
  end;
end;

[Run]
; Don't auto-run service here - we do it in CurStepChanged for better control
; Filename: "net"; Parameters: "start {#MyServiceName}"; Flags: runhidden nowait

[UninstallRun]
; Service cleanup is handled in CurUninstallStepChanged
; Filename: "net"; Parameters: "stop {#MyServiceName}"; Flags: runhidden
