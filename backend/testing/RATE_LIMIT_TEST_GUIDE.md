# Manual Rate Limiting Test Guide

## Prerequisites Check
- [x] Django server running (PID: 9848)
- [x] Redis server running
- [x] Test token: 55e3993a3633207b78a7e479c93213a3dc21223a

## Test 1: Verify Server is Running
```powershell
curl http://localhost:8000/api/v1/health/
```
**Expected**: `{"status":"ok"}`

## Test 2: Test Telemetry Endpoint (Normal Request)
```powershell
$headers = @{
    "Authorization" = "Token 55e3993a3633207b78a7e479c93213a3dc21223a"
    "X-Agent-Token" = "TEST-AGENT-001"
    "Content-Type" = "application/json"
}

$body = @{
    event_id = "test-001"
    event_type = "process_creation"
    timestamp = (Get-Date).ToString("o")
    agent_id = "TEST-AGENT-001"
    hostname = "test-host"
    severity = "INFO"
    data = @{ process_name = "test.exe"; pid = 1234 }
} | ConvertTo-Json

Invoke-WebRequest -Uri "http://localhost:8000/api/v1/telemetry/" `
    -Method POST `
    -Headers $headers `
    -Body $body
```
**Expected**: Status 201, response shows event queued

## Test 3: Check Redis Keys Created
```powershell
redis-cli -n 1 KEYS "rl:*"
```
**Expected**: Shows key like `rl:func:telemetry_endpoint:TEST-AGENT-001:60`

## Test 4: Check Redis Counter Value
```powershell
redis-cli -n 1 GET "rl:func:telemetry_endpoint:TEST-AGENT-001:60"
```
**Expected**: Shows "1" (or higher if multiple requests sent)

## Test 5: Test Rate Limit (Send Multiple Requests Rapidly)
```powershell
# Send 15 requests quickly to test burst limit (200/10s)
for ($i=1; $i -le 15; $i++) {
    $body = @{
        event_id = "burst-test-$i"
        event_type = "process_creation"  
        timestamp = (Get-Date).ToString("o")
        agent_id = "TEST-RATE-LIMIT"
        hostname = "test-host"
        severity = "INFO"
        data = @{}
    } | ConvertTo-Json
    
    $headers = @{
        "Authorization" = "Token 55e3993a3633207b78a7e479c93213a3dc21223a"
        "X-Agent-Token" = "TEST-RATE-LIMIT"
        "Content-Type" = "application/json"
    }
    
    try {
        $response = Invoke-WebRequest -Uri "http://localhost:8000/api/v1/telemetry/" `
            -Method POST -Headers $headers -Body $body -ErrorAction Stop
        Write-Host "Request $i`: ✅ Status $($response.StatusCode)"
    } catch {
        if ($_.Exception.Response.StatusCode -eq 429) {
            Write-Host "Request $i`: 🚫 RATE LIMITED (429)" -ForegroundColor Yellow
            Write-Host "   Rate limit is working!" -ForegroundColor Green
            break
        } else {
            Write-Host "Request $i`: ❌ Error $($_.Exception.Response.StatusCode)"
        }
    }
    Start-Sleep -Milliseconds 100
}
```
**Expected**: First 10-15 requests succeed, then 429 error

## Test 6: Verify Rate Limit Logging
Check server console output for:
```
[WARNING] ratelimit Rate limit exceeded | endpoint=telemetry_endpoint |  
key=header:HTTP_X_AGENT_TOKEN | value=TEST-RATE-LIMIT | rate=200/10s
```

## Test 7: Test Dashboard API
```powershell
$headers = @{
    "Authorization" = "Token 55e3993a3633207b78a7e479c93213a3dc21223a"
}

Invoke-WebRequest -Uri "http://localhost:8000/api/v1/dashboard/stats/" `
    -Method GET -Headers $headers
```
**Expected**: Status 200, returns dashboard statistics

## Test 8: Monitor Redis in Real-Time
Open a separate terminal:
```powershell
redis-cli -n 1
> MONITOR
```
Then run tests in another terminal and watch keys being created/updated

## Verification Checklist
- [ ] Server responds to health check
- [ ] Telemetry endpoint accepts normal requests
- [ ] Redis keys are created for rate limiting
- [ ] Rate limit counter increments correctly
- [ ] 429 error returned when limit exceeded
- [ ] Rate limit violations logged to console
- [ ] Dashboard API endpoints work
- [ ] Different endpoints have separate rate limits
- [ ] Counter resets after time window (60 seconds)

## Post-Test Cleanup
```powershell
# Clear all rate limit keys from Redis
redis-cli -n 1 FLUSHDB

# Stop Django server (if needed)
Get-Process python | Where-Object {$_.Id -eq 9848} | Stop-Process
```
