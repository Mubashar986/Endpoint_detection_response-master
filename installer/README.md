# EDR Agent Installer

This directory contains the Inno Setup installer for the EDR Agent.

## Prerequisites

1. **Inno Setup 6+**: Download from https://jrsoftware.org/isinfo.php
2. **Sysmon64.exe**: Download from https://docs.microsoft.com/en-us/sysinternals/downloads/sysmon
3. **Built Agent**: Build the agent first (see main README)

## Setup

1. Place `Sysmon64.exe` in the `resources/` folder
2. Optionally add an `icon.ico` to `resources/` for custom branding

## Building the Installer

### Using Inno Setup GUI
1. Open `setup.iss` in Inno Setup Compiler
2. Click Build → Compile
3. Output will be in `output/EDRAgent-Setup-X.X.X.exe`

### Using Command Line
```cmd
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" setup.iss
```

## Installer Features

- ✅ Admin privilege check
- ✅ Auth token prompt
- ✅ Sysmon installation (optional)
- ✅ Windows Service registration
- ✅ Auto-start service
- ✅ Clean uninstall

## Silent Install

```cmd
EDRAgent-Setup-1.0.0.exe /SILENT /SUPPRESSMSGBOXES /NORESTART /AUTH_TOKEN=your_token_here
```

## File Structure After Install

```
C:\Program Files\EDRAgent\
├── edr-agent.exe
├── config.json
├── auth.secret
├── sysmon-config.xml
├── Sysmon64.exe
└── logs\
    └── edr-agent.log
```
