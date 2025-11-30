# Concept: Agent Authentication & Enrollment
**First Principles, Market Approaches, and Best Practices**

---

# 1. First Principles: The "Who Are You?" Problem

In any Client-Server architecture where the client is a device (not a human), we face three fundamental challenges:

1.  **Identity:** How does the server know this request came from "Laptop-A" and not "Hacker-B"?
2.  **Trust:** How does "Laptop-A" know it's talking to the real Server?
3.  **Lifecycle:** What happens when "Laptop-A" is stolen? How do we revoke access?

**The Core Principle:**
> "Authentication must be tied to something you *have* (a secret) or something you *are* (a certificate), and that secret must never be static or shared."

---

# 2. Top 5 Market Approaches

How do industry giants (CrowdStrike, SentinelOne, Splunk) solve this?

### 1. The Enrollment Token (The Standard)
*   **How it works:** You generate a single "Installation Token" in the dashboard. You pass this token to the installer (`./install.sh --token XYZ`).
*   **The Trick:** This token is **only used once**. Upon first connection, the agent exchanges it for a unique, crypto-strong `Agent ID` and `Secret` (or Certificate).
*   **Pros:** Easy deployment (one script).
*   **Cons:** If the enrollment token leaks, rogue agents can register (until revoked).

### 2. Mutual TLS (mTLS) (The Gold Standard)
*   **How it works:** The server has a Certificate Authority (CA). Every agent is issued a unique Client Certificate signed by that CA. The TLS handshake itself proves identity.
*   **Pros:** Extremely secure. Impossible to spoof without the private key.
*   **Cons:** High operational complexity (managing PKI, revocation lists).

### 3. Rolling API Keys (The "Good Enough")
*   **How it works:** Agent has an API Key. Every 24 hours, it requests a new one.
*   **Pros:** If a key is stolen, it expires quickly.
*   **Cons:** "Split Brain" risk (if network fails during rotation, agent is locked out).

### 4. Cloud Identity (The "Cloud Native")
*   **How it works:** If running on AWS/Azure, the agent sends a signed Instance Identity Document. The server verifies it with AWS.
*   **Pros:** Zero secrets to manage.
*   **Cons:** Only works in cloud environments (not on employee laptops).

### 5. Static Shared Secret (The "MVP" - Current State)
*   **How it works:** Every agent has the same `AUTH_TOKEN` hardcoded in a config file.
*   **Pros:** Dead simple to build.
*   **Cons:** **Catastrophic Security Risk.** If one laptop is hacked, the attacker has the key to the entire kingdom.

---

# 3. Our Strategy: The "Environment Variable" Fix

We are currently at **Level 5 (Static Shared Secret)**.
The Bug Report requires us to move to a safer version of Level 5 (removing it from code), paving the way for Level 1 (Enrollment Tokens).

### The Immediate Fix (Issue #1)
We will move the secret from `config.json` (Code) to `EDR_AUTH_TOKEN` (Environment).

**Why?**
1.  **Code Safety:** We can commit code to GitHub without leaking secrets.
2.  **Ops Flexibility:** We can change the token in production (via Ansible/GPO) without recompiling the C++ agent.

### The Future State (Level 1)
1.  Agent starts with `ENROLLMENT_TOKEN` (Env Var).
2.  Agent calls `POST /api/register`.
3.  Server validates token and returns unique `agent_id` and `agent_secret`.
4.  Agent saves these to encrypted disk and uses them for future calls.

---

# 4. Impact Analysis (Resolving Issue #1)

**Change:**
Modify `EventConverter.cpp` / `EdrAgent.cpp` to read `getenv("EDR_AUTH_TOKEN")` instead of parsing `config.json`.

**Impact:**
*   **Security:** HIGH POSITIVE. Token no longer in source control.
*   **Deployment:** MEDIUM IMPACT. All existing agents must be restarted with the new environment variable set.
*   **Risk:** LOW. If the variable is missing, the agent will fail to connect (Fail Closed).

**Verification:**
1.  Set `EDR_AUTH_TOKEN=test_token` in terminal.
2.  Run Agent.
3.  Verify it connects.
4.  Unset variable.
5.  Verify it logs an error and exits.
