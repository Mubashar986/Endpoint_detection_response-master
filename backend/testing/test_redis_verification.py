"""
Redis Verification Script
Checks if rate limiting keys are being created in Redis correctly
"""

import redis
import time
from datetime import datetime

# Connect to Redis database 1 (rate limiting)
try:
    r = redis.Redis(host='localhost', port=6379, db=1, decode_responses=True)
    r.ping()
    print("✅ Connected to Redis (database 1)")
except Exception as e:
    print(f"❌ Cannot connect to Redis: {str(e)}")
    print("   Make sure Redis is running: redis-server")
    exit(1)

print("\n" + "="*60)
print("REDIS RATE LIMITING VERIFICATION")
print("="*60)
print(f"Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")

# Check for rate limiting keys
print("Searching for rate limiting keys...\n")

keys = r.keys("rl:*")

if keys:
    print(f"Found {len(keys)} rate limiting keys:\n")
    
    for key in keys:
        ttl = r.ttl(key)
        value = r.get(key)
        
        print(f"Key: {key}")
        print(f"  Value (request count): {value}")
        print(f"  TTL (seconds remaining): {ttl}")
        print()
else:
    print("⚠️  No rate limiting keys found")
    print("   This is normal if no requests have been made since server restart")
    print("\n   To create keys, make some API requests:")
    print("   - Send telemetry: POST /api/v1/telemetry/")
    print("   - Access dashboard: GET /api/v1/dashboard/stats/")
    print()

# Show Redis info
print("="*60)
print("REDIS DATABASE INFO")
print("="*60)

info = r.info('keyspace')
print(f"Database 1 (Rate Limiting): {info.get('db1', 'No keys yet')}")

# Check if Django cache is configured
try:
    from django.core.cache import cache
    cache.set('test_key', 'test_value', 10)
    result = cache.get('test_key')
    if result == 'test_value':
        print("\n✅ Django cache (Redis) is working correctly")
        cache.delete('test_key')
    else:
        print("\n⚠️  Django cache test failed")
except Exception as e:
    print(f"\n❌ Django cache error: {str(e)}")
    print("   Make sure you're running this from Django shell:")
    print("   python manage.py shell < test_redis_verification.py")

print("\n" + "="*60)
print("MONITORING TIP")
print("="*60)
print("\nTo monitor rate limiting in real-time, run:")
print("  redis-cli -n 1")
print("  > MONITOR")
print("\nOr check keys periodically:")
print("  redis-cli -n 1 KEYS 'rl:*'")
print("="*60)
