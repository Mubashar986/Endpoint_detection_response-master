import requests
import os
import sys

# Configuration
BASE_URL = "http://localhost:8000/api/v1/commands/poll/"
VALID_TOKEN = "test_token_123" # Ensure this matches a valid token in your DB if you want 200 OK
INVALID_TOKEN = "PLACEHOLDER_USE_ENV_VAR_EDR_AUTH_TOKEN"

def test_auth(token, name):
    headers = {}
    if token:
        headers["Authorization"] = f"Token {token}"
    
    try:
        print(f"Testing {name}...")
        response = requests.get(BASE_URL, headers=headers)
        print(f"  Status: {response.status_code}")
        if response.status_code == 401:
            print("  Result: REJECTED (Correct for invalid token)")
        elif response.status_code == 200 or response.status_code == 204:
            print("  Result: ACCEPTED")
        else:
            print(f"  Result: Unexpected {response.status_code}")
    except Exception as e:
        print(f"  Error: {e}")
    print("-" * 30)

if __name__ == "__main__":
    print("=== Authentication Flow Verification ===\n")
    
    # 1. Test No Token
    test_auth(None, "No Token")
    
    # 2. Test Placeholder/Invalid Token
    test_auth(INVALID_TOKEN, "Placeholder Token")
    
    # 3. Test Valid Token (Simulated)
    # Note: This will only return 200/204 if 'test_token_123' exists in the Django DB.
    # Otherwise it will also be 401.
    test_auth(VALID_TOKEN, "Valid Token (Simulated)")
