# Manual Rate Limiting Test Script
# Tests burst protection (200/10s) on telemetry endpoint

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "   Rate Limiting Burst Test" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Configuration
$baseUrl = "http://localhost:8001"
$token = "55e3993a3633207b78a7e479c93213a3dc21223a"
$agentId = "MANUAL-BURST-TEST"

# Setup headers
$headers = @{
    "Authorization" = "Token $token"
    "X-Agent-Token" = $agentId
    "Content-Type"  = "application/json"
}

Write-Host "Configuration:" -ForegroundColor Yellow
Write-Host "  Server: $baseUrl"
Write-Host "  Agent: $agentId"
Write-Host "  Expected: First ~200 requests succeed, then 429 (Rate Limited)"
Write-Host ""

# Test: Send 20 rapid requests
Write-Host "Sending 300 rapid requests (NO DELAY)..." -ForegroundColor Green
Write-Host ""

$successCount = 0
$rateLimitedCount = 0
$errorCount = 0

for ($i = 1; $i -le 300; $i++) {
    # Get current Unix timestamp (seconds since epoch)
    $unixTimestamp = [int][double]::Parse((Get-Date -UFormat %s))
    
    # Build request body (matches C++ agent format)
    $body = @{
        event_id   = "manual-burst-$i"
        event_type = "file"
        timestamp  = $unixTimestamp
        agent_id   = $agentId
        hostname   = "manual-test-host"
        severity   = "INFO"
        version    = "1.0"
        host       = @{
            hostname = "manual-test"
            os       = "Windows"
        }
        file       = @{
            path      = "C:\test-$i.txt"
            operation = "write"
        }
    } | ConvertTo-Json -Compress

    try {
        $response = Invoke-WebRequest -Uri "$baseUrl/api/v1/telemetry/" `
            -Method POST `
            -Headers $headers `
            -Body $body `
            -UseBasicParsing

        $successCount++
        Write-Host "Request $($i.ToString().PadLeft(2)) : OK Status $($response.StatusCode) (Success)" -ForegroundColor Green
        
    }
    catch {
        $statusCode = $_.Exception.Response.StatusCode.value__
        
        if ($statusCode -eq 429) {
            $rateLimitedCount++
            Write-Host "Request $($i.ToString().PadLeft(2)) : RATE LIMITED Status 429" -ForegroundColor Yellow
            Write-Host ""
            Write-Host "RATE LIMITING IS WORKING!" -ForegroundColor Green -BackgroundColor Black
            Write-Host "   Burst limit triggered at request #$i" -ForegroundColor Green
            break
        }
        elseif ($statusCode -eq 400) {
            $errorCount++
            Write-Host "Request $($i.ToString().PadLeft(2)) : ERROR Status 400 (Bad Request)" -ForegroundColor Red
            Write-Host "                 ERROR: Data validation failed!" -ForegroundColor Red
            break
        }
        else {
            $errorCount++
            Write-Host "Request $($i.ToString().PadLeft(2)) : ERROR Status $statusCode" -ForegroundColor Red
        }
    }
    
    # Start-Sleep -Milliseconds 10
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "   TEST RESULTS" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Total Requests:  300" -ForegroundColor White
Write-Host "Successful:      $successCount" -ForegroundColor Green
Write-Host "Rate Limited:    $rateLimitedCount" -ForegroundColor Yellow
Write-Host "Errors:          $errorCount" -ForegroundColor Red
Write-Host ""

if ($rateLimitedCount -gt 0) {
    Write-Host "PASS - Rate limiting is working correctly!" -ForegroundColor Green -BackgroundColor Black
}
elseif ($errorCount -gt 0) {
    Write-Host "FAIL - Data validation errors" -ForegroundColor Red -BackgroundColor Black
}
elseif ($successCount -eq 300) {
    Write-Host "WARNING - All requests succeeded (rate limit may not be configured)" -ForegroundColor Yellow -BackgroundColor Black
}
else {
    Write-Host "INCONCLUSIVE - Unexpected result" -ForegroundColor Magenta -BackgroundColor Black
}

Write-Host ""
