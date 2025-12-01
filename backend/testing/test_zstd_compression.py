import urllib.request
import urllib.error
import zstandard as zstd
import json

# Configuration
URL = "http://127.0.0.1:8000/api/v1/telemetry/"
TOKEN = "ba76c2dca3772eba25e32d70288dced3bcb9cbc0"

# 1. Create Payload
payload = {
    "hostname": "test-host",
    "ip_address": "192.168.1.100",
    "os": "Windows 10",
    "events": [
        {"event_id": 1, "process_name": "test.exe"}
    ]
}
json_str = json.dumps(payload)
print(f"Original Size: {len(json_str)} bytes")

# 2. Compress
cctx = zstd.ZstdCompressor(level=3)
compressed_data = cctx.compress(json_str.encode('utf-8'))
print(f"Compressed Size: {len(compressed_data)} bytes")

# 3. Send Request
headers = {
    "Content-Type": "application/json",
    "Content-Encoding": "zstd",
    "Authorization": f"Token {TOKEN}"
}

req = urllib.request.Request(URL, data=compressed_data, headers=headers, method='POST')

try:
    print(f"Sending POST to {URL}...")
    with urllib.request.urlopen(req) as response:
        print(f"Status Code: {response.getcode()}")
        print(f"Response: {response.read().decode('utf-8')}")
        print("✅ Request sent successfully (Server reachable). Check server logs for decompression confirmation.")

except urllib.error.HTTPError as e:
    print(f"HTTP Error: {e.code}")
    print(f"Response: {e.read().decode('utf-8')}")
    if e.code in [401, 403]:
        print("✅ Request reached server (Auth failed as expected). Check server logs for decompression.")
    else:
        print("❌ Unexpected HTTP error.")

except urllib.error.URLError as e:
    print(f"❌ Connection failed: {e.reason}")
    print("Ensure the Django server is running on port 8000.")
