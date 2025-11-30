# Concept: Secret Management
**First Principles, Market Approaches, and Best Practices**

---

# 1. First Principles: The "Config vs. Code" Problem

In software engineering, we must distinguish between:
*   **Code:** The logic (Algorithms, Functions). This is static and shared.
*   **Configuration:** The environment (Database URLs, API Keys). This varies per deployment.

**The Core Principle (12-Factor App):**
> "Store config in the environment. Code should be deployable to any environment (Dev, QA, Prod) without changing a single line of code."

If you hardcode a secret:
1.  **Security Risk:** Anyone with code access has production access.
2.  **Operational Rigidity:** You can't rotate keys without recompiling/redeploying.

---

# 2. Top 5 Market Approaches

How do industry giants manage secrets like `SECRET_KEY`?

### 1. Environment Variables (The Standard)
*   **How it works:** `SECRET_KEY = os.getenv('DJANGO_SECRET_KEY')`
*   **Pros:** Simple, supported by every OS and Cloud (AWS, Docker, Kubernetes).
*   **Cons:** Variables can leak in crash dumps or `ps` commands if not careful.

### 2. .env Files (The Developer Friendly)
*   **How it works:** A local `.env` file (git-ignored) is loaded by the app at startup.
*   **Pros:** Easy for local development.
*   **Cons:** Easy to accidentally commit to Git if `.gitignore` is wrong.

### 3. Secret Management Services (The Enterprise)
*   **How it works:** App calls an API (HashiCorp Vault, AWS Secrets Manager) at startup to fetch keys.
*   **Pros:** Centralized auditing, automatic rotation, granular access control.
*   **Cons:** High complexity and cost. Overkill for small apps.

### 4. Encrypted Config Files (The "GitOps")
*   **How it works:** Secrets are encrypted (e.g., Mozilla SOPS) and committed to Git. The app decrypts them with a master key at runtime.
*   **Pros:** Version control for config.
*   **Cons:** Managing the master decryption key is still a problem (The "Turtle Problem").

### 5. Hardcoded Secrets (The "Anti-Pattern" - Current State)
*   **How it works:** `SECRET_KEY = 'django-insecure-...'`
*   **Pros:** Zero setup.
*   **Cons:** **Catastrophic Security Risk.**

---

# 3. Our Strategy: The "Environment Variable" Fix

We are moving from **Level 5 (Hardcoded)** to **Level 1 (Environment Variables)**.

### The Fix (Issue #2)
We will modify `settings.py` to read `SECRET_KEY` from the environment.

**Why?**
1.  **Security:** Secrets are removed from the codebase.
2.  **Compliance:** Meets basic security standards (OWASP, SOC2).
3.  **Flexibility:** Easy to change keys in production without touching code.

---

# 4. Impact Analysis (Resolving Issue #2)

**Change:**
Modify `backend/edr_server/settings.py`:
```python
SECRET_KEY = os.environ.get('DJANGO_SECRET_KEY', 'fallback-dev-key')
```

**Impact:**
*   **Security:** HIGH POSITIVE.
*   **Deployment:** LOW IMPACT. Developers need to set the env var or rely on the fallback (for dev only).
*   **Risk:** LOW. If the variable is missing in production, we must ensure it fails or warns, rather than silently using a weak default.

**Verification:**
1.  Set `DJANGO_SECRET_KEY=prod-key`.
2.  Run Server.
3.  Verify it starts.
4.  Unset variable.
5.  Verify it uses fallback (in Dev) or fails (in Prod).
