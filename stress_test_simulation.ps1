<#
.SYNOPSIS
    Generates security events to stress test the EDR Agent and Detection Engine.
    
.DESCRIPTION
    This script loops 50 times to generate a high volume of events that match 
    specific detection rules in the backend (seeds_rule.py).
    
    It simulates:
    1. Basic PowerShell Execution (RULE-PS-001)
    2. Suspicious Encoded PowerShell (RULE-PS-002)
    3. Credential Dumping Keywords (RULE-CRED-001)
    4. Suspicious Network Connections (RULE-NET-001)
    5. Suspicious File Locations (RULE-FILE-001)

.NOTES
    Author: Antigravity
    Date: 2025-11-20
#>

$LoopCount = 100
Write-Host "Starting Stress Test: Generating $LoopCount iterations of suspicious events..." -ForegroundColor Cyan

for ($i = 1; $i -le $LoopCount; $i++) {
    Write-Host "[$i/$LoopCount] Simulating attacks..." -NoNewline
    
    # 1. RULE-PS-001 & RULE-PS-002: Suspicious PowerShell
    # Triggers: powershell, -WindowStyle Hidden, -NonInteractive
    Start-Process powershell.exe -ArgumentList "-WindowStyle Hidden", "-NonInteractive", "-Command", "Write-Host 'Simulation'" -WindowStyle Hidden -Wait

    # 2. RULE-CRED-001: Credential Dumping Keywords
    # Triggers: mimikatz, procdump        
    # We use cmd.exe to just echo the string, which creates a Process Create event with the command line.
    Start-Process cmd.exe -ArgumentList "/c", "echo", "mimikatz_simulation_attempt" -WindowStyle Hidden -Wait

    Start-Process powershell.exe -ArgumentList "-WindowStyle ", "-NonInteractive", "-Command", "Write-Host 'Simulation'" -WindowStyle Hidden -Wait

    # 3. RULE-NET-001: Suspicious High Port Connection (> 8000)
    # Triggers: dest_port > 8000
    # We attempt a connection to localhost on port 8888 (it will fail fast, but generate a network event)
    try {
        $tcp = New-Object System.Net.Sockets.TcpClient
        $connect = $tcp.BeginConnect("127.0.0.1", 8888, $null, $null)
        Start-Sleep -Milliseconds 10
        $tcp.Close()
    } catch {
        # Ignore connection errors
    }

    # 4. RULE-FILE-001: Suspicious File Location
    # We copy a dummy executable to Temp and run it
    $dummyExe = "$env:TEMP\suspicious_test_$i.exe"
    Copy-Item "C:\Windows\System32\whoami.exe" -Destination $dummyExe -Force
    Start-Process $dummyExe -WindowStyle Hidden -Wait
    Remove-Item $dummyExe -Force

    Write-Host " Done." -ForegroundColor Green
    
    # Small sleep to prevent complete CPU starvation, but fast enough to stress the agent
    Start-Sleep -Milliseconds 50
}

Write-Host "Stress Test Complete!" -ForegroundColor Cyan
Write-Host "Check your dashboard for alerts." -ForegroundColor Yellow
