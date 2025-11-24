import os
import django
import sys
from collections import Counter

# Setup Django environment
sys.path.append(r'c:\Endpoint_detection_response-master\backend')
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings')
django.setup()

from ingestion.models import TelemetryEvent

def check_quality():
    print("========================================")
    print("  EDR Data Quality Check")
    print("========================================")
    
    total_events = TelemetryEvent.objects.count()
    print(f"Total Events in DB: {total_events}")
    
    if total_events == 0:
        print("No events found.")
        return

    # 1. Check for Duplicates (based on Content)
    print("\n[1/2] Checking for Duplicates...")
    
    # We'll fetch just the fields we need to identify uniqueness
    events = TelemetryEvent.objects.all().only('event_id', 'agent_id', 'timestamp', 'event_type', 'raw_data')
    
    seen = set()
    duplicates = 0
    
    for e in events:
        # Create a unique signature based on CONTENT, not ID
        # We use timestamp + event_type + specific details
        # Note: timestamp is a datetime object
        ts_str = str(e.timestamp)
        
        details = ""
        if 'process' in e.raw_data:
            details = f"{e.raw_data['process'].get('pid')}_{e.raw_data['process'].get('name')}"
        elif 'network' in e.raw_data:
            details = f"{e.raw_data['network'].get('source_port')}_{e.raw_data['network'].get('dest_ip')}"
        elif 'file' in e.raw_data:
            details = f"{e.raw_data['file'].get('operation')}_{e.raw_data['file'].get('path')}"
            
        sig = f"{e.agent_id}_{e.event_type}_{ts_str}_{details}"
        
        if sig in seen:
            duplicates += 1
        else:
            seen.add(sig)
            
    print(f"  Unique Events: {len(seen)}")
    print(f"  Duplicate Events: {duplicates}")
    
    if duplicates == 0:
        print("  [OK] No duplicates found. Batching logic is clean.")
    else:
        print(f"  [WARN] Found {duplicates} duplicates! Batching might be re-sending buffers.")

    # 2. Alert Ratio
    print("\n[2/2] Alert Statistics...")
    
    # MongoDB aggregation is faster
    pipeline = [
        {"$group": {"_id": "$event_type", "count": {"$sum": 1}}}
    ]
    
    # TelemetryEvent is a MongoEngine Document, so we use .objects.aggregate
    results = list(TelemetryEvent.objects.aggregate(pipeline))
    
    print("  Event Distribution:")
    for res in results:
        print(f"    - {res['_id']}: {res['count']}")

if __name__ == "__main__":
    check_quality()
