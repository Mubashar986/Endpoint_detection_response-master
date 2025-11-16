from celery import shared_task
from .models import TelemetryEvent
from .rule_engine import DetectionEngine  # NEW: Import rule engine
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
        if event.event_type == 'process':
            cmd = event.raw_data.get('process', {}).get('command_line', '')
            if 'powershell' in cmd.lower():
                logger.warning(f"🔵 POWERSHELL DETECTED: {event.event_id}")
                logger.warning(f"   Command: {cmd}")
       
        
        logger.info(f"✅ Saved {event.event_type} event {event.event_id}")
        
        # Step 2: NEW - Run rule-based detection
        try:
            alerts = DetectionEngine.evaluate_event(event.event_id)
            
            if alerts:
                logger.warning(f"🚨 Rule-based detection: {len(alerts)} alerts created")
                for alert in alerts:
                    logger.warning(f"   - {alert.severity}: {alert.rule_name}")
            else:
                logger.debug(f"✅ No threats detected for event {event.event_id}")
        
        except Exception as detection_error:
            # Don't fail entire task if detection fails
            logger.error(f"❌ Detection error: {detection_error}")
            # Continue processing (event still saved)
        
        return {
            'status': 'success',
            'event_id': event.event_id,
            'event_type': event.event_type,
            'alerts_created': len(alerts) if 'alerts' in locals() else 0
        }
        
    except Exception as e:
        logger.error(f"❌ Failed to process telemetry: {str(e)}")
        raise  # Re-raise to trigger Celery retry
