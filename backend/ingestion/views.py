from rest_framework.decorators import api_view, permission_classes
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from .serializers import TelemetrySerializer
from .tasks import telemetry_ingest
from .ratelimit_utils import ratelimit_with_logging
from django.conf import settings
import logging
from datetime import datetime
logger = logging.getLogger(__name__)


@api_view(['POST'])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='header:HTTP_X_AGENT_TOKEN', rate=settings.RATELIMIT_TELEMETRY, method='POST', group='telemetry_sustained')  # Sustained limit
@ratelimit_with_logging(key='header:HTTP_X_AGENT_TOKEN', rate='200/10s', method='POST', group='telemetry_burst')  # Burst protection
def telemetry_endpoint(request):
    """
    Ingest telemetry events. Supports both single event (dict) and batch events (list).
    """
    # Determine if this is a batch or single event
    is_batch = isinstance(request.data, list)
    
    # Initialize serializer with many=True if it's a list
    serializer = TelemetrySerializer(data=request.data, many=is_batch)
    
    if serializer.is_valid():
        validated_data = serializer.validated_data
        
        # If it's a single event, wrap it in a list for uniform processing
        events_to_process = validated_data if is_batch else [validated_data]
        
        tasks_queued = 0
        failed_tasks = 0
        
        print(f"\n[Ingestion] Processing {len(events_to_process)} event(s)")
        
        for event in events_to_process:
            try:
                # Queue the task in Celery
                task = telemetry_ingest.delay(event)
                tasks_queued += 1
                # print(f"  -> Queued event {event.get('event_id')} (Task: {task.id})")
            except Exception as e:
                failed_tasks += 1
                logger.error(f"Failed to queue event {event.get('event_id')}: {str(e)}")
        
        response_data = {
            'status': 'accepted',
            'message': f'Queued {tasks_queued} events for processing',
            'batch_size': len(events_to_process),
            'failed': failed_tasks
        }
        
        # For single event backward compatibility, include event_id
        if not is_batch and events_to_process:
            response_data['event_id'] = events_to_process[0].get('event_id')
            
        return Response(response_data, status=status.HTTP_201_CREATED)
        
    else:
        logger.error(f"Validation error: {serializer.errors}")
        return Response({
            'status': 'error',
            'message': 'Validation failed',
            'errors': serializer.errors
        }, status=status.HTTP_400_BAD_REQUEST)
