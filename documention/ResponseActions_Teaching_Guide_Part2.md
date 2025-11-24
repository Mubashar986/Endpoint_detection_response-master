# PART 3: HOST ISOLATION (NETWORK CONTAINMENT)

## 1. Concept Breakdown

### What is Host Isolation?
**Host isolation** means **blocking all network traffic** from/to an endpoint, except communication with the EDR server. It's like putting the infected computer in quarantine so it cannot:
- Spread malware to other machines (lateral movement)
- Exfiltrate stolen data to attacker servers
- Receive commands from Command & Control (C2) servers

### The Problem It Solves
When malware is detected but you're not sure of its full impact yet:
- Ransomware might be spreading to network shares
- Data breach might be ongoing
- You need time to investigate without risking further damage

---

## 2. Mental Model (How To Think About It)

### Django/Python Analogy
In Python web development, you might block users with middleware:
```python
# Block a  malicious IP
BLOCKED_IPS = ['192.168.1.100']

class BlockIPMiddleware:
    def __init__(self, get_response):
        self.get_response = get_response
    
    def __call__(self, request):
        if request.META['REMOTE_ADDR'] in BLOCKED_IPS:
            return HttpResponseForbidden("Blocked")
        return self.get_response(request)
```

### Windows C++ Reality
In Windows, you must configure the **Windows Firewall** (or use kernel drivers) to block network traffic at the **packet level**.

```
┌──────────────────────────────────────┐
│       Application Layer              │  ← Python middleware works here
│  (HTTP requests, web browsers)       │
└───────────────┬──────────────────────┘
                │
┌───────────────▼──────────────────────┐
│       Transport Layer (TCP/UDP)      │
└───────────────┬──────────────────────┘
                │
┌───────────────▼──────────────────────┐
│       Network Layer (IP)             │  ← Windows Firewall works here
│   ┌────────────────────────────┐    │
│   │  Windows Filtering Platform│    │  ← EDR works here (kernel level)
│   └────────────────────────────┘    │
└───────────────┬──────────────────────┘
                │
            [Network Card]
```

**Key Difference**: Python blocks at application level. Firewall blocks at network level (ALL applications affected).

---

## 3. Cross-Language Comparison

### Python (High-Level Firewall Control)
```python
import subprocess

# Windows firewall rule (via PowerShell)
def isolate_host(server_ip):
    # Block all outbound
    subprocess.run([
        'netsh', 'advfirewall', 'firewall', 'add', 'rule',
        'name=EDR_BLOCK_ALL',
        'dir=out',
        'action=block'
    ])
    
    # Allow EDR server
    subprocess.run([
        'netsh', 'advfirewall', 'firewall', 'add', 'rule',
        'name=EDR_ALLOW_SERVER',
        'dir=out',
        'action=allow',
        f'remoteip={server_ip}',
        'protocol=TCP'
    ])
```

**Characteristics**:
- **Simple**: Just calls `netsh` command
- **Not native**: Uses subprocess (shell execution risk)
- **Error handling**: Hard to parse `netsh` output
- **Privileged**: Requires admin rights

### C++ (Multiple Approaches)

#### Option 1: Shell Command (Like Python)
```cpp
int result = system("netsh advfirewall firewall add rule name=\"EDR_BLOCK_ALL\" dir=out action=block");
```
**Pros**: Simple  
**Cons**: Security risk (command injection), hard to parse errors

#### Option 2: Windows API (WFP - Windows Filtering Platform)
```cpp
#include <fwpmu.h>

bool isolateHostWFP(const std::string& serverIp) {
    HANDLE engineHandle;
    DWORD result = FwpmEngineOpen0(NULL, RPC_C_AUTHN_DEFAULT, NULL, NULL, &engineHandle);
    
    if (result != ERROR_SUCCESS) return false;
    
    // Create filter to block all traffic
    FWPM_FILTER0 filter = {0};
    filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;  // IPv4 outbound
    filter.action.type = FWP_ACTION_BLOCK;
    filter.weight.type = FWP_UINT8;
    filter.weight.uint8 = 15;  // High priority
    
    UINT64 filterId;
    result = FwpmFilterAdd0(engineHandle, &filter, NULL, &filterId);
    
    // TODO: Add exception for EDR server IP
    
    FwpmEngineClose0(engineHandle);
    return result == ERROR_SUCCESS;
}
```

**Key Differences**:

| Aspect | Python (subprocess) | C++ (system()) | C++ (WFP API) |
|--------|---------------------|----------------|---------------|
| **Security** | Command injection risk | Same risk | Safe (no shell) |
| **Error Handling** | Parse text output | Return code only | Detailed error codes |
| **Performance** | Spawns shell process | Spawns shell | Direct API calls |
| **Granularity** | Rule-based | Rule-based | Packet-level filters |
| **Complexity** | 5 lines | 1 line | 50+ lines |

---

## 4. Why This Concept Exists

### Security Containment
When malware is discovered:
1. **Immediate threat**: Stop data exfiltration NOW
2. **Investigation time**: Prevent spread while SOC investigates
3. **Compliance**: Regulations require "incident containment" (GDPR Art. 33)

### Real-World Example (NotPetya Ransomware, 2017)
- Spread via network shares in minutes
- Companies that **isolated infected hosts immediately** limited damage
- Companies that didn't lost entire networks ($10B+ global damage)

---

## 5. **5 DIFFERENT APPROACHES TO HOST ISOLATION**

### Approach 1: **netsh Command (Simple MVP)**
```cpp
bool isolateHost(const std::string& serverIp, int serverPort) {
    // Block all outbound traffic
    std::string cmd1 = "netsh advfirewall firewall add rule name=\"EDR_BLOCK_ALL\" dir=out action=block";
    
    // Allow EDR server
    std::string cmd2 = "netsh advfirewall firewall add rule name=\"EDR_ALLOW_SERVER\" dir=out action=allow "
                       "remoteip=" + serverIp + " protocol=TCP remoteport=" + std::to_string(serverPort);
    
    // Allow DNS (so hostname resolution works)
    std::string cmd3 = "netsh advfirewall firewall add rule name=\"EDR_ALLOW_DNS\" dir=out action=allow "
                       "protocol=UDP remoteport=53";
    
    int result1 = system(cmd1.c_str());
    int result2 = system(cmd2.c_str());
    int result3 = system(cmd3.c_str());
    
    return (result1 == 0 && result2 == 0 && result3 == 0);
}

bool deisolateHost() {
    system("netsh advfirewall firewall delete rule name=\"EDR_BLOCK_ALL\"");
    system("netsh advfirewall firewall delete rule name=\"EDR_ALLOW_SERVER\"");
    system("netsh advfirewall firewall delete rule name=\"EDR_ALLOW_DNS\"");
    return true;
}
```

**Pros**:
- ✅ **Simple**: 20 lines of code
- ✅ **Fast**: Works in < 1 second
- ✅ **Reversible**: Easy to undo

**Cons**:
- ❌ **User can bypass**: User can disable firewall via GUI
- ❌ **Command injection**: If `serverIp` contains semicolons
- ❌ **No error details**: `system()` returns 0 or 1

**Best For**: MVP, learning, small deployments

---

### Approach 2: **CreateProcess (Safer than system())**
```cpp
#include <Windows.h>

bool runNetshCommand(const std::string& args) {
    std::string fullCmd = "netsh.exe " + args;
    
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    
    BOOL success = CreateProcessA(
        NULL,                       // Application name (NULL = use command line)
        (LPSTR)fullCmd.c_str(),    // Command line
        NULL, NULL,                 // Security attributes
        FALSE,                      // Inherit handles
        CREATE_NO_WINDOW,           // Creation flags (hidden window)
        NULL,                       // Environment
        NULL,                       // Current directory
        &si,                        // Startup info
        &pi                         // Process info (output)
    );
    
    if (!success) {
        std::cerr << "CreateProcess failed: " << GetLastError() << std::endl;
        return false;
    }
    
    // Wait for netsh to finish
    WaitForSingleObject(pi.hProcess, 5000);  // 5 second timeout
    
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return exitCode == 0;
}

bool isolateHost(const std::string& serverIp) {
    bool r1 = runNetshCommand("advfirewall firewall add rule name=EDR_BLOCK_ALL dir=out action=block");
    bool r2 = runNetshCommand("advfirewall firewall add rule name=EDR_ALLOW_SERVER dir=out action=allow remoteip=" + serverIp);
    return r1 && r2;
}
```

**Pros**:
- ✅ **Safer**: No shell execution (no command injection)
- ✅ **Exit codes**: Can check if `netsh` actually succeeded
- ✅ **Hidden window**: User doesn't see command prompt flashing

**Cons**:
- ❌ **More code**: 40 lines vs 10 lines with `system()`
- ❌ **Still user-bypassable**: Firewall GUI can disable rules

**Best For**: Production systems where security matters

---

### Approach 3: **Windows Filtering Platform (WFP) - Kernel Level**
```cpp
#include <fwpmu.h>
#pragma comment(lib, "Fwpuclnt.lib")

class NetworkIsolator {
private:
    HANDLE engineHandle = NULL;
    UINT64 blockFilterId = 0;
    UINT64 allowFilterId = 0;
    
public:
    bool initialize() {
        DWORD result = FwpmEngineOpen0(
            NULL,                       // Server name (NULL = local)
            RPC_C_AUTHN_DEFAULT,       // Authentication service
            NULL,                       // Auth identity (NULL = caller)
            NULL,                       // Session (NULL = dynamic)
            &engineHandle
        );
        
        return result == ERROR_SUCCESS;
    }
    
    bool isolate(const std::string& allowedIp, UINT16 allowedPort) {
        // Step 1: Block ALL outbound TCP/UDP
        FWPM_FILTER0 blockFilter = {0};
        blockFilter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;  // IPv4 outbound
        blockFilter.action.type = FWP_ACTION_BLOCK;
        blockFilter.weight.type = FWP_UINT8;
        blockFilter.weight.uint8 = 10;  // Priority 10
        blockFilter.flags = FWPM_FILTER_FLAG_PERSISTENT;  // Survives reboot
        
        DWORD result = FwpmFilterAdd0(engineHandle, &blockFilter, NULL, &blockFilterId);
        if (result != ERROR_SUCCESS) return false;
        
        // Step 2: Allow EDR server IP:Port
        FWPM_FILTER0 allowFilter = {0};
        allowFilter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
        allowFilter.action.type = FWP_ACTION_PERMIT;
        allowFilter.weight.type = FWP_UINT8;
        allowFilter.weight.uint8 = 15;  // Higher priority (15 > 10)
        
        // Add condition: remoteip == allowedIp
        FWPM_FILTER_CONDITION0 conditions[2] = {0};
        
        // Condition 1: Remote IP
        FWP_V4_ADDR_AND_MASK addrMask;
        inet_pton(AF_INET, allowedIp.c_str(), &addrMask.addr);
        addrMask.mask = 0xFFFFFFFF;  // Exact match
        
        conditions[0].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
        conditions[0].matchType = FWP_MATCH_EQUAL;
        conditions[0].conditionValue.type = FWP_V4_ADDR_MASK;
        conditions[0].conditionValue.v4AddrMask = &addrMask;
        
        // Condition 2: Remote Port
        conditions[1].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
        conditions[1].matchType = FWP_MATCH_EQUAL;
        conditions[1].conditionValue.type = FWP_UINT16;
        conditions[1].conditionValue.uint16 = allowedPort;
        
        allow Filter.numFilterConditions = 2;
        allowFilter.filterCondition = conditions;
        
        result = FwpmFilterAdd0(engineHandle, &allowFilter, NULL, &allowFilterId);
        
        return result == ERROR_SUCCESS;
    }
    
    bool deisolate() {
        FwpmFilterDeleteById0(engineHandle, blockFilterId);
        FwpmFilterDeleteById0(engineHandle, allowFilterId);
        return true;
    }
    
    ~NetworkIsolator() {
        if (engineHandle) {
            FwpmEngineClose0(engineHandle);
        }
    }
};

// Usage
NetworkIsolator isolator;
isolator.initialize();
isolator.isolate("192.168.1.100", 8443);  // Allow only EDR server
// ... later ...
isolator.deisolate();
```

**Windows Filtering Platform Concepts**:
- **Layers**: Different network stack levels (IP, TCP, UDP, etc.)
  - `FWPM_LAYER_ALE_AUTH_CONNECT_V4`: IPv4 outbound connections
  - `FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4`: IPv4 inbound connections
- **Filters**: Rules with conditions (if IP == X, then BLOCK/PERMIT)
- **Weight**: Priority (higher weight = evaluated first)
- **Persistent**: Filters survive reboots

**Pros**:
- ✅ **Kernel-level**: User **cannot bypass** via GUI (requires admin/SYSTEM to remove)
- ✅ **Granular**: Can allow specific ports, protocols, IPs
- ✅ **Performance**: Packet filtering in kernel is fast
- ✅ **Professional**: This is how real EDRs work

**Cons**:
- ❌ **Very complex**: 100+ lines of code
- ❌ **Steep learning curve**: Must understand WFP architecture
- ❌ **Windows Vista+ only**: XP/2003 not supported

**Best For**: Production EDR, enterprise deployments

---

### Approach 4: **Disable Network Adapters (Nuclear Option)**
```cpp
#include <netcon.h>  // Network Connections API
#pragma comment(lib, "ole32.lib")

bool toggleNetworkAdapter(bool enable) {
    CoInitialize(NULL);
    
    INetConnectionManager* pManager = NULL;
    HRESULT hr = CoCreateInstance(
        CLSID_ConnectionManager,
        NULL,
        CLSCTX_SERVER,
        IID_INetConnectionManager,
        (void**)&pManager
    );
    
    if (FAILED(hr)) {
        CoUninitialize();
        return false;
    }
    
    IEnumNetConnection* pEnum = NULL;
    pManager->EnumConnections(NCME_DEFAULT, &pEnum);
    
    INetConnection* pConn = NULL;
    ULONG fetched;
    
    while (pEnum->Next(1, &pConn, &fetched) == S_OK) {
        NETCON_PROPERTIES* props;
        pConn->GetProperties(&props);
        
        if (props->MediaType == NCM_LAN || props->MediaType == NCM_SHAREDACCESSHOST_RAS) {
            if (enable) {
                pConn->Connect();
            } else {
                pConn->Disconnect();
            }
        }
        
        CoTaskMemFree(props);
        pConn->Release();
    }
    
    pEnum->Release();
    pManager->Release();
    CoUninitialize();
    
    return true;
}

bool isolateHost() {
    return toggleNetworkAdapter(false);  // Disable all adapters
}

bool deisolateHost() {
    return toggleNetworkAdapter(true);  // Re-enable
}
```

**Pros**:
- ✅ **Complete isolation**: No network traffic at all
- ✅ **Simple concept**: Just disable NIC

**Cons**:
- ❌ **Breaks EDR communication**: Agent cannot report back!
- ❌ **User notices**: Network icon shows disconnected
- ❌ **Breaks everything**: RDP, file shares, even localhost

**Best For**: **NEVER USE THIS** (except for offline forensics machines)

---

### Approach 5: **Group Policy + Windows Firewall (Enterprise)**
```cpp
// Pseudocode - This approach uses Active Directory Group Policy

// Step 1: Create GPO on domain controller
CreateGPO("EDR Isolation Policy");

// Step 2: Configure firewall settings in GPO
SetGPOFirewallRule("Block All Outbound", BLOCK, OUTBOUND);
SetGPOFirewallRule("Allow EDR Server", ALLOW, OUTBOUND, "192.168.1.100:8443");

// Step 3: Link GPO to computer's OU (Organizational Unit)
LinkGPO("OU=Quarantine,DC=company,DC=com", "EDR Isolation Policy");

// Step 4: Force GPO refresh on target computer
system("gpupdate /force /computer:PC-1234");

// To deisolate:
UnlinkGPO("OU=Quarantine,DC=company,DC=com");
```

**How it Works**:
1. EDR server talks to Active Directory
2. Moves computer object to "Quarantine" OU
3. GPO applies firewall rules automatically
4. Cannot be bypassed by local admin

**Pros**:
- ✅ **Centralized**: Managed from EDR server, not agent
- ✅ **Cannot bypass**: Domain policy overrides local settings
- ✅ **Audit trail**: AD logs all GPO changes

**Cons**:
- ❌ **Requires Active Directory**: Only works in domain environments
- ❌ **Slow**: GPO refresh can take 5-15 minutes
- ❌ **Complex setup**: Must integrate with AD

**Best For**: Enterprise environments with Active Directory

---

## 6. Common Mistakes

### ❌ Mistake 1: Forgetting to Allow DNS
```cpp
// Block all traffic
system("netsh advfirewall firewall add rule name=EDR_BLOCK_ALL dir=out action=block");

// Allow EDR server by IP
system("netsh advfirewall firewall add rule name=EDR_ALLOW_SERVER dir=out action=allow remoteip=192.168.1.100");

// ❌ Problem: If agent uses edr.company.com (hostname), DNS resolution fails!
```

**Fix**: Always allow UDP port 53 (DNS)
```cpp
system("netsh advfirewall firewall add rule name=EDR_ALLOW_DNS dir=out action=allow protocol=UDP remoteport=53");
```

### ❌ Mistake 2: Blocking Inbound AND Outbound
```cpp
system("netsh advfirewall firewall add rule name=EDR_BLOCK_ALL dir=in action=block");
system("netsh advfirewall firewall add rule name=EDR_BLOCK_ALL dir=out action=block");

// ❌ Problem: Blocks HTTP responses from EDR server!
```

**Fix**: Only block **outbound**. Inbound responses to agent-initiated connections are automatically allowed.

### ❌ Mistake 3: Not Testing Deisolation
```cpp
bool isolateHost() {
    system("netsh advfirewall firewall add rule name=EDR_BLOCK_ALL dir=out action=block");
    return true;
}

// ❌ Forgot to implement deisolateHost()!
// Analyst clicks "Remove Isolation" → Nothing happens → Host stays isolated forever
```

**Fix**: Always implement and TEST deisolation before deploying.

### ❌ Mistake 4: Hardcoding Server IP
```cpp
bool isolateHost() {
    system("netsh advfirewall firewall add rule ... remoteip=192.168.1.100");  // ❌ What if server IP changes?
}
```

**Fix**: Read from config
```cpp
ConfigReader config("config.json");
std::string serverIp = config.getServerIp();
```

---

## 7. Implementation Guide (Approach 2 - CreateProcess)

Full implementation provided in next section after RBAC.

---

## 8. Best Practices

1. **Always allow EDR server + DNS**
   ```cpp
   // Priority order:
   // 1. Allow EDR server (highest priority)
   // 2. Allow DNS
   // 3. Block everything else
   ```

2. **Log isolation events**
   ```cpp
   std::cout << "[ISOLATE] Host isolated at " << getCurrentTimestamp() << std::endl;
   // Also send log event to server
   ```

3. **Test deisolation immediately**
   ```cpp
   if (isolateHost()) {
       Sleep(5000);  // Wait 5 seconds
       if (!deisolateHost()) {
           std::cerr << "CRITICAL: Cannot deisolate! Manual intervention needed" << std::endl;
       }
   }
   ```

4. **Use persistent rules** (WFP)
   ```cpp
   filter.flags = FWPM_FILTER_FLAG_PERSISTENT;  // Survives reboots
   ```

---

## 9. Where You Will Use This In The System

```
Alert: Lateral Movement Detected
         ↓
SOC Analyst: "Isolate this host NOW"
         ↓
Dashboard: Click "Isolate Host" button
         ↓
Backend: POST /api/v1/response/isolate_host/
         ↓
Command Queue: {"agent": "PC-1", "type": "isolate_host"}
         ↓
Agent Polls: Receives command
         ↓
CommandProcessor::isolateHost(serverIp, serverPort)
         ↓
netsh creates firewall rules
         ↓
Result: Host can only talk to EDR server
         ↓
Dashboard: "Host isolated ✅ (Ping fails, EDR still active)"
```

---

## 10. Confidence Notes

✅ **You now understand**:
- 5 isolation approaches (netsh, CreateProcess, WFP, Adapter Disable, GPO)
- When to use each (MVP vs Production vs Enterprise)
- Common pitfalls (DNS, hardcoding, no deisolation testing)

---

# PART 4: RBAC (ROLE-BASED ACCESS CONTROL)

## 1. Concept Breakdown

### What is RBAC?
**Role-Based Access Control** means assigning **permissions** to **roles** instead of individual users.

Example:
- User "alice@company.com" → Role "SOC Analyst (Senior)" → Permissions [kill_process, isolate_host]
- User "bob@company.com" → Role "SOC Analyst (Junior)" → Permissions [view_alerts, add_notes]

### The Problem It Solves
Without RBAC:
- Junior analysts might accidentally kill critical processes
- Compliance violations (who authorized this kill?)
- Chaos (everyone has full admin access)

---

## 2. Mental Model

### Django Built-in RBAC
Django has **Permissions** and **Groups** built-in:

```python
from django.contrib.auth.models import User, Group, Permission

# Create groups (roles)
junior = Group.objects.create(name='SOC Analyst (Junior)')
senior = Group.objects.create(name='SOC Analyst (Senior)')

# Assign permissions
junior.permissions.add(Permission.objects.get(codename='view_alert'))
senior.permissions.add(
    Permission.objects.get(codename='view_alert'),
    Permission.objects.get(codename='can_kill_process'),
)

# Assign user to group
alice = User.objects.get(username='alice')
alice.groups.add(senior)

# Check permission
if alice.has_perm('ingestion.can_kill_process'):
    kill_process(pid)
```

### C++ Comparison
In C++, you'd implement RBAC manually:

```cpp
enum Permission {
    VIEW_ALERT = 1 << 0,      // 0001
    KILL_PROCESS = 1 << 1,    // 0010
    ISOLATE_HOST = 1 << 2,    // 0100
    MANAGE_RULES = 1 << 3,    // 1000
};

struct Role {
    std::string name;
    uint32_t permissions;  // Bitmask
};

Role juniorAnalyst = {"Junior", VIEW_ALERT};
Role seniorAnalyst = {"Senior", VIEW_ALERT | KILL_PROCESS | ISOLATE_HOST};

struct User {
    std::string email;
    Role role;
};

bool hasPermission(const User& user, Permission perm) {
    return (user.role.permissions & perm) != 0;
}

// Usage
User alice = {"alice@company.com", seniorAnalyst};
if (hasPermission(alice, KILL_PROCESS)) {
    killProcess(1234);
}
```

**Key Difference**:
- **Django**: Database-backed, dynamic permissions
- **C++**: In-memory, bitmask optimizations

---

## 3. **5 DIFFERENT APPROACHES TO RBAC**

### Approach 1: **Django Built-in Groups + Permissions (Recommended)**
```python
# models.py
from django.contrib.auth.models import Permission
from django.contrib.contenttypes.models import ContentType

# Create custom permissions
content_type = ContentType.objects.get_for_model(Alert)
Permission.objects.get_or_create(
    codename='can_kill_process',
    name='Can kill processes',
    content_type=content_type
)

# views.py
from django.contrib.auth.decorators import permission_required

@permission_required('ingestion.can_kill_process', raise_exception=True)
def kill_process_action(request):
    # Only users with kill_process permission reach here
    pass
```

**Pros**:
- ✅ **Built-in**: No external libraries
- ✅ **Django Admin integration**: Manage users/roles via GUI
- ✅ **Database-backed**: Changes persist automatically

**Cons**:
- ❌ **Not hierarchical**: Cannot do "Senior inherits Junior permissions" explicitly

**Best For**: Django projects (your case!)

---

### Approach 2: **django-guardian (Object-Level Permissions)**
```python
from guardian.shortcuts import assign_perm, get_objects_for_user

# Assign permission on specific alert
assign_perm('view_alert', user, alert_obj)

# Get all alerts user can view
alerts = get_objects_for_user(user, 'ingestion.view_alert')
```

**Pros**:
- ✅ **Granular**: "Alice can view ONLY alerts assigned to her"
- ✅ **Row-level security**: Database enforces permissions

**Cons**:
- ❌ **Overkill for MVP**: Response actions apply to ALL hosts, not specific ones

**Best For**: Multi-tenant SaaS EDR (each customer sees only their own data)

---

### Approach 3: **Custom Decorator (Simple Middleware)**
```python
from functools import wraps
from rest_framework.response import Response

ROLE_PERMISSIONS = {
    'viewer': [],
    'junior': ['view_alert', 'add_note'],
    'senior': ['view_alert', 'add_note', 'kill_process', 'isolate_host'],
    'admin': ['*'],  # All permissions
}

def require_role(min_role):
    def decorator(view_func):
        @wraps(view_func)
        def wrapper(request, *args, **kwargs):
            user_role = request.user.profile.role  # Assuming User has profile.role
            
            if min_role == 'admin' and user_role != 'admin':
                return Response({'error': 'Admin only'}, status=403)
            
            if min_role == 'senior' and user_role not in ['senior', 'admin']:
                return Response({'error': 'Senior analyst required'}, status=403)
            
            return view_func(request, *args, **kwargs)
        return wrapper
    return decorator

# Usage
@require_role('senior')
def kill_process_action(request):
    pass
```

**Pros**:
- ✅ **Simple**: 20 lines of code
- ✅ **Readable**: `@require_role('senior')` is self-documenting

**Cons**:
- ❌ **Not Django standard**: Reinvents the wheel
- ❌ **Hardcoded roles**: Changing roles requires code deploy

**Best For**: Prototypes, learning

---

### Approach 4: **RBAC Library (django-role-permissions)**
```python
from rolepermissions.roles import AbstractUserRole

class JuniorAnalyst(AbstractUserRole):
    available_permissions = {
        'view_alerts': True,
        'add_notes': True,
    }

class SeniorAnalyst(AbstractUserRole):
    available_permissions = {
        'view_alerts': True,
        'add_notes': True,
        'kill_process': True,
        'isolate_host': True,
    }

# Assign role
assign_role(user, 'junior_analyst')

# Check permission
if has_permission(user, 'kill_process'):
    kill_process()
```

**Pros**:
- ✅ **Clean API**: Role definitions in Python classes
- ✅ **Hierarchical roles**: Can define role inheritance

**Cons**:
- ❌ **External dependency**: Another package to maintain
- ❌ **Not as popular**: django-guardian is more widely used

**Best For**: Complex RBAC needs (many roles, inheritance)

---

### Approach 5: **API Gateway RBAC (Separate Service)**
```
┌──────────────┐
│  Dashboard   │
│  (Frontend)  │
└──────┬───────┘
       │ POST /kill_process (with JWT token)
       ▼
┌──────────────────┐
│  API Gateway     │  ← RBAC Service (separate microservice)
│  (Check token)   │
│  Decode JWT:     │
│  {user: "alice"  │
│   role: "senior"}│
└──────┬───────────┘
       │ If authorized, forward to backend
       ▼
┌──────────────────┐
│  Django Backend  │  ← Trusts API Gateway (no RBAC here)
│  (Executes kill) │
└──────────────────┘
```

**Pros**:
- ✅ **Centralized**: One RBAC service for multiple backends
- ✅ **Language-agnostic**: Backend can be C++, Python, Go

**Cons**:
- ❌ **Very complex**: Microservice architecture
- ❌ **Single point of failure**: If gateway dies, everything breaks

**Best For**: Massive enterprise systems (1000+ services)

---

## 4. Proposed Role Matrix (For Your EDR)

| Action | Viewer | Junior | Senior | Admin |
|--------|--------|--------|--------|-------|
| View alerts | ✅ | ✅ | ✅ | ✅ |
| Add notes | ❌ | ✅ | ✅ | ✅ |
| Assign alerts | ❌ | ✅ | ✅ | ✅ |
| Kill process | ❌ | ❌ | ✅ | ✅ |
| Isolate host | ❌ | ❌ | ✅ | ✅ |
| Create rules | ❌ | ❌ | ❌ | ✅ |
| Manage users | ❌ | ❌ | ❌ | ✅ |

---

## 5. Implementation Guide (Approach 1 - Django Built-in)

```python
# ingestion/management/commands/setup_rbac.py
from django.core.management.base import BaseCommand
from django.contrib.auth.models import Group, Permission
from django.contrib.contenttypes.models import ContentType
from ingestion.detection_models import Alert

class Command(BaseCommand):
    def handle(self, *args, **kwargs):
        # Create custom permissions
        content_type = ContentType.objects.get_for_model(Alert)
        
        can_kill, _ = Permission.objects.get_or_create(
            codename='can_kill_process',
            name='Can terminate processes on endpoints',
            content_type=content_type
        )
        
        can_isolate, _ = Permission.objects.get_or_create(
            codename='can_isolate_host',
            name='Can isolate endpoints from network',
            content_type=content_type
        )
        
        can_manage_rules, _ = Permission.objects.get_or_create(
            codename='can_manage_rules',
            name='Can create/edit detection rules',
            content_type=content_type
        )
        
        # Create groups
        viewer, _ = Group.objects.get_or_create(name='Viewer')
        junior, _ = Group.objects.get_or_create(name='SOC Analyst (Junior)')
        senior, _ = Group.objects.get_or_create(name='SOC Analyst (Senior)')
        admin, _ = Group.objects.get_or_create(name='Super Admin')
        
        # Assign permissions
        junior.permissions.add(
            Permission.objects.get(codename='view_alert'),
            Permission.objects.get(codename='change_alert'),  # Add notes
        )
        
        senior.permissions.add(
            *junior.permissions.all(),
            can_kill,
            can_isolate
        )
        
        admin.permissions.add(
            *senior.permissions.all(),
            can_manage_rules,
            Permission.objects.get(codename='add_user'),
            Permission.objects.get(codename='change_user'),
        )
        
        self.stdout.write(self.style.SUCCESS('RBAC setup complete'))
```

**Run once**:
```bash
python manage.py setup_rbac
```

**In views**:
```python
from django.contrib.auth.decorators import permission_required
from rest_framework.decorators import api_view, permission_classes
from rest_framework.permissions import IsAuthenticated

@api_view(['POST'])
@permission_classes([IsAuthenticated])
@permission_required('ingestion.can_kill_process', raise_exception=True)
def kill_process_action(request):
    agent_id = request.data['agent_id']
    pid = request.data['pid']
    
    # Create command in queue
    command = PendingCommand(
        agent_id=agent_id,
        command_type='kill_process',
        parameters={'pid': pid},
        issued_by=request.user.email
    )
    command.save()
    
    return Response({'status': 'queued'})
```

---

## 6. Best Practices

1. **Principle of Least Privilege**
   - Default role: Viewer (read-only)
   - Junior analysts: 6-12 months experience
   - Senior analysts: Proven track record

2. **Audit All Privileged Actions**
   ```python
   # Log who did what
   AuditLog.objects.create(
       user=request.user,
       action='kill_process',
       target=f'PID {pid} on {agent_id}',
       timestamp=timezone.now()
   )
   ```

3. **Require MFA for Destructive Actions**
   ```python
   if not request.user.mfa_verified:
       return Response({'error': 'MFA required for kill/isolate'}, status=403)
   ```

4. **Role Review Every 6 Months**
   - People leave, roles change
   - Automate "Alice hasn't logged in for 90 days → Disable account"

---

## 7. Confidence Notes

✅ **You now understand**:
- 5 RBAC approaches (Django built-in, django-guardian, custom, libraries, API gateway)
- When to use each
- How to implement with Django Groups + Permissions
- Best practices (least privilege, audit logs, MFA)

---

# SUMMARY & NEXT STEPS

## What We've Covered

1. **HTTP Polling** (5 approaches)
   - Simple loop, Interruptible, Exponential backoff, Long polling, Async
   
2. **Process Termination** (5 approaches)
   - Simple TerminateProcess, Verified, Tree kill, Graceful, Kernel driver

3. **Host Isolation** (5 approaches)
   - netsh, CreateProcess, WFP, Adapter disable, Group Policy

4. **RBAC** (5 approaches)
   - Django built-in, django-guardian, Custom decorator, Libraries, API Gateway

---

## Recommended Choices for Your MVP

| Component | Approach | Reason |
|-----------|----------|--------|
| **HTTP Polling** | Approach 2 (Interruptible Sleep) | Graceful shutdown, production-ready |
| **Kill Process** | Approach 2 (Verified) + Approach 3 (Tree) | Confirm death + kill children |
| **Host Isolation** | Approach 2 (CreateProcess + netsh) | Safe, no command injection, simple |
| **RBAC** | Approach 1 (Django built-in) | Zero dependencies, Admin UI |

---

## Let's Implement Together!

Now that you understand all the concepts, I'm ready to help you **implement the code step-by-step**.

**Which part would you like to start with?**

1. ✅ C++ Agent (Polling + Kill + Isolate)
2. ✅ Django Backend (Command Queue + RBAC)
3. ✅ Dashboard UI (Buttons + Permissions)

**Tell me** and I'll guide you through the implementation with explanations at every step!
