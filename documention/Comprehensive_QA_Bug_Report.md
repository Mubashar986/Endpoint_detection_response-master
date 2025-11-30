# EDR System - Comprehensive QA Bug Report

**Date:** 2025-11-28  
**Auditor:** Senior QA Engineer  
**System Version:** EDR Agent v1.1 + Django Backend  
**Status:** 🔴 **CRITICAL - UNSAFE FOR PRODUCTION**

---

## Executive Summary

This document details **32 active issues** identified during a comprehensive system audit.
**ZERO** critical issues have been resolved despite recent feature additions.

**Summary of Counts:**
- **CRITICAL:** 8 (Must fix immediately)
- **HIGH:** 10 (Must fix before release)
- **MEDIUM:** 9 (Fix in next sprint)
- **LOW:** 5 (Backlog)

---

## 1. CRITICAL Severity Issues (Security & Auth)

### Issue #1: Hardcoded Authentication Token in Version Control
**Severity:** CRITICAL  
**Component:** `edr-agent/config.json` (Line 5)  
**Description:** Authentication token `ba76c2dca3772eba25e32d70288dced3bcb9cbc0` is hardcoded.  
**Impact:** Full backend compromise. Attackers can send fake telemetry or spoof agents.  
**Fix:**
1. Remove token from `config.json`.
2. Use environment variable `EDR_AUTH_TOKEN`.
3. Rotate the compromised token immediately.

### Issue #2: Hardcoded Django SECRET_KEY
**Severity:** CRITICAL  
**Component:** `backend/edr_server/settings.py` (Line 23)  
**Description:** Default insecure `SECRET_KEY` is used.  
**Impact:** Session hijacking, remote code execution.  
**Fix:**
```python
import os
SECRET_KEY = os.environ.get('DJANGO_SECRET_KEY')
```

### Issue #3: DEBUG=True in Production
**Severity:** CRITICAL  
**Component:** `backend/edr_server/settings.py` (Line 26)  
**Description:** Debug mode enabled.  
**Impact:** Information leakage (stack traces, env vars, SQL queries).  
**Fix:** Set `DEBUG = False` and configure `ALLOWED_HOSTS`.

### Issue #4: Empty ALLOWED_HOSTS
**Severity:** CRITICAL  
**Component:** `backend/edr_server/settings.py` (Line 28)  
**Description:** `ALLOWED_HOSTS = []`.  
**Impact:** Host header injection attacks.  
**Fix:** Set to specific domain names or IPs.

### Issue #5: No MongoDB Authentication
**Severity:** CRITICAL  
**Component:** `backend/edr_server/settings.py` (Line 151)  
**Description:** Backend connects to MongoDB without username/password.  
**Impact:** Unrestricted database access if port 27017 is exposed.  
**Fix:** Enable MongoDB auth and use `MONGO_USER`/`MONGO_PASSWORD` env vars.

### Issue #6: No Rate Limiting
**Severity:** CRITICAL  
**Component:** `backend/ingestion/views.py`  
**Description:** No limits on API requests.  
**Impact:** DDOS vulnerability, resource exhaustion.  
**Fix:** Implement `django-ratelimit` on all public endpoints.

### Issue #7: Insufficient Request Validation
**Severity:** CRITICAL  
**Component:** `backend/ingestion/views.py`  
**Description:** No limit on batch size or payload size.  
**Impact:** Denial of Service via "Zip Bomb" or massive JSON payloads.  
**Fix:** Enforce `MAX_BATCH_SIZE = 1000` and `MAX_CONTENT_LENGTH`.

### Issue #8: Plaintext Token Transmission
**Severity:** CRITICAL  
**Component:** `edr-agent/HttpClient.cpp`  
**Description:** Communication uses HTTP (port 8000) instead of HTTPS.  
**Impact:** Tokens and data visible to network sniffers.  
**Fix:** Enable SSL/TLS in `WinHttpOpenRequest`.

---

## 2. HIGH Severity Issues (Reliability & Performance)

### Issue #9: Data Loss on Network Failure
**Severity:** HIGH  
**Component:** `edr-agent/EdrAgent.cpp` (Line 308)  
**Description:** `eventBuffer.clear()` is called even if `sendTelemetryBatch` fails.  
**Impact:** Permanent loss of security logs during network outages.  
**Fix:** Only clear buffer on success (HTTP 201). Implement retry logic.

### Issue #10: Memory Leak Risk (Static Buffer)
**Severity:** HIGH  
**Component:** `edr-agent/EdrAgent.cpp` (Line 282)  
**Description:** `eventBuffer` is a static vector with no upper bound check (other than batch size trigger). If send fails repeatedly and we implement retry (Issue #9) without a cap, it will leak memory.  
**Fix:** Implement a hard limit (e.g., 10MB) and drop oldest events if exceeded (Ring Buffer).

### Issue #11: Fake Gzip Compression
**Severity:** HIGH  
**Component:** `edr-agent/SimpleGzip.cpp`  
**Description:** Implementation uses "Store-Only" mode (no compression).  
**Impact:** Wasted bandwidth, false sense of optimization.  
**Fix:** Use `zlib` library for actual DEFLATE compression.

### Issue #12: No Connection Pooling
**Severity:** HIGH  
**Component:** `edr-agent/HttpClient.cpp`  
**Description:** New TCP connection for every request.  
**Impact:** Port exhaustion, high latency.  
**Fix:** Reuse `HINTERNET` session and connection handles.

### Issue #13: MongoDB Duplicate Event Crash
**Severity:** HIGH  
**Component:** `backend/ingestion/tasks.py`  
**Description:** `event_id` unique constraint causes unhandled exceptions on retry.  
**Impact:** Celery task failures, queue blockage.  
**Fix:** Use `TelemetryEvent.objects.get_or_create()` or catch `NotUniqueError`.

### Issue #14: No Request Decompression Validation
**Severity:** HIGH  
**Component:** `backend/edr_server/middleware.py`  
**Description:** Middleware decompresses without size limits.  
**Impact:** Memory exhaustion via Zip Bomb.  
**Fix:** Limit decompressed size (e.g., 100MB).

### Issue #15: No Celery Task Timeout
**Severity:** HIGH  
**Component:** `backend/ingestion/tasks.py`  
**Description:** Tasks can run indefinitely.  
**Impact:** Worker starvation if regex rules hang.  
**Fix:** Set `soft_time_limit` and `time_limit` on tasks.

### Issue #16: No Circuit Breaker
**Severity:** HIGH  
**Component:** `backend/ingestion/tasks.py`  
**Description:** Database failures cause infinite retries.  
**Impact:** Cascading failure during outages.  
**Fix:** Implement Circuit Breaker pattern for DB connections.

### Issue #17: Rule Cache Inefficiency
**Severity:** HIGH  
**Component:** `backend/ingestion/rule_engine.py`  
**Description:** Rules reloaded from DB every 5 minutes.  
**Impact:** Periodic latency spikes.  
**Fix:** Use Redis for rule caching with event-driven invalidation.

### Issue #18: Missing Database Indexes
**Severity:** HIGH  
**Component:** `backend/ingestion/models.py`  
**Description:** Missing compound indexes for common queries.  
**Impact:** Slow dashboard performance.  
**Fix:** Add indexes for `(agent_id, timestamp)`, `(event_type, timestamp)`.

---

## 3. MEDIUM Severity Issues (Usability & Maintenance)

### Issue #19: No Health Check Endpoint
**Severity:** MEDIUM  
**Description:** Agent has no status reporting mechanism.  
**Fix:** Add local HTTP server or heartbeat to backend.

### Issue #20: No Metrics
**Severity:** MEDIUM  
**Description:** No visibility into event rates, errors, or latency.  
**Fix:** Add Prometheus metrics.

### Issue #21: No API Versioning
**Severity:** MEDIUM  
**Description:** API changes will break agents.  
**Fix:** Use URL versioning (`/api/v1/`, `/api/v2/`).

### Issue #22: Future Timestamp Validation
**Severity:** MEDIUM  
**Description:** Events can be sent with future timestamps.  
**Fix:** Validate `timestamp <= now + 5 minutes`.

### Issue #23: Inefficient Alert Saving
**Severity:** MEDIUM  
**Description:** Alert save triggers synchronous stats update.  
**Fix:** Move stats update to async Celery task.

### Issue #24: No CORS Config
**Severity:** MEDIUM  
**Description:** Frontend on different domain will fail.  
**Fix:** Configure `django-cors-headers`.

### Issue #25: Ungraceful Shutdown
**Severity:** MEDIUM  
**Description:** Agent kills process immediately on Ctrl+C.  
**Fix:** Handle signals to flush buffer before exit.

### Issue #26: Content-Type Validation
**Severity:** MEDIUM  
**Description:** API accepts non-JSON content types.  
**Fix:** Enforce `Content-Type: application/json`.

### Issue #27: Hardcoded Buffer Size
**Severity:** MEDIUM  
**Description:** Batch size 100 is hardcoded.  
**Fix:** Move to `config.json`.

---

## 4. LOW Severity Issues

### Issue #28: Typo in Startup Message
**Severity:** LOW  
**Fix:** "fot the future" -> "for the future".

### Issue #29: Inconsistent Logging
**Severity:** LOW  
**Fix:** Standardize log format.

### Issue #30: No API Docs
**Severity:** LOW  
**Fix:** Add Swagger/OpenAPI.

### Issue #31: Missing Backend README
**Severity:** LOW  
**Fix:** Create documentation.

### Issue #32: No Unit Tests
**Severity:** LOW  
**Fix:** Add test suite.

---

## New Findings (v1.1)

### Issue #33: RBAC Not Applied to Telemetry
**Severity:** MEDIUM  
**Description:** RBAC decorators exist but aren't used on main endpoints.  
**Fix:** Audit all views and apply `require_analyst_or_admin` where appropriate.

### Issue #34: Command Polling Risk
**Severity:** MEDIUM  
**Description:** Polling interval control is client-side.  
**Fix:** Enforce minimum polling interval on server side.
