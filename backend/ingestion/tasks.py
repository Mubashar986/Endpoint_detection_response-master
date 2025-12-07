from celery import shared_task
from .models import TelemetryEvent
from .models_mongo import Agent  # Agent tracking
from .rule_engine import DetectionEngine
from datetime import datetime
import logging

logger = logging.getLogger(__name__)

@shared_task
def telemetry_ingest(data):
    """
    Celery worker task: Process and persist telemetry event.
    NOW RUNS RULE-BASED DETECTION.
    """
    
    try:
        # Step 1: Save event to MongoDB (existing code)
        event = TelemetryEvent(
            event_id=data['event_id'],
            agent_id=data['agent_id'],
            event_type=data['event_type'],
            timestamp=data['timestamp'],
            severity=data['severity'],
            raw_data=data
        )
        event.save()
        
        # Track agent metadata
        try:
            agent_id = data.get('agent_id')
            agent_version = data.get('agent_version', 'unknown')
            hostname = data.get('host', {}).get('hostname', 'unknown')
            
            # DETAILED TRACING: Log the raw agent_version from incoming data
            logger.warning(f"[TELEMETRY RECEIVED] agent_id={agent_id[:12]}... agent_version='{agent_version}' (from data)")
            logger.warning(f"[TELEMETRY RECEIVED] Full data keys: {list(data.keys())}")
            
            Agent.objects(agent_id=agent_id).update_one(
                set__hostname=hostname,
                set__agent_version=agent_version,
                set__last_seen=datetime.utcnow(),
                set__is_online=True,
                set_on_insert__first_seen=datetime.utcnow(),
                upsert=True
            )
            logger.warning(f"[AGENT UPDATED] Set agent_version='{agent_version}' for agent {agent_id[:12]}...")
        except Exception as agent_error:
            logger.error(f"Failed to update agent: {agent_error}")
        
        if event.event_type == 'process':
            cmd = event.raw_data.get('process', {}).get('command_line', '')
            if 'powershell' in cmd.lower():
                logger.warning(f" POWERSHELL DETECTED: {event.event_id}")
                logger.warning(f"   Command: {cmd}")
       
        
        logger.info(f" Saved {event.event_type} event {event.event_id}")
        
        # Step 2: NEW - Run rule-based detection
        try:
            alerts = DetectionEngine.evaluate_event(event.event_id)
            
            if alerts:
                logger.warning(f" Rule-based detection: {len(alerts)} alerts created")
                for alert in alerts:
                    logger.warning(f"   - {alert.severity}: {alert.rule_name}")
            else:
                logger.debug(f" No threats detected for event {event.event_id}")
        
        except Exception as detection_error:
            # Don't fail entire task if detection fails
            logger.error(f" Detection error: {detection_error}")
            # Continue processing (event still saved)
        
        return {
            'status': 'success',
            'event_id': event.event_id,
            'event_type': event.event_type,
            'alerts_created': len(alerts) if 'alerts' in locals() else 0
        }
        
    except Exception as e:
        logger.error(f" Failed to process telemetry: {str(e)}")
        raise  # Re-raise to trigger Celery retry
