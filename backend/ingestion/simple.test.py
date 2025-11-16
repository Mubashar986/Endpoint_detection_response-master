"""
Test rule against REAL event from your database.
"""

from ingestion.models import TelemetryEvent

# Load ONE real event from your database
event = TelemetryEvent.objects.filter(event_type='process').first()

if not event:
    print("❌ No events in database yet. Run your agent first!")
    exit()

print(f"✅ Loaded event: {event.event_id}")
print(f"   Process: {event.raw_data.get('process', {}).get('name')}")
print(f"   Command: {event.raw_data.get('process', {}).get('command_line')}")

# Same simple rule
simple_rule = {
    "name": "Detect PowerShell",
    "conditions": [
        {"field": "process.name", "value": "powershell.exe"}
    ]
}

# Check it
def check_rule(event_data, rule):
    for condition in rule["conditions"]:
        field = condition["field"]
        parts = field.split(".")
        value = event_data
        for part in parts:
            value = value.get(part, "")
        
        if "value" in condition:
            if value.lower() != condition["value"].lower():
                return False
    return True

# Test against real data
if check_rule(event.raw_data, simple_rule):
    print("🚨 ALERT: This event matches the rule!")
else:
    print("✅ This event is safe")
