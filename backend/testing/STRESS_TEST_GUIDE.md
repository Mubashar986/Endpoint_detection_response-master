# Rate Limiting Stress Test Guide

## Quick Start

```powershell
cd c:\Endpoint_detection_response-master\backend
python testing/stress_test_suite.py
```

## Test Scenarios

### Test 1: Single Agent - Sustained Load
- **Duration**: 60 seconds
- **Requests**: 1500 total (25 req/sec)
- **Expected**: First 1000 succeed, remaining 500 rate limited
- **Purpose**: Validate sustained rate limit (1000/m)

### Test 2: Single Agent - Burst Traffic
- **Duration**: ~3 seconds
- **Requests**: 300 rapid fire
- **Expected**: First ~200 succeed (burst limit), rest rate limited  
- **Purpose**: Validate burst protection (200/10s)

### Test 3: Multiple Agents - Concurrent
- **Agents**: 10 simultaneous
- **Requests**: 100 per agent (1000 total)
- **Expected**: Each agent independently limited
- **Purpose**: Validate per-agent isolation

### Test 4: Rate Limit Boundary
- **Duration**: 60 seconds
- **Requests**: Exactly 1001
- **Expected**: Request #1001 is rate limited
- **Purpose**: Validate exact limit enforcement

### Test 5: Recovery After Limit
- **Phases**:
  1. Hit rate limit (250 rapid requests)
  2. Wait 70 seconds
  3. Retry (10 requests)
- **Expected**: Access restored after window resets
- **Purpose**: Validate time-based reset

### Test 6: Mixed Endpoint Stress
- **Endpoints**: Telemetry + Dashboard + Response Actions
- **Concurrent**: 3 workers simultaneously
- **Expected**: Each endpoint independently limited
- **Purpose**: Validate realistic multi-endpoint usage

## Test Selection Menu

When you run the script, you'll see:
```
Available Tests:
  1. Single Agent - Sustained Load
  2. Single Agent - Burst Traffic
  3. Multiple Agents - Concurrent
  4. Rate Limit Boundary
  5. Recovery After Limit
  6. Mixed Endpoint Stress
  7. Run ALL tests

Select test to run (1-7):
```

## Interpreting Results

### Success Metrics
- ✅ **Successful**: Requests accepted (200/201 status)
- 🚫 **Rate Limited**: Requests blocked (429 status)
- ❌ **Errors**: Unexpected failures

### Example Output
```
TEST COMPLETE: Single Agent - Sustained Load
Duration: 60.45s
Total Requests: 1500
✅ Successful: 1000
🚫 Rate Limited (429): 500
❌ Errors: 0

Response Times:
  Average: 45.23ms
  Min: 12.34ms
  Max: 156.78ms
```

### What to Look For

#### Good Signs ✅
- Rate limiting kicks in at expected thresholds
- 429 responses returned correctly
- Response times remain consistent
- No unexpected errors or crashes
- Rate limits reset after time window

#### Warning Signs ⚠️
- Rate limit triggers too early/late
- High error rate (non-429)
- Increasing response times (performance degradation)
- Server crashes or timeouts
- Rate limits don't reset

## Common Issues and Fixes

### Issue: All requests failing with connection errors
**Cause**: Django server not running
**Fix**: `python manage.py runserver`

### Issue: All requests get 401 Unauthorized
**Cause**: Invalid auth token
**Fix**: Update `AUTH_TOKEN` in script with valid token:
```python
# Get valid token
python manage.py shell -c "from rest_framework.authtoken.models import Token; print(Token.objects.first().key)"
```

### Issue: Rate limit never triggers
**Cause**: Django-ratelimit not configured or Redis not running
**Fix**: 
```powershell
# Check Redis
redis-server

# Check Django settings
python manage.py shell -c "from django.conf import settings; print(settings.RATELIMIT_ENABLE)"
```

### Issue: Too many rate limits (limits too strict)
**Cause**: Burst limit firing before sustained limit  
**Fix**: Adjust limits in `settings.py` or `.env`

## Monitoring During Tests

### Watch Redis Keys
```powershell
# In separate terminal
redis-cli -n 1 MONITOR
```

### Watch Django Logs
Django console will show:
```
[WARNING] ratelimit Rate limit exceeded | endpoint=telemetry_endpoint | 
key=header:HTTP_X_AGENT_TOKEN | value=STRESS-TEST-BURST | rate=200/10s
```

### Check Redis Counters
```powershell
redis-cli -n 1 KEYS "rl:*"
redis-cli -n 1 GET "rl:func:telemetry_endpoint:STRESS-TEST-BURST:10"
```

## Performance Expectations

### Normal Operation
- Response time: 20-100ms
- Successful rate: >95% (until limit hit)
- Error rate: <1%

### Under Rate Limit
- 429 responses: Immediate (<5ms)
- No server errors
- Other clients unaffected

### Resource Usage
- Redis memory: <10MB for rate limit keys
- Django process: Normal CPU usage
- No memory leaks after tests

## Advanced: Custom Test Creation

### Template for Custom Test
```python
def test_custom():
    """Your custom test scenario"""
    reporter.start_test("Custom Test Name")
    
    url = f"{BASE_URL}/api/v1/your-endpoint/"
    headers = {'Authorization': f'Token {AUTH_TOKEN}'}
    
    for i in range(YOUR_REQUEST_COUNT):
        result = make_request(url, headers, YOUR_DATA, method='POST')
        # Add your assertions here
    
    reporter.end_test()
```

## Post-Test Cleanup

### Clear Redis Keys
```powershell
redis-cli -n 1 FLUSHDB
```

### Check for Leftover Resources
```powershell
# Check Python processes
Get-Process python

# Check Redis memory
redis-cli -n 1 INFO memory
```

## Stress Test Report Template

After running tests, document:
1. Test date/time
2. Which tests were run
3. Pass/fail for each test
4. Any anomalies observed
5. Performance metrics
6. Recommendations for limit adjustments
