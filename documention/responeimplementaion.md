Response Actions Implementation Plan (Revised)
Kill Process & Isolate Host Feature
Date: 2025-11-21 (Updated: 2025-11-21 20:44)
Feature: Response Actions (Kill Process / Isolate Host)
Architecture: Option A (MVP) - HTTP Polling + RBAC
Estimated Effort: 12 hours (1.5 developer days)

Revision Summary
Changes from original plan:

✅ HTTP Polling instead of WebSocket (WebSocket not available due to vcpkg issues)
✅ RBAC Design with Django Admin integration (4 roles defined)
✅ Design-focused - No full code dumps, only architecture and patterns
✅ HTTP acknowledged - No HTTPS requirement for development
User Review Required
IMPORTANT

Critical Architecture Decision: HTTP Polling Command Delivery

Since WebSocket infrastructure is not available, we're implementing a command queue pattern where:

Backend stores commands in MongoDB (pending_commands collection)
Agent polls /api/v1/commands/poll/ every 5-10 seconds
Agent executes command and reports result back
Tradeoff: Commands take 5-10 seconds to execute (polling delay) vs instant delivery with WebSocket.

Acceptable? For response actions, 5-10 second delay is reasonable (ransomware still spreading, but much better than manual intervention).

WARNING

RBAC Security Model

Proposed 4 roles with escalating privileges:

Viewer - Read-only (view alerts, events)
SOC Analyst (Junior) - View + add notes + assign alerts
SOC Analyst (Senior) - Junior + kill process + isolate host
Super Admin - Full access + manage rules + manage users
Question: Should Junior analysts be able to kill processes, or is this too risky?

High-Level Architecture
1. Command Delivery Flow (HTTP Polling)
┌─────────────────────────────────────────────────────────┐
│                  Django Backend                          │
│                                                          │
│  ┌──────────────────┐      ┌──────────────────┐        │
│  │ Dashboard UI     │      │  Response API     │        │
│  │ (Analyst clicks) │─────▶│ /kill_process/    │        │
│  └──────────────────┘      └────────┬──────────┘        │
│                                     │                    │
│                                     ▼                    │
│                          ┌────────────────────┐         │
│                          │   MongoDB          │         │
│                          │ ┌────────────────┐ │         │
│                          │ │pending_commands│ │         │
│                          │ │{               │ │         │
│                          │ │ agent: "PC-1"  │ │         │
│                          │ │ type: "kill"   │ │         │
│                          │ │ status: "new"  │ │         │
│                          │ │}               │ │         │
│                          │ └────────────────┘ │         │
│                          └────────┬───────────┘         │
│                                   │                     │
│  ┌────────────────────────────────┴───────┐            │
│  │  Command Poll API                      │            │
│  │  GET /api/v1/commands/poll/            │            │  
│  │  (returns pending command if exists)   │            │
│  └────────────────────┬───────────────────┘            │
└───────────────────────┼────────────────────────────────┘
                        │
                        │ HTTP GET (every 5-10 sec)
                        ▼
         ┌──────────────────────────┐
         │   Windows Agent (C++)    │
         │                          │
         │  ┌────────────────────┐  │
         │  │ Polling Loop       │  │
         │  │ (background thread)│  │
         │  └──────┬─────────────┘  │
         │         │                │
         │         ▼                │
         │  ┌────────────────────┐  │
         │  │ Execute Command    │  │
         │  │ killProcess(1234)  │  │
         │  └──────┬─────────────┘  │
         │         │                │
         │         ▼                │
         │  POST /api/v1/commands/result/
         │  {"status": "success"}  │
         └──────────────────────────┘
Key Design Points:

Agent maintains persistent HTTP GET loop in background thread
Server returns 200 with command OR 204 No Content (no pending commands)
Agent executes synchronously, reports back immediately
Server marks command as completed in MongoDB
2. RBAC Permission Matrix
Action	Viewer	Junior Analyst	Senior Analyst	Super Admin
View alerts	✅	✅	✅	✅
View events	✅	✅	✅	✅
View rules	✅	✅	✅	✅
Assign alert to self	❌	✅	✅	✅
Add notes to alert	❌	✅	✅	✅
Resolve alert	❌	✅	✅	✅
Kill process	❌	❌	✅	✅
Isolate host	❌	❌	✅	✅
Deisolate host	❌	❌	✅	✅
Create/edit rules	❌	❌	❌	✅
Toggle rules on/off	❌	❌	✅	✅
View audit trail	❌	✅	✅	✅
Manage users	❌	❌	❌	✅
Access Django Admin	❌	❌	❌	✅
Implementation: Django Groups + custom permission decorators

Proposed Changes
Component 1: Agent - HTTP Polling Command Loop
Philosophy
The agent needs a background thread that continuously polls the server for pending commands while the main thread continues monitoring events.

[MODIFY] EdrAgent.cpp
New Components Needed:

PollCommandsThread - Background thread that polls every 5-10 seconds

Uses HttpClient (already exists)
GET /api/v1/commands/poll/
Parses JSON response
Calls CommandProcessor::executeCommand()
POST result back to /api/v1/commands/result/<command_id>/
Threading Strategy:

Main Thread: Event monitoring (existing)
↓
Spawn Thread 2: Command polling (NEW)
Design Pattern:

// Pseudocode - design only
void pollCommandsLoop() {
    HttpClient client;
    
    while (running) {
        // Poll for commands
        json response = client.GET("/api/v1/commands/poll/");
        
        if (response.status == 200 && response.body contains command) {
            string commandId = response["command_id"];
            string commandType = response["type"];
            json params = response["parameters"];
            
            // Execute using existing CommandProcessor
            string result = CommandProcessor::executeCommand(commandJson);
            
            // Report result back
            client.POST("/api/v1/commands/result/" + commandId + "/", result);
        }
        
        // Wait 5-10 seconds
        Sleep(5000);
    }
}
// In main()
std::thread commandThread(pollCommandsLoop);
commandThread.detach();  // Run in background
Key Considerations:

Thread-safe logging (use mutex if needed)
Graceful shutdown (set running = false on Ctrl+C)
Error handling (network timeout, server down)
[MODIFY] CommandProcessor.cpp
New Functions to Add:

killProcess(DWORD pid)

Design: Use OpenProcess(PROCESS_TERMINATE, ...) + TerminateProcess()
Return: {"status": "success"} or {"status": "failed", "error": "Access Denied"}
killProcessTree(DWORD pid)

Design: Use CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, ...) to enumerate children
Recursive kill (children first, then parent)
Return: {"status": "success", "killed_count": 5}
isolateHost(string serverIp, int serverPort)

Design: Execute netsh advfirewall commands via system() or CreateProcess()
Commands:
netsh advfirewall firewall add rule name="EDR_BLOCK_ALL" dir=out action=block
netsh advfirewall firewall add rule name="EDR_ALLOW_SERVER" dir=out action=allow remoteip=<serverIp> protocol=TCP remoteport=<serverPort>
netsh advfirewall firewall add rule name="EDR_ALLOW_DNS" dir=out action=allow protocol=UDP remoteport=53
Return: {"status": "isolated"}
deisolateHost()

Design: Delete firewall rules
netsh advfirewall firewall delete rule name="EDR_BLOCK_ALL"
netsh advfirewall firewall delete rule name="EDR_ALLOW_SERVER"
netsh advfirewall firewall delete rule name="EDR_ALLOW_DNS"
Update 
executeCommand()
 switch:

Add cases for "kill_process", "kill_process_tree", "isolate_host", "deisolate_host"
Extract parameters from JSON (e.g., pid = commandJson["pid"])
Call appropriate function
Return result JSON
Component 2: Backend - Command Queue System
[NEW] pending_commands.py (Models)
Purpose: MongoDB models for command queue

Design:

class PendingCommand(Document):
    """
    Command queue - stores commands waiting to be executed by agents
    """
    command_id = StringField(required=True, unique=True, default=uuid4)
    agent_id = StringField(required=True)  # Target endpoint
    
    # Command details
    command_type = StringField(required=True)  # kill_process, isolate_host, etc.
    parameters = DictField()  # {"pid": 1234}
    
    # Status tracking
    status = StringField(required=True)  # new, in_progress, completed, failed, timeout
    result = DictField()  # Response from agent
    
    # Metadata
    issued_by = StringField(required=True)  # Analyst email
    related_alert_id = StringField()
    
    # Timestamps
    created_at = DateTimeField(default=datetime.utcnow)
    expires_at = DateTimeField()  # Auto-delete after 5 minutes
    completed_at = DateTimeField()
    
    meta = {
        'collection': 'pending_commands',
        'indexes': [
            'agent_id',
            'status',
            'expires_at'  # For cleanup
        ]
    }
Why MongoDB?

Fast lookups by agent_id + status=new
TTL index for auto-cleanup (expires_at)
Flexible schema (different command types have different parameters)
[NEW] command_api.py (Views)
Purpose: API endpoints for command queue

Endpoints:

GET /api/v1/commands/poll/ (Agent polls this)

Auth: Token-based (agent token)
Logic:
agent_id = identify_agent_from_token(request)
command = PendingCommand.objects(agent_id=agent_id, status='new').first()
if command:
    command.status = 'in_progress'
    command.save()
    return Response({
        'command_id': command.command_id,
        'type': command.command_type,
        'parameters': command.parameters
    })
else:
    return Response(status=204)  # No content
POST /api/v1/commands/result/<command_id>/ (Agent reports result)

Auth: Token-based
Logic:
command = PendingCommand.objects(command_id=command_id).first()
command.status = 'completed' if success else 'failed'
command.result = request.data
command.completed_at = datetime.utcnow()
command.save()
# Update ResponseAction audit log
ResponseAction.objects(action_id=command.related_action_id).update(
    status=command.status,
    result_message=command.result['message']
)
POST /api/v1/response/kill_process/ (Dashboard triggers this)

Auth: User token (MUST be Senior Analyst or Admin)
Logic:
# Permission check
if not request.user.has_perm('ingestion.can_kill_process'):
    return Response({'error': 'Insufficient permissions'}, status=403)
# Create command in queue
command = PendingCommand(
    agent_id=request.data['agent_id'],
    command_type='kill_process',
    parameters={'pid': request.data['pid']},
    status='new',
    issued_by=request.user.email,
    expires_at=datetime.utcnow() + timedelta(minutes=5)
)
command.save()
# Create audit log
ResponseAction(...).save()
return Response({'status': 'queued', 'command_id': command.command_id})
Timeout Handling:

Celery task runs every 1 minute
Checks for commands with status=in_progress and expires_at < now
Marks as timeout
Component 3: RBAC System Design
[NEW] groups_permissions.py (Setup Script)
Purpose: Initialize Django Groups and Permissions

Design Pattern:

# Run once during deployment
from django.contrib.auth.models import Group, Permission
from django.contrib.contenttypes.models import ContentType
# Define custom permissions
content_type = ContentType.objects.get_for_model(Alert)
Permission.objects.get_or_create(
    codename='can_kill_process',
    name='Can kill processes on endpoints',
    content_type=content_type
)
Permission.objects.get_or_create(
    codename='can_isolate_host',
    name='Can isolate endpoints',
    content_type=content_type
)
Permission.objects.get_or_create(
    codename='can_manage_rules',
    name='Can create/edit detection rules',
    content_type=content_type
)
# Create Groups
viewer_group = Group.objects.create(name='Viewer')
junior_group = Group.objects.create(name='SOC Analyst (Junior)')
senior_group = Group.objects.create(name='SOC Analyst (Senior)')
admin_group = Group.objects.create(name='Super Admin')
# Assign permissions
junior_group.permissions.add(
    Permission.objects.get(codename='change_alert'),  # Can assign, add notes
    Permission.objects.get(codename='view_alert')
)
senior_group.permissions.add(
    *junior_group.permissions.all(),
    Permission.objects.get(codename='can_kill_process'),
    Permission.objects.get(codename='can_isolate_host')
)
admin_group.permissions.add(
    *senior_group.permissions.all(),
    Permission.objects.get(codename='can_manage_rules'),
    Permission.objects.get(codename='add_user'),
    Permission.objects.get(codename='change_user')
)
[MODIFY] Django Admin Customization
File: 
ingestion/admin.py

Purpose: Customize Django Admin to show user roles prominently

Design:

from django.contrib import admin
from django.contrib.auth.admin import UserAdmin
from django.contrib.auth.models import User
class CustomUserAdmin(UserAdmin):
    list_display = ('username', 'email', 'get_groups', 'is_staff', 'last_login')
    list_filter = ('groups', 'is_staff', 'is_superuser')
    
    def get_groups(self, obj):
        return ", ".join([g.name for g in obj.groups.all()])
    get_groups.short_description = 'Roles'
# Unregister default UserAdmin
admin.site.unregister(User)
admin.site.register(User, CustomUserAdmin)
# Add Group management
from django.contrib.auth.models import Group
class GroupAdmin(admin.ModelAdmin):
    list_display = ('name', 'get_permissions')
    filter_horizontal = ('permissions',)
    
    def get_permissions(self, obj):
        return obj.permissions.count()
    get_permissions.short_description = 'Permission Count'
admin.site.register(Group, GroupAdmin)
User Experience:

Super Admin logs into Django Admin
Goes to "Users" section
Click on user (e.g., analyst1@company.com)
Under "Groups", select "SOC Analyst (Senior)"
Save → User now has kill process permissions
[NEW] Custom Permission Decorator
File: ingestion/decorators.py

Purpose: Enforce RBAC in views

Design:

from functools import wraps
from rest_framework.response import Response
from rest_framework import status
def require_permission(permission_codename):
    """
    Decorator to check if user has specific permission
    Usage: @require_permission('can_kill_process')
    """
    def decorator(view_func):
        @wraps(view_func)
        def wrapper(request, *args, **kwargs):
            if not request.user.has_perm(f'ingestion.{permission_codename}'):
                return Response({
                    'error': 'Insufficient permissions',
                    'required': permission_codename
                }, status=status.HTTP_403_FORBIDDEN)
            
            return view_func(request, *args, **kwargs)
        return wrapper
    return decorator
# Usage in views
@api_view(['POST'])
@permission_classes([IsAuthenticated])
@require_permission('can_kill_process')
def kill_process_action(request):
    # Only Senior Analysts and Admins reach here
    pass
Component 4: Dashboard UI Updates
[MODIFY] alert_detail.html
Changes:

Show/hide action buttons based on user permissions
<!-- Response Actions Section -->
<div class="card mt-3">
    <div class="card-header">
        <h5>🛡️ Response Actions</h5>
    </div>
    <div class="card-body">
        {% if user.has_perm('ingestion.can_kill_process') and alert.evidence.process_id %}
        <button class="btn btn-danger" onclick="killProcess(...)">
            🔴 Kill Process (PID: {{ alert.evidence.process_id }})
        </button>
        {% else %}
        <button class="btn btn-secondary" disabled title="Insufficient permissions">
            🔴 Kill Process (Requires Senior Analyst)
        </button>
        {% endif %}
        
        {% if user.has_perm('ingestion.can_isolate_host') %}
        <button class="btn btn-warning" onclick="isolateHost(...)">
            🔒 Isolate Host
        </button>
        {% else %}
        <button class="btn btn-secondary" disabled>
            🔒 Isolate Host (Requires Senior Analyst)
        </button>
        {% endif %}
    </div>
</div>
JavaScript updates
function killProcess(agentId, pid) {
    if (!confirm('⚠️ Kill process ' + pid + '?\n\nThis cannot be undone.')) {
        return;
    }
    
    fetch('/api/v1/response/kill_process/', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'Authorization': 'Token ' + getToken(),
            'X-CSRFToken': getCsrfToken()
        },
        body: JSON.stringify({
            agent_id: agentId,
            pid: pid,
            alert_id: '{{ alert.alert_id }}'  // Link to alert
        })
    })
    .then(response => {
        if (response.status === 403) {
            alert('❌ Insufficient permissions. Contact your admin.');
            return;
        }
        return response.json();
    })
    .then(data => {
        if (data.status === 'queued') {
            alert('✅ Command queued. Agent will execute within 5-10 seconds.');
            // Poll for result
            pollCommandResult(data.command_id);
        }
    });
}
function pollCommandResult(commandId) {
    // Poll every 2 seconds for up to 30 seconds
    let attempts = 0;
    const maxAttempts = 15;
    
    const interval = setInterval(() => {
        fetch('/api/v1/commands/status/' + commandId + '/')
            .then(r => r.json())
            .then(data => {
                if (data.status === 'completed') {
                    clearInterval(interval);
                    alert('✅ Process killed successfully');
                    location.reload();
                } else if (data.status === 'failed') {
                    clearInterval(interval);
                    alert('❌ Failed: ' + data.result.message);
                } else if (++attempts >= maxAttempts) {
                    clearInterval(interval);
                    alert('⏱️ Timeout - check action log');
                }
            });
    }, 2000);
}
[NEW] response_actions.html (Audit Trail Page)
Design:

Table showing all response actions
Columns: Timestamp, Action, Agent, Analyst, Status, Result
Filter by: Date range, Agent, Analyst, Status
Export to CSV (for compliance audits)
Access Control:

Junior Analysts can see their own actions
Senior Analysts can see all actions
Super Admins can see all + delete
Verification Plan
Unit Tests
Agent Tests (C++)
File: test_response_actions.cpp

Test Cases:

test_kill_process_valid() - Start notepad, kill it, verify dead
test_kill_process_invalid_pid() - Try PID 999999, should return error
test_kill_protected_process() - Try csrss.exe, should fail with access denied
test_isolate_host() - Create firewall rules, verify ping fails
test_poll_command_no_pending() - Poll returns 204
test_poll_command_executes() - Create pending command, poll, verify executed
Backend Tests (Python)
File: test_command_api.py

Test Cases:

test_poll_returns_pending_command() - Create command, poll, verify returned
test_poll_marks_in_progress() - Verify status changes
test_result_updates_command() - Post result, verify completion
test_kill_process_requires_permission() - Junior analyst gets 403
test_senior_analyst_can_kill() - Senior analyst gets 200
test_command_timeout() - Old command marked timeout
Integration Tests
End-to-end Kill Process

Start notepad on agent machine
Dashboard: Click "Kill Process"
Wait 10 seconds
Verify notepad closed
Check audit log has entry
End-to-end Isolation

Dashboard: Click "Isolate Host"
From agent machine: Try ping 8.8.8.8 → should fail
Verify EDR events still flowing
Dashboard: Click "Remove Isolation"
Verify ping works again
Permission Enforcement

Login as Junior Analyst
Try to kill process → should see disabled button
Logout, login as Senior
Same page → button enabled
Manual Testing Checklist
 Agent polling loop starts on agent boot
 Agent continues event monitoring while polling
 Kill process executes within 10 seconds of button click
 Isolation prevents RDP but allows EDR
 All actions logged to response_actions collection
 RBAC permissions enforced (Junior cannot kill)
 Django Admin shows user roles correctly
 Timeout handling works (command expires after 5 min)
Technical Debt & Limitations
Item	Impact	Mitigation Timeline
HTTP polling delay (5-10 sec)	🟡 Not instant	Phase 3: WebSocket (when vcpkg fixed)
No offline command queue	🔴 Commands lost if agent offline	Phase 2: Persistent queue in Redis
No confirmation/approval workflow	🟡 Accidental kills possible	Phase 2: Two-factor confirm
netsh instead of WFP API	🟡 User can bypass via GUI	Phase 3: Kernel-level WFP
HTTP (no HTTPS)	🔴 Commands in cleartext	Development only, prod TBD
No command history in UI	🟡 Hard to track who did what	Phase 2: Enhanced audit page
No rate limiting	🟡 Analyst spam risk	Phase 2: 10 actions/min limit
Implementation Timeline
Phase 1: MVP (12 hours)
Day 1 Morning (4 hours): Agent Changes

Add background polling thread to 
EdrAgent.cpp
 (2h)
Implement killProcess() in 
CommandProcessor.cpp
 (1h)
Implement isolateHost() using netsh (1h)
Build & test locally
Day 1 Afternoon (4 hours): Backend Changes

Create PendingCommand model (30min)
Create command poll/result APIs (1.5h)
Create RBAC groups & permissions setup script (1h)
Update kill_process_action view with queue logic (1h)
Day 2 Morning (4 hours): UI & Integration

Update alert_detail.html with permission checks (1h)
Add JavaScript polling for command results (1h)
Create audit trail page response_actions.html (1h)
End-to-end testing (1h)
Phase 2: Hardening (Week 2, 8 hours)
Add command timeout handling (Celery task)
Add rate limiting (max 10 actions/min per user)
Add confirmation dialog with countdown
Improve error messages & logging
Add CSV export for audit trail
Phase 3: Advanced (Month 2, 16 hours)
Replace HTTP polling with WebSocket (when vcpkg fixed)
Implement WFP API (Windows Filtering Platform)
Add process suspend/resume
Add automated response rules
Add approval workflow (manager approves isolation)
RBAC Roles - Detailed Specification
Role 1: Viewer
Purpose: Compliance officer, manager (view-only access)

Permissions:

view_alert
view_telemetryevent
view_detectionrule
view_responseaction (audit trail)
UI Access:

Dashboard (read-only)
Alert list (no action buttons)
Event search
Audit trail
Cannot:

Modify anything
Execute response actions
Access Django Admin
Role 2: SOC Analyst (Junior)
Purpose: Tier 1 SOC analyst (triage, initial investigation)

Permissions:

All Viewer permissions +
change_alert (assign to self, add notes)
can_resolve_alert (mark as resolved/false positive)
UI Access:

Assign alerts to self
Add investigation notes
Mark alerts resolved
View audit trail (own actions only)
Cannot:

Kill processes
Isolate hosts
Create/edit rules
Rationale: Junior analysts are still learning. Giving kill/isolate access too early is risky.

Role 3: SOC Analyst (Senior)
Purpose: Tier 2 SOC analyst (advanced investigation, response)

Permissions:

All Junior permissions +
can_kill_process
can_isolate_host
can_toggle_rules (enable/disable detection rules)
UI Access:

Kill process button (enabled)
Isolate host button (enabled)
Toggle rule on/off
View all audit trail
Cannot:

Create new rules (prevent accidental breaking)
Manage users
Access Django Admin
Rationale: Experienced analysts who understand consequences. Trusted with destructive actions.

Role 4: Super Admin
Purpose: EDR administrator, security engineer

Permissions:

All Senior permissions +
can_manage_rules (create, edit, delete rules)
add_user, change_user, delete_user
Full Django Admin access
UI Access:

Everything
Django Admin panel
Create custom detection rules
Manage user accounts & roles
Rationale: Full control for system administrators. Only 1-2 people should have this.

Decision Matrix for User Roles
Question: What role should I assign to a new hire?

Scenario	Recommended Role
New SOC analyst (0-6 months experience)	Junior
Experienced SOC analyst (1+ years)	Senior
SOC Manager (oversight only)	Viewer
Compliance auditor	Viewer
Security engineer (builds rules)	Super Admin
IT support (needs to check alerts)	Viewer or Junior
API Reference Summary
Command Queue APIs
GET /api/v1/commands/poll/
Auth: Agent token
Response:

// If command exists
{
  "command_id": "uuid",
  "type": "kill_process",
  "parameters": {"pid": 1234}
}
// If no command
204 No Content
POST /api/v1/commands/result/<command_id>/
Auth: Agent token
Request:

{
  "status": "success",
  "message": "Process 1234 terminated",
  "exit_code": 1
}
Response: 200 OK

POST /api/v1/response/kill_process/
Auth: User token (requires can_kill_process permission)
Request:

{
  "agent_id": "DESKTOP-ABC123",
  "pid": 3456,
  "alert_id": "optional-alert-uuid"
}
Response:

{
  "status": "queued",
  "command_id": "uuid",
  "message": "Command will execute within 5-10 seconds"
}
Errors:

403 - Insufficient permissions
400 - Missing parameters
404 - Agent not found
GET /api/v1/commands/status/<command_id>/
Auth: User token
Purpose: Poll command status (for UI progress indicator)
Response:

{
  "command_id": "uuid",
  "status": "completed",  // new, in_progress, completed, failed, timeout
  "result": {
    "status": "success",
    "message": "Process killed"
  },
  "created_at": "2025-11-21T12:00:00Z",
  "completed_at": "2025-11-21T12:00:08Z"
}
Security Considerations
1. Command Injection Prevention
Risk: Malicious admin injects shell commands via parameters

Mitigation:

Validate all inputs server-side (PID must be integer, agent_id alphanumeric)
Agent validates command types (whitelist approach)
Never use system() with user input directly
Use Windows APIs (TerminateProcess, not taskkill.exe /f /pid <user_input>)
2. Privilege Escalation
Risk: Attacker compromises Junior analyst account, escalates to kill processes

Mitigation:

RBAC strictly enforced at API level (not just UI)
All actions logged with analyst email
Monitor anomalous behavior (e.g., 100 kill commands in 1 minute)
3. Replay Attacks
Risk: Attacker captures HTTP request, replays it later

Mitigation:

Commands expire after 5 minutes
Command IDs are UUIDs (random, not sequential)
Agent token rotation (Phase 2)
4. Audit Trail Integrity
Risk: Malicious admin deletes audit logs to cover tracks

Mitigation:

MongoDB write-once (application-level, no delete permission)
Export logs to immutable storage (S3, Glacier) - Phase 2
Alert on bulk deletes (SIEM integration) - Phase 3
Success Metrics
Functionality
✅ Can kill process within 10 seconds (polling + execution time)
✅ Can isolate host within 10 seconds
✅ Isolation blocks external traffic but preserves EDR communication
✅ RBAC prevents unauthorized actions (Junior cannot kill)
Reliability
✅ 100% command delivery (if agent online)
✅ <1% command failure rate (excluding access denied)
✅ Zero crashes or agent hangs
✅ All actions logged (no data loss)
Security
✅ All actions require valid token
✅ Permission checks enforced at API level
✅ Audit trail immutable (append-only)
Usability
✅ Dashboard shows clear feedback ("Command queued...")
✅ Disabled buttons show tooltip (why disabled)
✅ Audit trail filterable by agent, analyst, date
Next Steps
Review this plan - Approve architecture decisions
Questions to answer:
Should Junior analysts be able to kill processes? (currently set to NO)
What should command polling interval be? (currently 5 seconds)
Should we implement approval workflow now or later? (currently later)
Begin implementation following Phase 1 timeline
Deploy to test environment before production
Document Status: ✅ Ready for Implementation
Estimated Completion: 1.5 developer days (12 hours)
Risk Level: 🟡 Medium (security & privilege implications)