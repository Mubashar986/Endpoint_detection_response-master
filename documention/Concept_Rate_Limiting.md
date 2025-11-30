# Concept: Rate Limiting
**Defense Against Abuse and Overload**

---

# 1. What is Rate Limiting?
Rate limiting is a strategy for limiting network traffic. It sets a cap on how often someone can repeat an action within a certain timeframe.

**The Metaphor:** It's like a bouncer at a club. Even if you have a ticket (Authentication), you can't bring 100 friends in at once (Rate Limit).

---

# 2. Why do we need it? (The Risks)
Without rate limiting, your API is vulnerable to:
1.  **Brute Force Attacks:** Hackers guessing passwords millions of times per second.
2.  **Denial of Service (DoS):** An attacker (or a buggy agent) flooding the server with requests, crashing it for everyone else.
3.  **Resource Exhaustion:** expensive queries (like "Search all logs") consuming 100% CPU.

---

# 3. Where does it live?
In our EDR system, Rate Limiting belongs in the **Middleware** or **View Decorators** layer of the Backend.

*   **Global Limit:** "No IP can make more than 1000 requests per minute."
*   **Endpoint Limit:** "No user can try to login more than 5 times per minute."

---

# 4. Implementation Strategy (Django)

We will use the industry-standard library: **`django-ratelimit`**.

### How it works
It uses a fast cache (like Redis) to count requests.

### Example Usage
```python
from django_ratelimit.decorators import ratelimit

@ratelimit(key='ip', rate='5/m', block=True)
def login(request):
    # ...
```
*   **key='ip':** Limit based on the user's IP address.
*   **rate='5/m':** Allow 5 requests per minute.
*   **block=True:** If they exceed the limit, return `403 Forbidden` immediately.

---

# 5. EDR Specific Plan (Issue #4)
We need to protect:
1.  **Login Endpoints:** Prevent password guessing.
2.  **Telemetry Ingestion:** Prevent a compromised agent from flooding us.
3.  **Command Polling:** Prevent agents from hammering the database.

**Proposed Limits:**
*   **Login:** 5/minute per IP.
*   **Telemetry:** 100/minute per Agent ID.
*   **Polling:** 60/minute per Agent ID.
