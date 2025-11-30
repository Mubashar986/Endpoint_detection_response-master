"""
Quick Authentication & Telemetry Diagnostic
Tests if agent authentication and telemetry ingestion are working
"""

import sys
import os

# Add parent directory to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings')

import django
django.setup()

from rest_framework.authtoken.models import Token
from django.contrib.auth.models import User

print("="*70)
print("AUTHENTICATION & TELEMETRY DIAGNOSTIC")
print("="*70)

# 1. Check admin user token
print("\n1. Admin Token Check:")
try:
    admin_user = User.objects.filter(username='admin').first()
    if admin_user:
        token = Token.objects.get_or_create(user=admin_user)[0]
        print(f"   ✅ Admin user exists")
        print(f"   Username: {admin_user.username}")
        print(f"   Token: {token.key}")
        print(f"\n   ⚠️  UPDATE YOUR AGENT CONFIG WITH THIS TOKEN:")
        print(f"   auth.secret should contain: {token.key}")
    else:
        print(f"   ❌ Admin user not found")
        print(f"   Create admin: python manage.py createsuperuser")
except Exception as e:
    print(f"   ❌ Error: {e}")

# 2. Check all tokens
print(f"\n2. All Available Tokens:")
try:
    tokens = Token.objects.all()[:10]
    if tokens:
        for token in tokens:
            print(f"   User: {token.user.username:15} Token: {token.key}")
    else:
        print(f"   No tokens found")
except Exception as e:
    print(f"   ❌ Error: {e}")

# 3. Test telemetry serializer
print(f"\n3. Telemetry Serializer Test:")
try:
    from ingestion.serializers import TelemetrySerializer
    
    test_data = {
        'agent_id': 'TEST-AGENT',
        'event_id': 'diag-001',
        'event_type': 'file',
        'timestamp': 1700000000,
        'severity': 'INFO',
         'version': '1.0',
        'host': {'hostname': 'test-machine', 'os': 'Windows'},
        'file': {'path': 'C:\\test.txt', 'operation': 'write'}
    }
    
    serializer = TelemetrySerializer(data=test_data)
    if serializer.is_valid():
        print(f"   ✅ Serializer validation passed")
        print(f"   Validated data: {serializer.validated_data}")
    else:
        print(f"   ❌ Serializer validation failed")
        print(f"   Errors: {serializer.errors}")
        
except Exception as e:
    print(f"   ❌ Error: {e}")

# 4. Check Celery connection
print(f"\n4. Celery Connection:")
try:
    from celery import current_app
    
    # Try to ping Redis
    result = current_app.control.inspect().ping()
    if result:
        print(f"   ✅ Celery workers responding")
        for worker, status in result.items():
            print(f"   Worker: {worker} - Status: {status}")
    else:
        print(f"   ⚠️  No Celery workers detected")
        print(f"   Start worker: celery -A edr_server worker --pool=solo --loglevel=info")
except Exception as e:
    print(f"   ⚠️  Cannot connect to workers: {e}")
    print(f"   This is okay if worker is running in separate terminal")

# 5. Check Redis connection
print(f"\n5. Redis Connection:")
try:
    import redis
    r = redis.Redis(host='localhost', port=6379, db=0)
    r.ping()
    print(f"   ✅ Redis is running")
    
    # Check queue length
    queue_len = r.llen('celery')
    print(f"   Celery queue length: {queue_len}")
    
except Exception as e:
    print(f"   ❌ Redis error: {e}")
    print(f"   Start Redis: redis-server")

print(f"\n{'='*70}")
print("DIAGNOSTIC COMPLETE")
print(f"{'='*70}\n")

print("RECOMMENDED ACTIONS:")
print("1. Copy the admin token above to your agent's auth.secret file")
print("2. Ensure Celery worker is running: celery -A edr_server worker --pool=solo")
print("3. Ensure Redis is running: redis-server")
print("4. Restart your agent")
print("="*70)
