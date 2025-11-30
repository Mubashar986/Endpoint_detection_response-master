# System Features Deep Dive: The Hidden Gears
**Security, Performance, and Administration**

---

# Part 1: Security (RBAC & Permissions)

We don't just let anyone delete users. We implemented a strict **Role-Based Access Control (RBAC)** system.

## 1. The Roles
| Role | Permissions |
| :--- | :--- |
| **SOC Viewer** | Read-Only (View Alerts, Events, Rules). |
| **SOC Analyst** | Operational (Kill Process, Isolate Host, Toggle Rules). |
| **Superuser** | Admin (Manage Users, Create Rules, Full Access). |

## 2. The Implementation (`rbac_decorators.py`)
We use Python **decorators** to enforce these rules on every API call.

```python
@require_analyst_or_admin
def trigger_kill_process(request):
    # Only runs if user is Analyst or Superuser
    ...
```

**How it works:**
1.  Checks `request.user.is_superuser`.
2.  Checks `request.user.groups` for "SOC Analyst".
3.  If neither, it returns a 403 Forbidden or redirects.

---

# Part 2: Dashboard & Admin APIs

The frontend is powered by a rich set of APIs in `dashboard_views.py` and `admin_views.py`.

## 1. The Dashboard (`dashboard_views.py`)
*   **Stats API:** Returns live counts of Events, Alerts, and Critical issues.
*   **Timeline API:** Fetches all events +/- 30 minutes around an alert for context.
*   **Timezone Magic:** We use a helper `to_local()` to ensure a user in New York sees EST times, while the server runs in UTC.

## 2. The Admin Panel (`admin_views.py`)
*   **User Management:** Create, Edit, and Delete users. Assign Roles.
*   **Rule Builder:** A visual interface to create JSON detection rules without writing code.

---

# Part 3: Middleware (Performance)

We optimized the high-volume telemetry endpoint using **Middleware**.

## 1. The Problem
Agents send thousands of JSON logs. Sending them as raw text wastes bandwidth.

## 2. The Solution: Gzip (`middleware.py`)
We created `DecompressMiddleware`.

**Logic:**
1.  Intercepts every request to `/api/v1/telemetry/`.
2.  Checks for header `Content-Encoding: gzip`.
3.  If found, it **decompresses** the body *before* Django sees it.

```python
if 'gzip' in encoding:
    request._body = gzip.decompress(request.body)
```

**Result:** 90% reduction in network traffic.

---

# Part 4: Data Validation (Serializers)

We use **Django REST Framework Serializers** to ensure data quality.

## 1. The Gatekeeper (`serializers.py`)
Before any data enters our system, `TelemetrySerializer` checks it.

*   **Required Fields:** Ensures `agent_id`, `event_type`, etc., exist.
*   **Type Checking:** Ensures `process` event has `process` data.
*   **Timestamp Normalization:** Converts Unix timestamps (integers) or ISO strings into proper **UTC Datetime** objects.

```python
def validate_timestamp(self, value):
    # Converts 1698432000 -> datetime(2023, 10, 27, ...)
    return datetime.fromtimestamp(value, tz=timezone.utc)
```

---

**Summary:**
Your system is more than just Redis and Celery. It has:
1.  **RBAC** for security.
2.  **Rich APIs** for visualization.
3.  **Compression** for speed.
4.  **Strict Validation** for data integrity.
