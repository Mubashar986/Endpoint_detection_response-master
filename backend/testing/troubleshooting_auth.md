# Authentication Troubleshooting Guide

## 1. The Problem
Your server logs show `401 Unauthorized` and `Received Auth Header: 'Token'`.
This means the Agent **is connecting** and sending a token, but the Server says **"This token is wrong"**.

## 2. How to Fix It

### Step 1: Find a Valid Token on the Server
Run this command in your backend terminal to see the real tokens:
```powershell
cd c:\Endpoint_detection_response-master\backend
python -c "import os; import django; os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings'); django.setup(); from rest_framework.authtoken.models import Token; print('\nVALID TOKENS:'); [print(f'User: {t.user.username} | Token: {t.key}') for t in Token.objects.all()]"
```
*Copy the long string of random characters (the Token).*

### Step 2: Check What the Agent is Using
Open this file:
`c:\Endpoint_detection_response-master\edr-agent\config.json`

Does it look like this?
```json
"auth_token": "PLACEHOLDER_USE_ENV_VAR_EDR_AUTH_TOKEN"
```
**That is the problem.** The agent is sending the word "PLACEHOLDER..." as the password.

### Step 3: The Fix
You have two options. **Option A** is better.

**Option A: Create the Secret File (Secure)**
1. Go to `c:\Endpoint_detection_response-master\edr-agent\`
2. Create a new file named `auth.secret` (no extension).
3. Paste **only** the valid token from Step 1 into it.
4. Save.

**Option B: Edit Config (Easy)**
1. Open `config.json`.
2. Replace the placeholder with your real token.
```json
"auth_token": "YOUR_REAL_TOKEN_HERE"
```

### Step 4: Restart Agent
Close the agent window and run it again. It should now say `HTTP 201` (Success).
