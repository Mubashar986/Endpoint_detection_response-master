"""
rule_engine.py
Rule evaluation engine for threat detection.
"""
import re
import logging
from typing import Dict, Any, List, Optional
from datetime import datetime, timezone, timedelta
import time
from .models import TelemetryEvent
from .detection_models import DetectionRule
from .detection_models import Alert  # Reuse existing Alert model

logger = logging.getLogger(__name__)
class RuleCache:
    """
    In-memory cache for detection rules.
    because fetching the detection rules from the takes time so it gives memake the system 
   """ 
    _rules_cache = None  # Dict of {entity_type: [rules]}
    _cache_timestamp = None
    _cache_ttl_seconds = 300  # 5 minutes
    
    @classmethod
    def get_rules(cls, entity_type: str = None) -> List[DetectionRule]:
        """
        Get active rules from cache.
        """
        # Check if cache expired
        if cls._cache_expired():
            cls._load_rules()
        
        # Return all rules or filtered by entity type
        if entity_type:
            return cls._rules_cache.get(entity_type, [])
        else:
            # Flatten all rules
            all_rules = []
            for rules in cls._rules_cache.values():
                all_rules.extend(rules)
            return all_rules
    
    @classmethod
    def _cache_expired(cls) -> bool:
        """Check if cache needs refresh."""
        if cls._rules_cache is None:
            return True
        
        if cls._cache_timestamp is None:
            return True
        
        age = (datetime.now(timezone.utc) - cls._cache_timestamp).total_seconds()
        return age > cls._cache_ttl_seconds
    
    @classmethod
    def _load_rules(cls):
        """Load rules from MongoDB into cache."""
        logger.info("Loading detection rules into cache...")
        start_time = time.time()
        
        # Query all enabled rules
        rules = DetectionRule.objects.filter(
            enabled=True,
            deployment_status="PRODUCTION"
        )
        
        # Organize by entity type for fast filtering
        cls._rules_cache = {
            "process": [],
            "file": [],
            "network": [],
        }
        
        for rule in rules:
            entity_type = rule.detection_logic.get('entity_type')
            if entity_type in cls._rules_cache:
                cls._rules_cache[entity_type].append(rule)
            else:
                logger.warning(f"Rule {rule.rule_id} has unknown entity_type: {entity_type}")
        
        cls._cache_timestamp = datetime.now(timezone.utc)
        
        elapsed = time.time() - start_time
        total_rules = sum(len(rules) for rules in cls._rules_cache.values())
        
        logger.info(f"✅ Loaded {total_rules} rules in {elapsed:.2f}s")
        logger.info(f"   Process: {len(cls._rules_cache['process'])}")
        logger.info(f"   File: {len(cls._rules_cache['file'])}")
        logger.info(f"   Network: {len(cls._rules_cache['network'])}")
    
    @classmethod
    def invalidate(cls):
        """Force cache refresh (called when rules modified)."""
        cls._rules_cache = None
        cls._cache_timestamp = None


class ConditionEvaluator:
   
    @staticmethod
    def evaluate(event: TelemetryEvent, condition: Dict[str, Any]) -> bool:
        
        # Extract condition parameters
        field_path = condition.get('field')
        operator = condition.get('operator')
        value = condition.get('value')
        values = condition.get('values', [])
        case_sensitive = condition.get('case_sensitive', True)
        
        # Extract field value from event
        field_value = ConditionEvaluator._extract_field(event, field_path)
        
        
        # ========== ADD THIS DEBUG LOGGING ==========
        logger.debug(f"  Checking condition:")
        logger.debug(f"    Field: {field_path}")
        logger.debug(f"    Operator: {operator}")
        logger.debug(f"    Expected: {value or values}")
        logger.debug(f"    Actual: {field_value}")
    # ======
        
        if field_value is None:
            # Field doesn't exist in event
            return False
        
        # Convert to string for string operations
        field_str = str(field_value)
        
        # Apply operator
        try:
            if operator == 'equals':
                return ConditionEvaluator._op_equals(field_str, value, case_sensitive)
            
            elif operator == 'not_equals':
                return not ConditionEvaluator._op_equals(field_str, value, case_sensitive)
            
            elif operator == 'contains':
                return ConditionEvaluator._op_contains(field_str, value, case_sensitive)
            
            elif operator == 'not_contains':
                return not ConditionEvaluator._op_contains(field_str, value, case_sensitive)
            
            elif operator == 'contains_any':
                return ConditionEvaluator._op_contains_any(field_str, values, case_sensitive)
            
            elif operator == 'contains_all':
                return ConditionEvaluator._op_contains_all(field_str, values, case_sensitive)
            
            elif operator == 'starts_with':
                return ConditionEvaluator._op_starts_with(field_str, value, case_sensitive)
            
            elif operator == 'ends_with':
                return ConditionEvaluator._op_ends_with(field_str, value, case_sensitive)
            
            elif operator == 'regex':
                return ConditionEvaluator._op_regex(field_str, value, case_sensitive)
            
            elif operator == 'greater_than':
                return ConditionEvaluator._op_greater_than(field_value, value)
            
            elif operator == 'less_than':
                return ConditionEvaluator._op_less_than(field_value, value)
            
            elif operator == 'in_list':
                return ConditionEvaluator._op_in_list(field_str, values, case_sensitive)
            
            else:
                logger.error(f"Unknown operator: {operator}")
                return False
        
        except Exception as e:
            logger.error(f"Error evaluating condition: {e}")
            logger.error(f"  Field: {field_path}, Operator: {operator}")
            return False
    
    # ========== OPERATOR IMPLEMENTATIONS ==========
    
    @staticmethod
    def _op_equals(field: str, value: Any, case_sensitive: bool) -> bool:
        """Exact string match."""
        if case_sensitive:
            return field == str(value)
        else:
            return field.lower() == str(value).lower()
    
    @staticmethod
    def _op_contains(field: str, value: str, case_sensitive: bool) -> bool:
        """Substring match."""
        if case_sensitive:
            return value in field
        else:
            return value.lower() in field.lower()
    
    @staticmethod
    def _op_contains_any(field: str, values: List[str], case_sensitive: bool) -> bool:
        """Match if ANY value found in field."""
        for val in values:
            if ConditionEvaluator._op_contains(field, val, case_sensitive):
                return True  # Found one, stop checking
        return False
    
    @staticmethod
    def _op_contains_all(field: str, values: List[str], case_sensitive: bool) -> bool:
        """Match only if ALL values found in field."""
        for val in values:
            if not ConditionEvaluator._op_contains(field, val, case_sensitive):
                return False  # One missing, fail immediately
        return True  # All found
    
    @staticmethod
    def _op_starts_with(field: str, value: str, case_sensitive: bool) -> bool:
        """String starts with prefix."""
        if case_sensitive:
            return field.startswith(value)
        else:
            return field.lower().startswith(value.lower())
    
    @staticmethod
    def _op_ends_with(field: str, value: str, case_sensitive: bool) -> bool:
        """String ends with suffix."""
        if case_sensitive:
            return field.endswith(value)
        else:
            return field.lower().endswith(value.lower())
    
    @staticmethod
    def _op_regex(field: str, pattern: str, case_sensitive: bool) -> bool:
        """Regular expression match."""
        flags = 0 if case_sensitive else re.IGNORECASE
        try:
            return bool(re.search(pattern, field, flags))
        except re.error as e:
            logger.error(f"Invalid regex pattern: {pattern}, Error: {e}")
            return False
    
    @staticmethod
    def _op_greater_than(field: Any, value: Any) -> bool:
        """Numeric comparison: field > value."""
        try:
            return float(field) > float(value)
        except (ValueError, TypeError):
            return False
    
    @staticmethod
    def _op_less_than(field: Any, value: Any) -> bool:
        """Numeric comparison: field < value."""
        try:
            return float(field) < float(value)
        except (ValueError, TypeError):
            return False
    
    @staticmethod
    def _op_in_list(field: str, values: List[str], case_sensitive: bool) -> bool:
        """Check if field value is in list."""
        if case_sensitive:
            return field in values
        else:
            field_lower = field.lower()
            values_lower = [v.lower() for v in values]
            return field_lower in values_lower
    
    # ========== FIELD EXTRACTION ==========
    
    @staticmethod
    def _extract_field(event: TelemetryEvent, field_path: str) -> Optional[Any]:
        """
        Extract nested field value using dot notation.
        
        Examples:
          "process.name" → event.raw_data["process"]["name"]
          "network.dest_port" → event.raw_data["network"]["dest_port"]
          "agent_id" → event.agent_id
        
        Args:
            event: TelemetryEvent object
            field_path: Dot-separated path (e.g., "process.command_line")
        
        Returns:
            Field value or None if not found
        """
        parts = field_path.split('.')
        
        # Start with raw_data for nested lookups
        obj = event.raw_data
        
        # Navigate nested structure
        for part in parts:
            if isinstance(obj, dict):
                obj = obj.get(part)
            else:
                # Fallback to event attributes
                obj = getattr(event, part, None)
            
            if obj is None:
                return None
        
        return obj


class RuleEvaluator:
    """
    Main rule evaluation orchestrator.
    
    Responsibilities:
      - Coordinate condition evaluation
      - Apply AND/OR logic
      - Check exceptions
      - Generate alert evidence
    """
    
    def __init__(self, rule: DetectionRule, event: TelemetryEvent):
        """
        Initialize evaluator with rule and event.
        
        Args:
            rule: DetectionRule to evaluate
            event: TelemetryEvent to check against
        """
        self.rule = rule
        self.event = event
        self.matched_conditions = []  # Track which conditions matched (for evidence)
    
    def evaluate(self) -> bool:
        """
        Main evaluation method.
        
        Returns:
            True if rule matches event, False otherwise
        """
        start_time = time.time()
        
        # STEP 1: Entity type check (fast pre-filter)
        rule_entity_type = self.rule.detection_logic.get('entity_type')
        if rule_entity_type and rule_entity_type != self.event.event_type:
            return False  # Wrong event type, skip
        
        # STEP 2: Evaluate all conditions
        conditions = self.rule.detection_logic.get('conditions', [])
        logic_operator = self.rule.detection_logic.get('logic', 'AND')
        
        condition_results = []
        
        for condition in conditions:
            result = ConditionEvaluator.evaluate(self.event, condition)
            condition_results.append(result)
            
            if result:
                self.matched_conditions.append(condition)
            
            # OPTIMIZATION: Early termination for AND logic
            if logic_operator == 'AND' and not result:
                # One condition failed, no point checking rest
                logger.debug(f"Rule {self.rule.rule_id}: AND logic failed at condition")
                return False
        
        # STEP 3: Apply logic operator
        if logic_operator == 'AND':
            all_match = all(condition_results)
            if not all_match:
                return False
        elif logic_operator == 'OR':
            any_match = any(condition_results)
            if not any_match:
                return False
        else:
            logger.warning(f"Unknown logic operator: {logic_operator}")
            return False
        
        # STEP 4: Check exceptions (false positive filters)
        if self._check_exceptions():
            logger.info(f"Rule {self.rule.rule_id}: Alert suppressed by exception")
            return False
        
        # STEP 5: All checks passed
        elapsed = time.time() - start_time
        logger.info(f"✅ Rule {self.rule.rule_id} matched in {elapsed*1000:.2f}ms")
        
        return True
    
    def _check_exceptions(self) -> bool:
        """
        Check if event matches any exception filters.
        
        Returns:
            True if event should be EXCLUDED (exception matched)
            False if no exceptions matched (alert should be created)
        """
        exceptions = self.rule.exceptions
        
        for exception in exceptions:
            if ConditionEvaluator.evaluate(self.event, exception):
                reason = exception.get('reason', 'No reason provided')
                logger.debug(f"Exception matched: {reason}")
                return True  # Matched exception, suppress alert
        
        return False  # No exceptions matched
    
    def get_evidence_summary(self) -> Dict[str, Any]:
        """
        Extract relevant evidence for alert.
        
        Returns:
            Dict with evidence fields
        """
        event_type = self.event.event_type
        raw = self.event.raw_data
        
        # Extract matched indicator values
        matched_indicators = []
        for condition in self.matched_conditions:
            if 'value' in condition:
                matched_indicators.append(condition['value'])
            elif 'values' in condition:
                matched_indicators.extend(condition['values'])
        
        # Type-specific evidence
        if event_type == 'process':
            proc = raw.get('process', {})
            return {
                'process_name': proc.get('name'),
                'command_line': proc.get('command_line'),
                'pid': proc.get('pid'),
                'parent_pid': proc.get('parent_pid'),
                'parent_name': proc.get('parent_name'),
                'user': proc.get('user'),
                'execution_path': proc.get('path'),
                'matched_indicators': matched_indicators
            }
        
        elif event_type == 'file':
            file_data = raw.get('file', {})
            return {
                'file_path': file_data.get('path'),
                'file_name': file_data.get('name'),
                'operation': file_data.get('operation'),
                'size_bytes': file_data.get('size'),
                'file_hash': file_data.get('hash'),
                'matched_indicators': matched_indicators
            }
        
        elif event_type == 'network':
            net = raw.get('network', {})
            return {
                'protocol': net.get('protocol'),
                'source_ip': net.get('source_ip'),
                'dest_ip': net.get('dest_ip'),
                'dest_port': net.get('dest_port'),
                'bytes_sent': net.get('bytes_sent'),
                'bytes_received': net.get('bytes_received'),
                'process_name': net.get('process_name'),
                'matched_indicators': matched_indicators
            }
        
        return {'matched_indicators': matched_indicators}


class DetectionEngine:
    """
    Main detection engine: orchestrates rule evaluation.
    
    Public API:
      - evaluate_event(event_id): Run all rules against one event
    """
    
    @staticmethod
    def evaluate_event(event_id: str) -> List[Alert]:
        """
        Evaluate single event against all active rules.
        
        Args:
            event_id: Event identifier
        
        Returns:
            List of Alert objects created (empty if no matches)
        """
        try:
            event = TelemetryEvent.objects.get(event_id=event_id)
        except TelemetryEvent.DoesNotExist:
            logger.error(f"Event {event_id} not found")
            return []
        
        # Load rules from cache (filtered by event type)
        rules = RuleCache.get_rules(entity_type=event.event_type)
        
        logger.info(f"Evaluating event {event_id} against {len(rules)} {event.event_type} rules")
        
        created_alerts = []
        
        for rule in rules:
            # Check if rule applies to this endpoint
            if not rule.is_active_for_endpoint(event.agent_id):
                continue
            
            # Evaluate rule
            evaluator = RuleEvaluator(rule, event)
            
            if evaluator.evaluate():
                # Rule matched, create alert
                alert = DetectionEngine._create_alert(rule, event, evaluator)
                created_alerts.append(alert)
        
        if created_alerts:
            logger.warning(f"🚨 {len(created_alerts)} alerts created for event {event_id}")
        
        return created_alerts
    
    @staticmethod
    def _create_alert(rule: DetectionRule, event: TelemetryEvent, evaluator: RuleEvaluator) -> Alert:
        """
        Create Alert document from rule match.
        
        Args:
            rule: Matched DetectionRule
            event: TelemetryEvent that triggered rule
            evaluator: RuleEvaluator (for evidence extraction)
        
        Returns:
            Alert object (saved to MongoDB)
        """
        import uuid
        
        # Generate unique alert ID
        alert_id = f"ALT-{datetime.now(timezone.utc).strftime('%Y%m%d')}-{uuid.uuid4().hex[:6].upper()}"
        
        # Create alert
        alert = Alert(
            alert_id=alert_id,
            rule_id=rule.rule_id,
            rule_name=rule.name,
            severity=rule.severity,
            confidence=rule.confidence,
            endpoint_id=event.agent_id,
            hostname=event.raw_data.get('host', {}).get('hostname', 'UNKNOWN'),
            mitre_tactics=rule.mitre_tactics,
            mitre_techniques=rule.mitre_techniques,
            matched_event_ids=[event.event_id],
            evidence_summary=evaluator.get_evidence_summary(),
            alert_status='UNRESOLVED',
            first_detected=datetime.now(timezone.utc)
        )
        
        alert.save()
        
        logger.info(f"🚨 Created alert: {alert_id} for rule {rule.rule_id}")
        
        # Update rule statistics (in-memory, bulk save later)
        rule.increment_alert_count()
        
        return alert
