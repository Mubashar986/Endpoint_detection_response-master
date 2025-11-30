# Check if alerts exist for RULE-TEST-001
from ingestion.detection_models import Alert
from datetime import datetime, timezone

print("=" * 100)
print("CHECKING ALERTS FOR RULE-TEST-001")
print("=" * 100)

# Find all alerts for this rule
alerts = Alert.objects(rule_id='RULE-TEST-001').order_by('-first_detected')

print(f"\nFound {alerts.count()} alerts for RULE-TEST-001\n")

if alerts.count() > 0:
    print("✅ ALERTS EXIST! The detection IS working, but they might not be visible in UI\n")
    
    for i, alert in enumerate(alerts, 1):
        print(f"[Alert {i}]")
        print(f"  Alert ID: {alert.alert_id}")
        print(f"  Status: {alert.alert_status}")
        print(f"  Severity: {alert.severity}")
        print(f"  First Detected: {alert.first_detected}")
        print(f"  Hostname: {alert.hostname}")
        print(f"  Evidence: {alert.evidence_summary}")
        print("-" * 100)
    
    print("\n🔍 DIAGNOSIS:")
    print("The rule IS working and creating alerts.")
    print("If you don't see them in the Dashboard, check:")
    print("1. Dashboard alert filters (status, severity)")
    print("2. Time range filters")
    print("3. Browser cache (hard refresh: Ctrl+Shift+R)")
    
else:
    print("❌ No alerts found, but rule stats show alert_count_7d=2")
    print("This is a data consistency issue.")

print("\n" + "=" * 100)
