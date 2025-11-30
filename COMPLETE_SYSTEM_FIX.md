# Complete EDR System Fix Guide

## Current Status: 401 Unauthorized Error

### Quick Diagnosis Commands

Run these in order and send me the output:

```powershell
# 1. Check if auth.secret exists in the BUILD directory (where agent runs)
dir c:\Endpoint_detection_response-master\edr-agent\build\bin\auth.secret

# 2. Show the token content
type c:\Endpoint_detection_response-master\edr-agent\build\bin\auth.secret

# 3. Show valid tokens from database
cd c:\Endpoint_detection_response-master\backend
python -c "import os; import django; os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings'); django.setup(); from rest_framework.authtoken.models import Token; [print(f'{t.user.username}: {t.key}') for t in Token.objects.all()]"
```

### The Fix (Step by Step)

#### Step 1: Get a Valid Token
```powershell
cd c:\Endpoint_detection_response-master\backend
python -c "import os; import django; os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings'); django.setup(); from rest_framework.authtoken.models import Token; print(Token.objects.first().key if Token.objects.exists() else 'NO TOKENS')"
```

Copy the output (should be a long string like `55e39abc123def456...`).

#### Step 2: Create auth.secret in the CORRECT Location

**CRITICAL**: The agent runs from `build\bin`, NOT from the source directory.

```powershell
# Navigate to where the agent ACTUALLY runs
cd c:\Endpoint_detection_response-master\edr-agent\build\bin

# Create the file (replace YOUR_TOKEN_HERE with the actual token from Step 1)
echo YOUR_TOKEN_HERE > auth.secret

# Verify it was created
type auth.secret
```

#### Step 3: Verify Middleware is Not Interfering

The server has a `DecompressMiddleware`. Check if it's corrupting headers.

#### Step 4: Restart Everything

```powershell
# Stop the agent (Ctrl+C in its window)

# Restart it
cd c:\Endpoint_detection_response-master\edr-agent\build\bin
.\edr-agent-http.exe
```

#### Step 5: Check Agent Logs

You should see:
```
[ConfigReader] Using Auth Token from auth.secret file
```

NOT:
```
[ConfigReader] ERROR: Config contains placeholder token
```

### If Still Getting 401

Send me:
1. The EXACT content of `build\bin\auth.secret`
2. The full agent startup logs (first 20 lines)
3. The server logs when the agent tries to connect
