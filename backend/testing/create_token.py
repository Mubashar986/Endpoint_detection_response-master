import os
import django
from django.conf import settings

# Setup Django environment
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings')
django.setup()

from django.contrib.auth.models import User
from rest_framework.authtoken.models import Token

def create_token():
    username = "Mubashar"
    email = "mubashirmaitlo@gmail.com"
    password = "1234"

    try:
        user = User.objects.get(username=username)
        print(f"User '{username}' already exists.")
    except User.DoesNotExist:
        user = User.objects.create_superuser(username, email, password)
        print(f"Created superuser '{username}'.")

    token, created = Token.objects.get_or_create(user=user)
    print(f"Token: {token.key}")
    
    # Update config.json automatically? No, let's just print it for now.
    # Actually, let's update it to be helpful.
    import json
    config_path = r"c:\Endpoint_detection_response-master\edr-agent\config.json"
    try:
        with open(config_path, "r") as f:
            config = json.load(f)
        
        if config.get("auth_token") != token.key:
            print(f"Updating config.json with new token...")
            config["auth_token"] = token.key
            with open(config_path, "w") as f:
                json.dump(config, f, indent=2)
            print("config.json updated!")
        else:
            print("config.json already has the correct token.")
            
    except Exception as e:
        print(f"Error updating config.json: {e}")

if __name__ == "__main__":
    create_token()
