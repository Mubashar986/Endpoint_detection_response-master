"""
heartbeat_views.py - Agent Heartbeat API Endpoint
==================================================
Handles heartbeat POST requests from EDR agents.
Updates agent status, stores metrics, and can return update instructions.
"""

from rest_framework.decorators import api_view, permission_classes
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from django.utils import timezone
from datetime import timedelta
import logging

from .models_mongo import Agent
from .serializers import HeartbeatSerializer

logger = logging.getLogger(__name__)

# Agent version for update notifications
# In production, this would come from a configuration or database
LATEST_AGENT_VERSION = "1.0.0"


@api_view(['POST'])
@permission_classes([IsAuthenticated])
def heartbeat_endpoint(request):
    """
    Receive heartbeat from EDR agent.
    
    Request body:
    {
        "agent_id": "uuid",
        "agent_version": "1.0.0",
        "hostname": "DESKTOP-ABC123",
        "timestamp": "2024-01-01T00:00:00Z",
        "status": "running",
        "cpu_percent": 5.2,
        "memory_mb": 45,
        "uptime_seconds": 3600,
        "events_sent": 1234,
        "events_queued": 12,
        "ws_reconnect_failures": 0,
        "http_failures": 0
    }
    
    Response:
    {
        "status": "ok",
        "message": "Heartbeat received",
        "update_available": false,
        "latest_version": "1.0.0"
    }
    """
    serializer = HeartbeatSerializer(data=request.data)
    
    if not serializer.is_valid():
        logger.warning(f"Invalid heartbeat data: {serializer.errors}")
        return Response({
            'status': 'error',
            'message': 'Invalid heartbeat data',
            'errors': serializer.errors
        }, status=status.HTTP_400_BAD_REQUEST)
    
    data = serializer.validated_data
    agent_id = data['agent_id']
    
    try:
        # Find or create agent record
        agent = Agent.objects(agent_id=agent_id).first()
        
        if agent is None:
            # New agent - create record
            agent = Agent(
                agent_id=agent_id,
                hostname=data.get('hostname', 'unknown'),
                agent_version=data.get('agent_version', 'unknown'),
                first_seen=timezone.now()
            )
            logger.info(f"New agent registered: {agent_id[:8]}...")
        
        # Update agent record with heartbeat data
        agent.hostname = data.get('hostname', agent.hostname)
        agent.agent_version = data.get('agent_version', agent.agent_version)
        agent.last_seen = timezone.now()
        agent.is_online = True
        
        # Update metrics (if fields exist in model)
        if hasattr(agent, 'cpu_percent'):
            agent.cpu_percent = data.get('cpu_percent', 0)
        if hasattr(agent, 'memory_mb'):
            agent.memory_mb = data.get('memory_mb', 0)
        if hasattr(agent, 'uptime_seconds'):
            agent.uptime_seconds = data.get('uptime_seconds', 0)
        if hasattr(agent, 'events_sent'):
            agent.events_sent = data.get('events_sent', 0)
        if hasattr(agent, 'last_heartbeat'):
            agent.last_heartbeat = timezone.now()
        
        agent.save()
        
        # Check if update is available
        current_version = data.get('agent_version', '0.0.0')
        update_available = version_compare(current_version, LATEST_AGENT_VERSION) < 0
        
        response_data = {
            'status': 'ok',
            'message': 'Heartbeat received',
            'update_available': update_available,
            'latest_version': LATEST_AGENT_VERSION
        }
        
        # Add update info if update is available
        if update_available:
            response_data['update_url'] = f'/api/v1/agent/download/?version={LATEST_AGENT_VERSION}'
            response_data['update_checksum'] = ''  # Would be populated from update registry
            logger.info(f"Agent {agent_id[:8]}... needs update: {current_version} -> {LATEST_AGENT_VERSION}")
        
        logger.debug(f"Heartbeat from {agent_id[:8]}... (v{current_version})")
        return Response(response_data, status=status.HTTP_200_OK)
        
    except Exception as e:
        logger.error(f"Heartbeat processing error: {str(e)}")
        return Response({
            'status': 'error',
            'message': 'Internal server error'
        }, status=status.HTTP_500_INTERNAL_SERVER_ERROR)


def version_compare(v1: str, v2: str) -> int:
    """
    Compare two version strings.
    Returns: -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
    """
    try:
        parts1 = [int(x) for x in v1.split('.')]
        parts2 = [int(x) for x in v2.split('.')]
        
        # Pad shorter version with zeros
        while len(parts1) < len(parts2):
            parts1.append(0)
        while len(parts2) < len(parts1):
            parts2.append(0)
        
        for i in range(len(parts1)):
            if parts1[i] < parts2[i]:
                return -1
            elif parts1[i] > parts2[i]:
                return 1
        return 0
    except (ValueError, AttributeError):
        return 0
