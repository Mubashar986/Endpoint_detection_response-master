from mongoengine import connect
from ingestion.models import TelemetryEvent
import json

# Connect to MongoDB
connect(db='edr_telemetry', host='localhost', port=27017)

# Query for recent Python process events
python_events = TelemetryEvent.objects(
    event_type='process',
    raw_data__process__command_line__icontains='python'
).order_by('-created_at').limit(5)

print(f"Found {python_events.count()} Python process events")
print("=" * 80)

for event in python_events:
    proc = event.raw_data.get('process', {})
    print(f"\nEvent ID: {event.event_id}")
    print(f"Created: {event.created_at}")
    print(f"Command Line: {proc.get('command_line', 'N/A')}")
    print(f"PID: {proc.get('pid', 'N/A')}")
    print("-" * 80)
