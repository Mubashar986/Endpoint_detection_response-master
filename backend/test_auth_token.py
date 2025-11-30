import urllib.request
import json

# 1. Read the token
try:
    with open(r'c:\Endpoint_detection_response-master\edr-agent\auth.secret', 'r') as f:
        token = f.read().strip()
    print(f"Read Token: {token[:5]}...{token[-5:]}")
except Exception as e:
    print(f"Error reading auth.secret: {e}")
    exit(1)

# 2. Test against server
url = 'http://127.0.0.1:8000/api/v1/telemetry/'
data = {
    "agent_id": "TEST_SCRIPT",
    "event_id": "TEST_001",
    "event_type": "process",
    "timestamp": 1234567890,
    "severity": "INFO",
    "raw_data": {"process": {"name": "test.exe"}}
}
json_data = json.dumps(data).encode('utf-8')

req = urllib.request.Request(url, data=json_data, method='POST')
req.add_header('Authorization', f'Token {token}')
req.add_header('Content-Type', 'application/json')

print(f"Testing URL: {url}")
try:
    with urllib.request.urlopen(req) as response:
        print(f"Status Code: {response.status}")
        print(f"Response: {response.read().decode()}")
        print("✅ SUCCESS: Token is valid!")
except urllib.error.HTTPError as e:
    print(f"❌ FAILURE: HTTP {e.code} - {e.reason}")
    print(e.read().decode())
except Exception as e:
    print(f"Connection Error: {e}")
