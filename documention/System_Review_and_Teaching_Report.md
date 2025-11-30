# System Review Report: Response Actions Implementation

**Date**: 2025-11-25  
**Version**: 1.0  
**Focus**: Response Actions (Kill Process, Isolate Host) - Architecture, Code, and Logic

---

## 1. Executive Summary

This document provides a comprehensive review of the recently implemented **Response Actions** feature. It serves as a teaching guide to understand *how* the system works, *why* specific design decisions were made, and *where* the logic resides.

**Key Feature Implemented**:
- **Command Delivery**: Asynchronous HTTP Polling (Server ↔ Agent).
- **Actions**: Kill Process (and tree), Isolate Host (Firewall), De-isolate Host.
- **Security**: Role-Based Access Control (RBAC) for analysts.
- **Audit**: Full traceability of who executed what and when.

---

## 2. Architectural Design: The "HTTP Polling" Pattern

### Why not WebSocket?
Initially, we considered WebSockets for real-time control. However, due to infrastructure constraints (missing C++ libraries in the current environment), we opted for an **HTTP Polling** architecture.

### How it Works
Instead of the server "pushing" a command to the agent, the agent "asks" the server for work.

1.  **The Queue (MongoDB)**: When an analyst clicks "Kill", we don't talk to the agent immediately. We save a `PendingCommand` in the database.
2.  **The Heartbeat (Agent)**: Every 5 seconds, the Agent asks: *"Do you have work for me?"* (`GET /api/v1/commands/poll/`).
3.  **The Execution**: If yes, the Agent executes it and reports back (`POST /api/v1/commands/result/`).

**Trade-off Analysis**:
*   **Pros**: Robust, firewall-friendly (outbound only), no persistent connections to manage, easier to debug.
*   **Cons**: Latency (up to 5 seconds delay).
*   **Verdict**: Acceptable for MVP. Ransomware takes milliseconds, but human response takes minutes, so 5s is negligible for *manual* response.

---

## 3. Backend Implementation (Django + MongoDB)

### 3.1 Data Models (`models_mongo.py`)

We introduced two key collections in MongoDB:

1.  **`PendingCommand`**: The "To-Do List" for agents.
    *   **Fields**: `command_id`, `agent_id`, `type` (e.g., 'kill_process'), `parameters` (e.g., PID), `status` ('new', 'in_progress', 'completed').
    *   **Logic**: This is a temporary state. Commands expire after 5 minutes if not picked up.

2.  **`ResponseAction`**: The "Permanent Record" (Audit Log).
    *   **Fields**: `user` (Analyst), `action_type`, `target_agent`, `timestamp`, `result_summary`.
    *   **Logic**: This is write-once/append-only. It ensures compliance so we know exactly which analyst killed a process.

### 3.2 API Logic (`command_views.py`)

**The Polling Endpoint (`poll_commands`)**:
*   **Logic**:
    1.  Identify Agent from Header (`X-Agent-ID`).
    2.  Query MongoDB for `PendingCommand` where `agent_id=X` AND `status='new'`.
    3.  **Atomic Update**: If found, mark status as `'in_progress'` immediately to prevent double-execution.
    4.  Return JSON payload.

**The Trigger Endpoints (`trigger_kill_process`)**:
*   **Logic**:
    1.  **Permission Check**: verify user is `SOC Analyst` or `Superuser`.
    2.  **Validation**: Ensure PID and Agent ID are present.
    3.  **Queue**: Create `PendingCommand`.
    4.  **Audit**: Create `ResponseAction`.
    5.  Return "Queued" status to UI.

### 3.3 Security & RBAC (`rbac_decorators.py`)

We implemented a custom decorator system to enforce the "Principle of Least Privilege".

*   **`@require_analyst_or_admin`**:
    *   Checks if user is in "SOC Analyst" group OR is a Superuser.
    *   **Design**: Decouples code from specific user IDs. We can add/remove users from groups in Django Admin without changing code.

---

## 4. Agent Implementation (C++ / Win32)

### 4.1 The Polling Thread (`EdrAgent.cpp`)

We didn't want to block the main event monitoring thread.
*   **Design**: A separate `std::thread` runs `pollCommandsLoop`.
*   **Logic**:
    *   `while(running)` loop.
    *   `HttpClient::GET` to check for commands.
    *   If command received -> `CommandProcessor::executeCommand`.
    *   `HttpClient::POST` to send result.
    *   `Sleep(5000)` (5 seconds).

### 4.2 Command Execution (`CommandProcessor.cpp`)

This is where the "heavy lifting" happens using Windows APIs.

**A. Kill Process (`killProcess`)**:
*   **API Used**: `OpenProcess(PROCESS_TERMINATE, ...)` and `TerminateProcess()`.
*   **Safety**: We verify the handle is valid.
*   **Tree Kill**: We use `CreateToolhelp32Snapshot` to find all child processes (e.g., if you kill `cmd.exe`, we also kill the `powershell.exe` it spawned).

**B. Isolate Host (`isolateHost`)**:
*   **Mechanism**: Windows Firewall (`netsh advfirewall`).
*   **Logic**:
    1.  **Block All Outbound**: `dir=out action=block`.
    2.  **Allow EDR Server**: Whitelist the EDR server IP so the agent can still talk to us! (Critical step).
    3.  **Allow DNS**: UDP 53 (optional, but usually needed).
*   **Why Netsh?**: It's simpler than the WFP (Windows Filtering Platform) C++ API for an MVP, though less stealthy.

---

## 5. Code Quality & Standards Review

### Coding Level
*   **Backend**: Pythonic, uses Django REST Framework patterns. Decorators keep views clean.
*   **Agent**: Modern C++ (C++11/14). Uses `nlohmann::json` for clean JSON handling. RAII (Resource Acquisition Is Initialization) is used for handles (mostly), though manual `CloseHandle` is present (could be improved with smart pointers in future).

### Design Level
*   **Separation of Concerns**:
    *   `CommandProcessor` knows *how* to kill.
    *   `EdrAgent` knows *when* to kill (polling).
    *   `HttpClient` knows *how to talk* to the server.
*   **Stateless Server**: The server doesn't keep open connections. It just reads/writes to MongoDB. This scales well.

### Implementation Logic
*   **Fail-Safe**: If the agent crashes, the command in MongoDB eventually times out (`expires_at`).
*   **Feedback Loop**: The UI doesn't just say "Sent". It can poll the command status to say "Success" only when the Agent reports back.

---

## 6. Next Steps for Improvement

1.  **HTTPS**: Currently using HTTP. Essential to upgrade to HTTPS to prevent command injection via Man-in-the-Middle.
2.  **Command Validation**: Add stronger checks on PIDs (e.g., don't allow killing PID 0 or 4).
3.  **WFP Driver**: Replace `netsh` with a kernel driver for isolation to prevent malware from just unblocking itself.

---

**Conclusion**: The system is now a functional "Remote Control" loop. It has moved from a passive "Camera" (monitoring events) to an active "Robot Arm" (taking action).
