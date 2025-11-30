"""
Quick Celery & Task Queue Diagnostic
Checks if tasks are being queued and processed
"""

print("="*70)
print("CELERY TASK QUEUE DIAGNOSTIC")
print("="*70)

# Check Django settings
from django.conf import settings
print("\n1. Django Settings:")
print(f"   CELERY_BROKER_URL: {settings.CELERY_BROKER_URL}")
print(f"   CELERY_RESULT_BACKEND: {settings.CELERY_RESULT_BACKEND}")

# Check if Celery app is configured
try:
    from edr_server.celery import app as celery_app
    print(f"\n2. Celery App: ✅ Configured")
    print(f"   Broker: {celery_app.conf.broker_url}")
except Exception as e:
    print(f"\n2. Celery App: ❌ Error - {e}")

# Check available tasks
try:
    from ingestion.tasks import telemetry_ingest
    print(f"\n3. Telemetry Task: ✅ Registered")
    print(f"   Task name: {telemetry_ingest.name}")
except Exception as e:
    print(f"\n3. Telemetry Task: ❌ Error - {e}")

# Try to inspect Celery workers
try:
    from celery import current_app
    i = current_app.control.inspect()
    
    print(f"\n4. Celery Workers:")
    stats = i.stats()
    if stats:
        for worker, info in stats.items():
            print(f"   Worker: {worker}")
            print(f"   Status: Running")
    else:
        print("   ⚠️  No workers detected")
        print("   Make sure Celery worker is running:")
        print("   celery -A edr_server worker --pool=solo")
    
    # Check active tasks
    active = i.active()
    if active:
        print(f"\n5. Active Tasks: {sum(len(tasks) for tasks in active.values())}")
    else:
        print(f"\n5. Active Tasks: 0")
    
    # Check reserved tasks
    reserved = i.reserved()
    if reserved:
        print(f"6. Reserved Tasks: {sum(len(tasks) for tasks in reserved.values())}")
    else:
        print(f"6. Reserved Tasks: 0")
        
except Exception as e:
    print(f"\n4. Worker Inspection: ❌ Error - {e}")
    print("   Is Celery worker running?")

# Test sending a simple task
print(f"\n7. Test Task Submission:")
try:
    test_event = {
        'agent_id': 'DIAGNOSTIC-TEST',
        'event_id': 'diag-001',
        'event_type': 'file',
        'timestamp': 1700000000,
        'severity': 'INFO',
        'version': '1.0',
        'host': {'hostname': 'test', 'os': 'Windows'},
        'file': {'path': 'C:\\test.txt', 'operation': 'write'}
    }
    
    result = telemetry_ingest.delay(test_event)
    print(f"   ✅ Task queued successfully")
    print(f"   Task ID: {result.id}")
    print(f"   Task State: {result.state}")
    
    # Wait a bit for processing
    import time
    time.sleep(2)
    
    print(f"   Task State after 2s: {result.state}")
    if result.ready():
        print(f"   Task Result: {result.result}")
    else:
        print(f"   Task still processing...")
        
except Exception as e:
    print(f"   ❌ Error queuing task: {e}")

print("\n" + "="*70)
print("DIAGNOSTIC COMPLETE")
print("="*70)
