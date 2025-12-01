# Django Shell Debug Script
# Run this with: python manage.py shell < check_python_events.py
# Or copy-paste into: python manage.py shell

from ingestion.models import TelemetryEvent
from datetime import datetime, timedelta

print("=" * 100)
print("CHECKING RECENT PROCESS EVENTS IN MONGODB")
print("=" * 100)

# Get all process events from last hour
one_hour_ago = datetime.utcnow() - timedelta(hours=1)
recent_events = TelemetryEvent.objects(
    event_type='process',
    created_at__gte=one_hour_ago
).order_by('-created_at').limit(20)

print(f"\nFound {recent_events.count()} process events in the last hour\n")

# Display each event
for i, event in enumerate(recent_events, 1):
    proc = event.raw_data.get('process', {})
    cmd_line = proc.get('command_line', 'N/A')
    proc_name = proc.get('name', 'N/A')
    pid = proc.get('pid', 'N/A')
    
    print(f"\n[Event {i}] {event.created_at}")
    print(f"  Process: {proc_name}")
    print(f"  PID: {pid}")
    print(f"  Command Line: {cmd_line}")
    
    # Highlight if it contains our target
    if 'test_malware_simulation' in cmd_line.lower():
        print("  >>> 🎯 THIS SHOULD TRIGGER RULE-TEST-001!")
    if 'python' in cmd_line.lower():
        print("  >>> Contains 'python'")
    
    print("-" * 100)

# Specifically search for test_malware_simulation
print("\n\n" + "=" * 100)
print("SEARCHING FOR 'test_malware_simulation' IN COMMAND LINE")
print("=" * 100)

matching_events = TelemetryEvent.objects(
    event_type='process',
    raw_data__process__command_line__icontains='test_malware_simulation'
)

print(f"\nFound {matching_events.count()} events matching 'test_malware_simulation'\n")

if matching_events.count() > 0:
    for event in matching_events:
        proc = event.raw_data.get('process', {})
        print(f"Event ID: {event.event_id}")
        print(f"Command: {proc.get('command_line')}")
        print(f"Created: {event.created_at}")
        print("-" * 100)
else:
    print("❌ NO EVENTS FOUND! This is why the alert isn't triggering.")
    print("\nPossible reasons:")
    print("1. Sysmon isn't capturing Python process creation")
    print("2. The command_line field doesn't include the script name")
    print("3. The EDR Agent isn't running or isn't sending events")

print("\n" + "=" * 100)
print("DEBUG COMPLETE")
print("=" * 100)
