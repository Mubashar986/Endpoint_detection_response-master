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
; Note: config.json is generated during install from user input (ServerConfigPage)
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
  // Server Configuration Page
  ServerConfigPage: TInputQueryWizardPage;
  ServerUrl: String;
  ServerPort: String;
  UseHttpsCheckBox: TNewCheckBox;
  EnablePollingCheckBox: TNewCheckBox;
  UseHttps: Boolean;
  EnablePolling: Boolean;
  
  // Auth Token Page
  AuthTokenPage: TInputQueryWizardPage;
  AuthToken: String;

// Create custom pages for server config and auth token
procedure InitializeWizard;
var
  CheckBoxLabel: TNewStaticText;
begin
  // ============================================
  // Page 1: Server Configuration
  // ============================================
  ServerConfigPage := CreateInputQueryPage(wpSelectTasks,
    'Server Configuration',
    'Enter your EDR server connection details',
    'Specify the server address where this agent will send telemetry data. ' +
    'For ngrok testing, use your ngrok URL (e.g., abc123.ngrok.io).');
  
  ServerConfigPage.Add('Server Address (e.g., localhost or abc123.ngrok.io):', False);
  ServerConfigPage.Add('Server Port (e.g., 8000 for HTTP, 443 for HTTPS):', False);
  
  // Set defaults
  ServerConfigPage.Values[0] := 'localhost';
  ServerConfigPage.Values[1] := '8000';
  
  // Add HTTPS checkbox
  CheckBoxLabel := TNewStaticText.Create(ServerConfigPage);
  CheckBoxLabel.Parent := ServerConfigPage.Surface;
  CheckBoxLabel.Caption := 'Connection Security:';
  CheckBoxLabel.Top := ServerConfigPage.Edits[1].Top + ServerConfigPage.Edits[1].Height + 16;
  CheckBoxLabel.Left := 0;
  
  UseHttpsCheckBox := TNewCheckBox.Create(ServerConfigPage);
  UseHttpsCheckBox.Parent := ServerConfigPage.Surface;
  UseHttpsCheckBox.Caption := 'Use HTTPS/WSS (recommended for production and ngrok)';
  UseHttpsCheckBox.Top := CheckBoxLabel.Top + CheckBoxLabel.Height + 4;
  UseHttpsCheckBox.Left := 0;
  UseHttpsCheckBox.Width := ServerConfigPage.SurfaceWidth;
  UseHttpsCheckBox.Checked := False;
  
  // Add HTTP Polling checkbox (fallback when WebSocket unavailable)
  EnablePollingCheckBox := TNewCheckBox.Create(ServerConfigPage);
  EnablePollingCheckBox.Parent := ServerConfigPage.Surface;
  EnablePollingCheckBox.Caption := 'Enable HTTP Command Polling (fallback if WebSocket unavailable)';
  EnablePollingCheckBox.Top := UseHttpsCheckBox.Top + UseHttpsCheckBox.Height + 8;
  EnablePollingCheckBox.Left := 0;
  EnablePollingCheckBox.Width := ServerConfigPage.SurfaceWidth;
  EnablePollingCheckBox.Checked := True;  // Enabled by default for reliability
  
  // ============================================
  // Page 2: Enrollment Token
  // ============================================
  AuthTokenPage := CreateInputQueryPage(ServerConfigPage.ID,
    'Agent Enrollment',
    'Enter the one-time registration token',
    'To register this agent, you need an Enrollment Token:' + #13#10 + #13#10 +
    '1. Log into your EDR Dashboard' + #13#10 +
    '2. Go to Agent Management → Tokens' + #13#10 +
    '3. Click "Generate New Token"' + #13#10 +
    '4. Copy and paste the token below');
  AuthTokenPage.Add('Enrollment Token:', False);
  AuthTokenPage.Values[0] := '';
end;


// Validate server config and auth token
function NextButtonClick(CurPageID: Integer): Boolean;
var
  PortNum: Integer;
begin
  Result := True;
  
  // Validate Server Configuration Page
  if CurPageID = ServerConfigPage.ID then
  begin
    ServerUrl := ServerConfigPage.Values[0];
    ServerPort := ServerConfigPage.Values[1];
    UseHttps := UseHttpsCheckBox.Checked;
    EnablePolling := EnablePollingCheckBox.Checked;
    
    if ServerUrl = '' then
    begin
      MsgBox('Please enter a server address.', mbError, MB_OK);
      Result := False;
    end
    else if ServerPort = '' then
    begin
      // Smart default: use 443 for HTTPS, 8000 for HTTP
      if UseHttps then
        ServerPort := '443'
      else
        ServerPort := '8000';
      // Update the field so user sees the default
      ServerConfigPage.Values[1] := ServerPort;
    end
    else
    begin
      // Validate port is a number
      PortNum := StrToIntDef(ServerPort, -1);
      if (PortNum < 1) or (PortNum > 65535) then
      begin
        MsgBox('Please enter a valid port number (1-65535).', mbError, MB_OK);
        Result := False;
      end;
    end;
  end;
  
  // Validate Enrollment Token Page
  if CurPageID = AuthTokenPage.ID then
  begin
    AuthToken := AuthTokenPage.Values[0];
    if AuthToken = '' then
    begin
      MsgBox('Please enter an Enrollment Token.' + #13#10 + #13#10 +
             'Generate one from your EDR Dashboard:' + #13#10 +
             'Agent Management → Tokens → Generate New Token', mbError, MB_OK);
      Result := False;
    end
    else if Length(AuthToken) < 20 then
    begin
      MsgBox('The Enrollment Token appears too short.' + #13#10 + #13#10 +
             'A valid token should be at least 40 characters.' + #13#10 +
             'Please copy the complete token from your EDR Dashboard.', mbError, MB_OK);
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

// Write config and install service after files are copied
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  AuthSecretPath, ConfigPath, ConfigContent: String;
  HttpsStr, WsProtocol, WsUri, PollingStr: String;
begin
  if CurStep = ssPostInstall then
  begin
    // NOTE: We do NOT write auth.secret here!
    // The agent will:
    //   1. Read enrollment_token from config.json
    //   2. Call /api/v1/enroll/ to register
    //   3. Save the returned identity_token to auth.secret
    
    // Generate config.json with user's server settings
    ConfigPath := ExpandConstant('{app}\config.json');
    
    if UseHttps then
    begin
      HttpsStr := 'true';
      WsProtocol := 'wss://';
    end
    else
    begin
      HttpsStr := 'false';
      WsProtocol := 'ws://';
    end;
    
    // Set HTTP polling based on user selection
    if EnablePolling then
      PollingStr := 'true'
    else
      PollingStr := 'false';
    
    // For standard ports (443 for HTTPS, 80 for HTTP), omit port from WebSocket URI
    // This is cleaner and avoids issues with proxies like ngrok
    if (UseHttps and (ServerPort = '443')) or ((not UseHttps) and (ServerPort = '80')) then
      WsUri := WsProtocol + ServerUrl + '/ws/agent/'
    else
      WsUri := WsProtocol + ServerUrl + ':' + ServerPort + '/ws/agent/';
    
    ConfigContent := '{' + #13#10 +
      '  "config_version": 1,' + #13#10 +
      '  "http_server": "' + ServerUrl + '",' + #13#10 +
      '  "http_port": ' + ServerPort + ',' + #13#10 +
      '  "use_https": ' + HttpsStr + ',' + #13#10 +
      '  "api_path": "/api/v1/telemetry/",' + #13#10 +
      // Enrollment token - used ONCE to register, then agent saves identity_token to auth.secret
      '  "enrollment_token": "' + AuthToken + '",' + #13#10 +
      '  "uri": "' + WsUri + '",' + #13#10 +
      '  "websocket_uri": "' + WsUri + '",' + #13#10 +
      '  "enable_http_polling": ' + PollingStr + ',' + #13#10 +
      '  "event_processor": {' + #13#10 +
      '    "source": [' + #13#10 +
      '      {' + #13#10 +
      '        "path": "Microsoft-Windows-Sysmon/Operational",' + #13#10 +
      '        "query": "*"' + #13#10 +
      '      },' + #13#10 +
      '      {' + #13#10 +
      '        "path": "Microsoft-Windows-PowerShell/Operational",' + #13#10 +
      '        "query": "*[System[(EventID=4104)]]"' + #13#10 +
      '      }' + #13#10 +
      '    ]' + #13#10 +
      '  },' + #13#10 +
      '  "command_processor": {' + #13#10 +
      '    "reverse_shell": {' + #13#10 +
      '      "ip": "0.0.0.0",' + #13#10 +
      '      "port": 4444' + #13#10 +
      '    }' + #13#10 +
      '  }' + #13#10 +
      '}';
    
    SaveStringToFile(ConfigPath, ConfigContent, False);
    
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
