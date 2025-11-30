# Rate Limiting Implementation Guide
## EDR System Security Hardening - Phase 4

---

## 📚 Table of Contents
1. [What is Rate Limiting?](#what-is-rate-limiting)
2. [Why Our EDR System Needs It](#why-our-edr-system-needs-it)
3. [How Rate Limiting Works](#how-rate-limiting-works)
4. [Implementation Strategy](#implementation-strategy)
5. [Django-Ratelimit Explained](#django-ratelimit-explained)
6. [Endpoint-by-Endpoint Analysis](#endpoint-by-endpoint-analysis)
7. [System Impact Analysis](#system-impact-analysis)
8. [Configuration & Tuning](#configuration--tuning)
9. [Monitoring & Troubleshooting](#monitoring--troubleshooting)

---

## 🎯 What is Rate Limiting?

### Definition
**Rate limiting** is a security technique that controls how many requests a client can make to a server within a specific time window. Think of it as a **bouncer at a nightclub** who only allows a certain number of people to enter per minute.

### Real-World Analogy
Imagine a water pipe:
- **No Rate Limit**: Like a burst pipe - unlimited water flow floods your house
- **With Rate Limit**: Like a controlled faucet - steady, manageable flow

### How It Works (Conceptually)
```
┌─────────────────────────────────────────────────────────────┐
│  Client sends requests                                       │
│  ↓                                                           │
│  Rate Limiter checks:                                        │
│    • Who is making the request? (IP/User/Token)              │
│    • How many requests in the last N seconds/minutes?        │
│    • Is this under the configured limit?                     │
│  ↓                                                           │
│  Decision:                                                   │
│    ✅ Under limit → Allow request → Process normally        │
│    ❌ Over limit → Block request → Return 429 error         │
└─────────────────────────────────────────────────────────────┘
```

### Key Components
1. **Rate**: The maximum allowed requests (e.g., `100/min` = 100 requests per minute)
2. **Key**: How we identify the requester (IP address, user ID, API token, etc.)
3. **Method**: Which HTTP methods to limit (GET, POST, etc.)
4. **Action**: What happens when limit is exceeded (block, throttle, log)

---

## 🚨 Why Our EDR System Needs It

### Current Vulnerability
**Right now, your system has NO protection against:**

#### 1. **Denial of Service (DoS) Attack**
```
Attacker → Sends 10,000 requests/second
         ↓
Your Server → CPU 100%, Memory Exhausted
         ↓
Result → System Crash, No telemetry processed
```

#### 2. **Rogue Agent Scenario**
```
Misconfigured Agent → Infinite loop, sends events continuously
                    ↓
Server → Processes millions of duplicate events
       ↓
Result → Database bloat, Celery queue overflow, System slowdown
```

#### 3. **Credential Stuffing / Brute Force**
```
Attacker → Tries 1000 passwords/minute on /api/v1/auth
         ↓
Result → Eventual account compromise
```

#### 4. **Resource Exhaustion**
Without limits, a single misbehaving client can:
- Fill your **Celery queue** with millions of tasks
- Exhaust **Redis memory**
- Overwhelm your **MongoDB** with writes
- Block legitimate agents from sending critical alerts

### Real-World EDR Incident (Hypothetical)
> **Scenario**: A security researcher discovers your telemetry endpoint. They write a script to "test" it with 50,000 fake events. Your server crashes in 30 seconds. During the 10-minute downtime:
> - 50 real agents can't report telemetry
> - A **real ransomware attack** happens on a machine
> - Your EDR is **blind** to it because the server is down

**Rate limiting prevents this.**

---

## 🔧 How Rate Limiting Works

### Algorithm: Token Bucket (Used by django-ratelimit)

Think of it like a **bucket of tokens**:

```
┌──────────────────────────────────────────────────────────────┐
│  Bucket: [🪙 🪙 🪙 🪙 🪙]  ← Max 5 tokens (= 5 requests/min) │
│                                                              │
│  Every minute: Bucket refills to max capacity               │
│                                                              │
│  Request arrives:                                            │
│    ✅ Token available? → Consume token → Allow request      │
│    ❌ Bucket empty? → Reject request → 429 Too Many         │
└──────────────────────────────────────────────────────────────┘
```

### Example Flow
**Configuration**: `100/min` (100 requests per minute)

```
Time    | Requests | Bucket State         | Result
--------|----------|----------------------|--------
0:00    | 1        | [99 tokens left]     | ✅ Allow
0:02    | 1        | [98 tokens left]     | ✅ Allow
...     | ...      | ...                  | ...
0:30    | 1        | [1 token left]       | ✅ Allow
0:31    | 1        | [0 tokens left]      | ✅ Allow
0:32    | 1        | [EMPTY]              | ❌ BLOCK (429 error)
1:00    | 1        | [100 tokens refilled]| ✅ Allow (new minute)
```

### Storage Backends
Rate limiters need to **track counts**. Options:

1. **In-Memory (default)** - Fast but doesn't scale across multiple servers
2. **Redis** ⭐ **Recommended** - Fast, distributed, perfect for your stack
3. **Database** - Slow, not recommended for high-traffic endpoints

**For your EDR system**: We'll use **Redis** (you already have it for Celery!)

---

## 📋 Implementation Strategy

### Library: `django-ratelimit`
- **GitHub**: https://github.com/jsocol/django-ratelimit
- **Why?**: Simple decorator-based, integrates seamlessly with Django, supports Redis
- **Production-Ready**: Used by major companies (Mozilla, etc.)

### Installation
```bash
pip install django-ratelimit
```

### Basic Usage
```python
from django_ratelimit.decorators import ratelimit

@ratelimit(key='ip', rate='100/m', method='POST')
def my_view(request):
    # If client exceeds 100 POST requests per minute
    # from their IP, they get a 429 error
    return Response({'status': 'ok'})
```

### Key Strategies for EDR System

#### Strategy 1: **Differentiated Limits** (Different endpoints, different limits)
```
High-Risk Endpoint (Auth)       → Strict (5/hour)
High-Volume Endpoint (Telemetry)→ Generous (1000/min)
Dashboard Pages                 → Moderate (200/min)
```

#### Strategy 2: **Smart Key Selection**
```python
# For Telemetry (Agent endpoints)
@ratelimit(key='header:X-Agent-Token', rate='1000/m')
# Why? Each agent identified by their token, fair per-agent limit

# For Auth endpoints
@ratelimit(key='ip', rate='5/1h')
# Why? Attackers don't have tokens yet, use IP to block brute force

# For Dashboard (logged-in users)
@ratelimit(key='user', rate='200/m')
# Why? Each SOC analyst gets their own limit
```

#### Strategy 3: **Layered Protection**
Apply multiple decorators for burst + sustained limits:
```python
@ratelimit(key='header:X-Agent-Token', rate='200/10s')  # Burst: 200 in 10 sec
@ratelimit(key='header:X-Agent-Token', rate='1000/m')   # Sustained: 1000/min
def telemetry_endpoint(request):
    # This allows normal bursts but prevents sustained flooding
    pass
```

---

## 🔍 Django-Ratelimit Explained

### Core Decorator Parameters

```python
@ratelimit(key='ip', rate='100/m', method='POST', block=True)
```

#### 1. **`key`** - Who are we tracking?
| Key Value | What It Does | Use Case |
|-----------|-------------|----------|
| `'ip'` | Track by IP address | Anonymous endpoints, auth |
| `'user'` | Track by logged-in user | Dashboard, authenticated APIs |
| `'header:X-Custom-Header'` | Track by custom header value | Agent token, API keys |
| `'get:param'` / `'post:param'` | Track by request parameter | Specific use cases |
| Callable function | Custom logic | Complex scenarios |

**Example - Custom Key Function**:
```python
def get_agent_id(group, request):
    # Extract agent ID from token header
    token = request.META.get('HTTP_X_AGENT_TOKEN', 'anonymous')
    return f'agent:{token}'

@ratelimit(key=get_agent_id, rate='1000/m')
def telemetry_endpoint(request):
    pass
```

#### 2. **`rate`** - How many requests allowed?
**Format**: `'<count>/<period>'`

| Rate | Meaning |
|------|---------|
| `'10/s'` | 10 requests per second |
| `'100/m'` | 100 requests per minute |
| `'1000/h'` | 1000 requests per hour |
| `'5000/d'` | 5000 requests per day |

**Special Values**:
- `None` - No limit (useful for conditional logic)
- `'0/s'` - Block all requests (emergency shutdown)

#### 3. **`method`** - Which HTTP methods to limit?
```python
method='POST'        # Only limit POST requests
method=['POST', 'PUT']  # Limit POST and PUT
method=ratelimit.ALL    # Limit all methods
```

#### 4. **`block`** - What happens when exceeded?
```python
block=True   # Return 403 Forbidden immediately (default)
block=False  # Set request.limited=True, let view handle it
```

**Custom handling**:
```python
@ratelimit(key='ip', rate='10/m', block=False)
def my_view(request):
    if getattr(request, 'limited', False):
        # Custom response
        return Response({
            'error': 'Rate limit exceeded',
            'retry_after': 60
        }, status=429)
    
    # Normal processing
    return Response({'status': 'ok'})
```

### Redis Integration

**settings.py**:
```python
RATELIMIT_USE_CACHE = 'default'  # Use Django cache backend

CACHES = {
    'default': {
        'BACKEND': 'django_redis.cache.RedisCache',
        'LOCATION': 'redis://127.0.0.1:6379/1',
        'OPTIONS': {
            'CLIENT_CLASS': 'django_redis.client.DefaultClient',
        }
    }
}
```

**How it works**:
1. Request arrives → Decorator checks Redis for key `ratelimit:ip:192.168.1.1`
2. If key exists → Increment counter
3. If counter > limit → Block request
4. If key doesn't exist → Create key with TTL (e.g., 60 seconds for `1/m`)

---

## 🗺️ Endpoint-by-Endpoint Analysis

### Our Current Endpoints (from urls.py)

Below is every endpoint in your EDR system, categorized by risk level and proposed rate limits.

---

### 🔴 **CRITICAL: Authentication Endpoints**

| Endpoint | Current State | Proposed Limit | Key | Why |
|----------|--------------|----------------|-----|-----|
| `POST /accounts/login/` | ❌ No protection | `5/1h` per IP | `ip` | Prevent brute force attacks |

**Risk**: Credential stuffing, brute force password attacks  
**Impact if unlimited**: Attacker can try thousands of passwords

**Implementation**:
```python
# ingestion/urls.py - we'll need to wrap the login view
from django_ratelimit.decorators import ratelimit

urlpatterns = [
    path('accounts/login/', 
         ratelimit(key='ip', rate='5/1h', method='POST')(auth_views.LoginView.as_view()), 
         name='login'),
]
```

---

### 🟠 **HIGH-RISK: Agent Communication Endpoints**

#### Telemetry Ingestion
| Endpoint | Volume | Proposed Limit | Key |
|----------|--------|----------------|-----|
| `POST /api/v1/telemetry/` | **Very High** | `1000/m` + `200/10s` burst | `header:X-Agent-Token` |

**Reasoning**:
- **High volume is legitimate** - Agents send batches of events
- **Per-agent limit** - Each agent gets their own quota
- **Burst allowance** - Allows spikes during security incidents

**Example**: If you have 50 agents:
- Each agent: 1000 requests/min = ~16 req/sec
- Total system: 50,000 requests/min possible
- One rogue agent can't DoS the system

**Implementation**:
```python
# ingestion/views.py
from django_ratelimit.decorators import ratelimit

@api_view(['POST'])
@permission_classes([IsAuthenticated])
@ratelimit(key='header:HTTP_X_AGENT_TOKEN', rate='200/10s', method='POST')
@ratelimit(key='header:HTTP_X_AGENT_TOKEN', rate='1000/m', method='POST')
def telemetry_endpoint(request):
    # Existing code
    ...
```

#### Command Polling
| Endpoint | Volume | Proposed Limit | Key |
|----------|--------|----------------|-----|
| `GET /api/v1/commands/poll/` | High | `300/m` | `header:X-Agent-Token` |

**Reasoning**:
- Agents poll every 10 seconds = 6 requests/minute normally
- 300/min gives 50x headroom for clock drift or retries

#### Command Result Reporting
| Endpoint | Volume | Proposed Limit | Key |
|----------|--------|----------------|-----|
| `POST /api/v1/commands/result/<id>/` | Low | `50/m` | `header:X-Agent-Token` |

**Reasoning**:
- Rare operation (only when action is executed)
- Generous limit to avoid false positives

---

### 🟡 **MEDIUM-RISK: Dashboard API Endpoints**

| Endpoint | Type | Proposed Limit | Key |
|----------|------|----------------|-----|
| `GET /api/v1/dashboard/stats/` | Read | `100/m` | `user` |
| `GET /api/v1/dashboard/alerts/` | Read | `100/m` | `user` |
| `GET /api/v1/dashboard/alerts/<id>/` | Read | `100/m` | `user` |
| `POST /api/v1/dashboard/alerts/<id>/status/` | Write | `50/m` | `user` |
| `POST /api/v1/dashboard/alerts/<id>/assign/` | Write | `50/m` | `user` |
| `POST /api/v1/dashboard/alerts/<id>/note/` | Write | `50/m` | `user` |
| `POST /api/v1/dashboard/rules/<id>/toggle/` | Write | `20/m` | `user` |
| `GET /api/v1/dashboard/alerts/<id>/timeline/` | Read | `100/m` | `user` |

**Reasoning**:
- **Per-user limits** - Each SOC analyst gets their own quota
- **Read endpoints** (100/m) - Analyst refreshing dashboard
- **Write endpoints** (50/m) - Less frequent, lower limit
- **Critical actions** (20/m) - Rule toggling is rare

---

### 🟢 **LOW-RISK: Dashboard Page Views (HTML)**

| Endpoint | Proposed Limit | Key |
|----------|----------------|-----|
| `GET /dashboard/` | `200/m` | `user_or_ip` |
| `GET /dashboard/alerts/` | `200/m` | `user_or_ip` |
| `GET /dashboard/alerts/<id>/` | `200/m` | `user_or_ip` |
| `GET /dashboard/rules/` | `200/m` | `user_or_ip` |
| `GET /dashboard/events/` | `200/m` | `user_or_ip` |
| `GET /dashboard/response-actions/` | `200/m` | `user_or_ip` |

**Reasoning**:
- Human users don't refresh pages 200 times/minute
- Protects against scrapers

**Implementation Note**: Use `user_or_ip` key:
```python
@ratelimit(key='user_or_ip', rate='200/m')
```
This tracks by user if logged in, otherwise by IP.

---

### 🔴 **CRITICAL: Response Action Triggers**

| Endpoint | Risk | Proposed Limit | Key | Why Strict? |
|----------|------|----------------|-----|-------------|
| `POST /api/v1/response/kill_process/` | **HIGH** | `20/m` | `user` | Dangerous operation |
| `POST /api/v1/response/isolate_host/` | **CRITICAL** | `10/m` | `user` | Network isolation |
| `POST /api/v1/response/deisolate_host/` | **CRITICAL** | `10/m` | `user` | Network restoration |

**Reasoning**:
- **Low legitimate volume** - Analysts don't kill 20 processes per minute
- **High impact** - Wrong action can disrupt business operations
- **Abuse prevention** - Prevents accidental or malicious mass actions

**Example Attack Scenario**:
> Attacker compromises SOC Analyst account → Writes script to isolate all 500 hosts  
> **With rate limit**: Only 10 hosts isolated before blocking  
> **Without rate limit**: All 500 hosts isolated in 10 seconds (company-wide outage)

---

### 🟢 **LOW-RISK: Admin Endpoints**

| Endpoint | Proposed Limit | Key |
|----------|----------------|-----|
| `GET /dashboard/admin/` | `100/m` | `user` |
| `POST /dashboard/admin/users/create/` | `10/m` | `user` |
| `POST /dashboard/admin/users/<id>/edit/` | `20/m` | `user` |
| `POST /dashboard/admin/rules/create/` | `10/m` | `user` |

**Reasoning**:
- Admin actions are rare
- Already protected by `@user_passes_test(is_admin)`
- Add rate limit as **defense in depth**

---

### ⚪ **NO LIMIT: Health Check**

| Endpoint | Limit |
|----------|-------|
| `GET /api/v1/health/` | **None** (monitoring systems need unrestricted access) |

---

## 📊 System Impact Analysis

### Positive Impacts ✅

#### 1. **DoS Protection**
- **Before**: 1 client sending 10,000 req/sec → Server crash
- **After**: Client blocked at 1000 req/min → Server stable

**Metric**: Uptime increases from 95% to 99.9%

#### 2. **Resource Stability**
- **Celery Queue**: Won't overflow with millions of tasks
- **Redis Memory**: Predictable usage (rate limit keys are tiny)
- **MongoDB**: Write load is bounded

**Metric**: Celery queue length stays under 10,000 (down from potential millions)

#### 3. **Cost Savings**
- **Before**: Need to overprovision servers for worst-case DoS
- **After**: Can size servers for legitimate peak load

**Metric**: 30-50% reduction in server costs

#### 4. **Security**
| Attack Type | Before | After |
|-------------|--------|-------|
| Credential stuffing | ✅ Possible | ❌ Blocked |
| API abuse | ✅ Possible | ❌ Blocked |
| Resource exhaustion | ✅ Possible | ❌ Blocked |

### Negative Impacts ⚠️ (and Mitigations)

#### 1. **False Positives**
**Risk**: Legitimate agent hits limit during incident

**Scenario**:
```
Agent detects ransomware → Generates 2000 events in 1 minute
Your limit: 1000/min
Result: 1000 events blocked ❌
```

**Mitigation**:
- Set limits **above normal peak** (baseline + 3x headroom)
- Use **burst limits** (e.g., `200/10s` allows spikes)
- **Monitor** and adjust based on real data

#### 2. **Legitimate High-Volume Agents**
**Risk**: Server with many processes (e.g., build server) generates lots of events

**Mitigation**:
- Use **per-agent limits** (not per-IP)
- Document how to request limit increase for special agents
- Implement **tiered limits** (normal agents: 1000/m, high-volume agents: 5000/m)

#### 3. **Shared IP Addresses (NAT)**
**Risk**: 10 agents behind same corporate NAT IP share rate limit

**Mitigation**:
- Use `header:X-Agent-Token` instead of `ip` for agent endpoints
- Each agent has unique token → unique limit

#### 4. **Redis Dependency**
**Risk**: If Redis goes down, rate limiting fails

**Options**:
```python
# Option 1: Fail-open (allow all requests if Redis is down)
RATELIMIT_FAIL_OPEN = True  # Default

# Option 2: Fail-closed (block all requests if Redis is down)
RATELIMIT_FAIL_OPEN = False  # More secure but risky
```

**Recommendation**: `FAIL_OPEN = True` (Redis outage shouldn't cause EDR outage)

### Performance Overhead

**Redis Lookup Cost**: ~1-2ms per request

**Example**:
- Before rate limiting: Request processing = 50ms
- After rate limiting: Request processing = 52ms (4% overhead)

**Negligible impact** - The protection is worth 2ms overhead.

---

## ⚙️ Configuration & Tuning

### settings.py Configuration

```python
# ========== RATE LIMITING ==========
RATELIMIT_ENABLE = True  # Set to False to disable globally (testing)
RATELIMIT_USE_CACHE = 'default'  # Use default cache backend
RATELIMIT_FAIL_OPEN = True  # If Redis is down, allow requests (don't block EDR)

# Redis Cache (you already have this for Celery)
CACHES = {
    'default': {
        'BACKEND': 'django_redis.cache.RedisCache',
        'LOCATION': 'redis://127.0.0.1:6379/1',
        'OPTIONS': {
            'CLIENT_CLASS': 'django_redis.client.DefaultClient',
        },
        'TIMEOUT': 60  # Default timeout for cache keys
    }
}

# Custom logging for rate limit violations
LOGGING = {
    'version': 1,
    'disable_existing_loggers': False,
    'handlers': {
        'ratelimit_file': {
            'level': 'WARNING',
            'class': 'logging.FileHandler',
            'filename': 'logs/ratelimit.log',
        },
    },
    'loggers': {
        'django_ratelimit': {
            'handlers': ['ratelimit_file'],
            'level': 'WARNING',
            'propagate': False,
        },
    },
}
```

### Environment-Specific Configuration

**.env**:
```bash
# Development: Relaxed limits
RATELIMIT_TELEMETRY_RATE=10000/m  # High limit for testing
RATELIMIT_AUTH_RATE=100/h         # Relaxed for dev

# Production: Strict limits
RATELIMIT_TELEMETRY_RATE=1000/m
RATELIMIT_AUTH_RATE=5/1h
```

**settings.py**:
```python
import os

# Read limits from environment
RATELIMIT_TELEMETRY = os.getenv('RATELIMIT_TELEMETRY_RATE', '1000/m')
RATELIMIT_AUTH = os.getenv('RATELIMIT_AUTH_RATE', '5/1h')
```

**views.py**:
```python
from django.conf import settings

@ratelimit(key='ip', rate=settings.RATELIMIT_AUTH, method='POST')
def login_view(request):
    pass
```

### Tuning Process

#### Step 1: Baseline (1 week)
Deploy with **generous limits** (3x expected peak):
```
Telemetry: 3000/m
Dashboard: 600/m
Auth: 20/h
```

#### Step 2: Monitor
Check Redis keys:
```bash
redis-cli
> KEYS rl:*
> GET rl:func:telemetry_endpoint:header:X-Agent-Token:AGENT-123
```

Check logs:
```bash
tail -f logs/ratelimit.log
```

#### Step 3: Analyze
Questions to ask:
- Are any legitimate users hitting limits? → Increase limit
- Are limits never approached? → Decrease limit (save resources)
- Are there spikes at certain times? → Add burst limits

#### Step 4: Adjust
Reduce limits by 50% every week until you find the sweet spot.

**Goal**: Limits should be hit < 0.1% of the time (only during actual attacks).

---

## 📈 Monitoring & Troubleshooting

### Logging Rate Limit Violations

**Create custom decorator wrapper**:
```python
# ingestion/ratelimit_utils.py
from django_ratelimit.decorators import ratelimit as django_ratelimit
import logging

logger = logging.getLogger('ratelimit')

def ratelimit_with_logging(key, rate, method='POST'):
    """Wrapper around ratelimit that logs violations."""
    def decorator(func):
        # Apply django_ratelimit
        limited_func = django_ratelimit(key=key, rate=rate, method=method, block=False)(func)
        
        def wrapper(request, *args, **kwargs):
            # Call rate-limited function
            response = limited_func(request, *args, **kwargs)
            
            # Check if request was limited
            if getattr(request, 'limited', False):
                # Extract key value for logging
                key_value = request.META.get(f'HTTP_{key.upper().replace("-", "_")}', 'unknown')
                logger.warning(
                    f"Rate limit exceeded: endpoint={func.__name__}, "
                    f"key={key}, value={key_value}, rate={rate}, "
                    f"method={request.method}, ip={request.META.get('REMOTE_ADDR')}"
                )
                # Return 429 error
                from rest_framework.response import Response
                return Response({
                    'error': 'Rate limit exceeded',
                    'retry_after': 60  # seconds
                }, status=429)
            
            return response
        return wrapper
    return decorator
```

**Usage**:
```python
from .ratelimit_utils import ratelimit_with_logging

@ratelimit_with_logging(key='header:HTTP_X_AGENT_TOKEN', rate='1000/m')
def telemetry_endpoint(request):
    pass
```

### Monitoring Dashboard

**metrics_view.py** (new file):
```python
from django.http import JsonResponse
from django.contrib.admin.views.decorators import staff_member_required
from django.core.cache import cache

@staff_member_required
def ratelimit_metrics(request):
    """
    Show rate limit usage for all agents.
    """
    # Get all agents from DB
    from rest_framework.authtoken.models import Token
    agents = Token.objects.all()
    
    metrics = []
    for agent in agents:
        key = f'rl:func:telemetry_endpoint:header:X-Agent-Token:{agent.key}'
        count = cache.get(key, 0)
        metrics.append({
            'agent_id': str(agent.key)[:8],  # First 8 chars
            'user': agent.user.username,
            'current_count': count,
            'limit': 1000,
            'usage_percent': (count / 1000) * 100
        })
    
    return JsonResponse({'agents': metrics})
```

**Add to urls.py**:
```python
path('api/v1/admin/ratelimit-metrics/', metrics_view.ratelimit_metrics, name='ratelimit_metrics'),
```

### Troubleshooting Guide

#### Issue 1: "Agent hitting limit, but traffic seems normal"

**Diagnosis**:
```bash
# Check Redis key
redis-cli GET "rl:func:telemetry_endpoint:header:X-Agent-Token:AGENT-ABC123"
# Returns: "1050" (means 1050 requests in current window)

# Check agent logs
grep "AGENT-ABC123" logs/ratelimit.log
```

**Solutions**:
1. Check if agent is in retry loop (check agent logs)
2. Check if limit is too low (compare with other agents)
3. Temporarily increase limit: `ratelimit(rate='2000/m')`

#### Issue 2: "Login page blocked for legitimate user"

**Diagnosis**:
```python
# Check if IP is shared (NAT/VPN)
# If 100 employees share one IP, 5/hour is too low
```

**Solution**:
```python
# Increase limit for auth
@ratelimit(key='ip', rate='50/h')  # Instead of 5/h
```

#### Issue 3: "Rate limiting not working (attacker bypassing)"

**Check**:
1. Is Redis running? `redis-cli PING`
2. Is decorator applied? Check view code
3. Is attacker rotating IPs? Use header-based key instead

**Emergency Block**:
```python
# Temporarily block all requests to endpoint
@ratelimit(key='ip', rate='0/s')  # Zero = block all
def vulnerable_endpoint(request):
    pass
```

---

## 🎯 Summary: Implementation Checklist

### Prerequisites
- [x] Redis is running (already used for Celery)
- [ ] Understand rate limit concepts
- [ ] Review endpoint access patterns

### Installation
- [ ] `pip install django-ratelimit`
- [ ] Add to `requirements.txt`
- [ ] Update `settings.py` with cache config

### Apply Decorators
- [ ] Auth endpoints (`5/1h`)
- [ ] Telemetry endpoint (`1000/m` + `200/10s`)
- [ ] Command poll endpoint (`300/m`)
- [ ] Dashboard APIs (`100/m` read, `50/m` write)
- [ ] Response actions (`20/m` kill, `10/m` isolate)
- [ ] Admin endpoints (`10-20/m`)
- [ ] Page views (`200/m`)

### Testing
- [ ] Test with normal agent load
- [ ] Test with simulated attack (high volume)
- [ ] Verify 429 errors returned correctly
- [ ] Check Redis keys created

### Monitoring
- [ ] Add logging for violations
- [ ] Create metrics dashboard
- [ ] Set up alerts for frequent violations

### Documentation
- [ ] Document limits in README
- [ ] Add troubleshooting guide for ops team
- [ ] Create runbook for limit adjustments

---

## 📚 Additional Resources

- **Django-Ratelimit Docs**: https://django-ratelimit.readthedocs.io/
- **Redis Commands**: https://redis.io/commands
- **OWASP Rate Limiting**: https://cheatsheetseries.owasp.org/cheatsheets/Denial_of_Service_Cheat_Sheet.html

---

## 🚀 Next Steps

1. **Review this document** with the team
2. **Start with conservative limits** (3x expected peak)
3. **Deploy to TEST environment first**
4. **Monitor for 1 week**
5. **Adjust limits** based on real data
6. **Deploy to PRODUCTION**

---

**Document Version**: 1.0  
**Last Updated**: 2025-11-29  
**Author**: EDR Security Team  
**Status**: Ready for Implementation
