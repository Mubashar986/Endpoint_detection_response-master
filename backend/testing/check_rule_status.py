# Check if RULE-TEST-001 exists in database with correct settings
from ingestion.detection_models import DetectionRule

print("=" * 100)
print("CHECKING RULE-TEST-001 IN DATABASE")
print("=" * 100)

try:
    rule = DetectionRule.objects.get(rule_id='RULE-TEST-001')
    print(f"\n✅ Rule found in database!")
    print(f"Name: {rule.name}")
    print(f"Enabled: {rule.enabled}")
    print(f"Deployment Status: {rule.deployment_status}")
    print(f"Entity Type: {rule.detection_logic.get('entity_type')}")
    
    print(f"\nConditions:")
    for i, cond in enumerate(rule.detection_logic.get('conditions', []), 1):
        print(f"  {i}. Field: {cond.get('field')}")
        print(f"     Operator: {cond.get('operator')}")
        print(f"     Value: {cond.get('value')}")
        print(f"     Case Sensitive: {cond.get('case_sensitive', True)}")
    
    print(f"\nLogic: {rule.detection_logic.get('logic')}")
    
    # This is the KEY check
    if rule.deployment_status == 'PRODUCTION' and rule.enabled:
        print("\n✅ Rule is configured correctly (PRODUCTION + ENABLED)")
    else:
        print(f"\n❌ PROBLEM: deployment_status={rule.deployment_status}, enabled={rule.enabled}")
        print("   Rule will NOT be loaded by RuleCache!")
        
except DetectionRule.DoesNotExist:
    print("\n❌ RULE-TEST-001 NOT FOUND in database!")
    print("   You need to run: python manage.py seed_rules")

print("\n" + "=" * 100)
print("CHECKING RULE CACHE")
print("=" * 100)

from ingestion.rule_engine import RuleCache

# Force invalidate and reload
RuleCache.invalidate()
loaded_rules = RuleCache.get_rules(entity_type='process')

print(f"\nRuleCache has {len(loaded_rules)} process rules loaded")

# Check if our rule is in the cache
test_rule_in_cache = any(r.rule_id == 'RULE-TEST-001' for r in loaded_rules)

if test_rule_in_cache:
    print("✅ RULE-TEST-001 is loaded in cache!")
else:
    print("❌ RULE-TEST-001 is NOT in cache!")
    print("\nRules in cache:")
    for r in loaded_rules:
        print(f"  - {r.rule_id}: {r.name}")

print("\n" + "=" * 100)
