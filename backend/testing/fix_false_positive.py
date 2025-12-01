# Quick fix: Add SearchProtocolHost.exe to RULE-FILE-001 exceptions
# Run this in Django shell: python manage.py shell < fix_false_positive.py

from ingestion.detection_models import DetectionRule

# Get the rule
rule = DetectionRule.objects.get(rule_id='RULE-FILE-001')

# Add exception
new_exception = {
    'field': 'process.command_line',
    'operator': 'contains',
    'value': 'SearchProtocolHost.exe',
    'reason': 'Windows Search indexing service - legitimate system process'
}

# Check if not already in exceptions
if new_exception not in rule.exceptions:
    rule.exceptions.append(new_exception)
    rule.save()
    print("✅ Added SearchProtocolHost.exe to exceptions")
else:
    print("Already in exceptions")

# Invalidate rule cache so it takes effect immediately
from ingestion.rule_engine import RuleCache
RuleCache.invalidate()

print(f"Rule {rule.rule_id} updated. False positives for SearchProtocolHost.exe will no longer trigger.")
