from rest_framework.decorators import api_view, permission_classes, authentication_classes
from rest_framework.authentication import SessionAuthentication, TokenAuthentication
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from rest_framework import status
from django.utils import timezone
from datetime import timedelta, timezone as dt_timezone
import json

from .models_mongo import PendingCommand, ResponseAction
from .rbac_decorators import require_analyst_or_admin
from .ratelimit_utils import ratelimit_with_logging
from django.conf import settings

# ==========================================
# AGENT APIs (Polling & Reporting)
# ==========================================

@api_view(['GET'])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='header:HTTP_X_AGENT_TOKEN', rate='300/m', method='GET')
def poll_commands(request):
    """
    Agent polls this endpoint to get pending commands.
    """
    # Identify agent from the authenticated user/token
    # In a real system, the token is tied to a specific agent.
    # For this MVP, we'll assume the username IS the agent_id or passed as a header
    # But to be safe and simple, let's expect the agent to send its ID in a header
    agent_id = request.headers.get('X-Agent-ID')
    
    if not agent_id:
        return Response({'error': 'X-Agent-ID header required'}, status=status.HTTP_400_BAD_REQUEST)

    # Find the oldest 'new' command for this agent
    command = PendingCommand.objects(agent_id=agent_id, status='new').order_by('created_at').first()
    
    if command:
        # Check if expired
        expires_at = command.expires_at
        if timezone.is_naive(expires_at):
            expires_at = timezone.make_aware(expires_at, dt_timezone.utc)

        if expires_at < timezone.now():
            command.status = 'timeout'
            command.save()
            return Response(status=status.HTTP_204_NO_CONTENT)
            
        # Mark as in progress
        command.status = 'in_progress'
        command.save()
        
        return Response({
            'command_id': command.command_id,
            'type': command.command_type,
            'parameters': command.parameters
        })
    
    return Response(status=status.HTTP_204_NO_CONTENT)

@api_view(['POST'])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='header:HTTP_X_AGENT_TOKEN', rate='50/m', method='POST')
def report_command_result(request, command_id):
    """
    Agent reports the result of a command execution.
    """
    try:
        command = PendingCommand.objects.get(command_id=command_id)
    except PendingCommand.DoesNotExist:
        return Response({'error': 'Command not found'}, status=status.HTTP_404_NOT_FOUND)
        
    result_data = request.data
    
    # Update command status
    command.status = 'completed' if result_data.get('status') == 'success' else 'failed'
    command.result = result_data
    command.completed_at = timezone.now()
    command.save()
    
    # Update the Audit Log (ResponseAction)
    # We find the ResponseAction linked to this command
    action = ResponseAction.objects(command_id=command_id).first()
    if action:
        action.status = command.status
        action.result_summary = str(result_data.get('message', ''))
        action.save()
    
    # AUTO-RESOLVE ALERT if action succeeded
    if result_data.get('status') == 'success':
        # Get alert_id from command parameters (if provided)
        alert_id = command.parameters.get('alert_id')
        if alert_id:
            try:
                from .detection_models import Alert
                import logging
                logger = logging.getLogger(__name__)
                
                alert = Alert.objects.get(alert_id=alert_id)
                
                # Auto-resolve  the alert
                analyst = command.issued_by
                action_type = command.command_type.replace('_', ' ').title()
                note = f"Auto-resolved: {action_type} executed successfully. {result_data.get('message', '')}"
                
                alert.mark_resolved(analyst, note)
                logger.info(f"Auto-resolved alert {alert_id} after successful {command.command_type}")
            except Exception as e:
                # Don't fail if auto-resolve fails
                pass
        
    return Response({'status': 'received'})

# ==========================================
# DASHBOARD AP APIs (Triggers)
# ==========================================

@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@require_analyst_or_admin
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_RESPONSE_ACTION, method='POST')
def trigger_kill_process(request):
    """
    UI triggers a kill process action.
    Requires SOC Analyst role or higher.
    """
        
    agent_id = request.data.get('agent_id')
    pid = request.data.get('pid')
    alert_id = request.data.get('alert_id')  # Optional: for auto-resolve
    
    if not agent_id or not pid:
        return Response({'error': 'agent_id and pid required'}, status=status.HTTP_400_BAD_REQUEST)
        
    # Create the command
    command = PendingCommand(
        agent_id=agent_id,
        command_type='kill_process',
        parameters={'pid': int(pid), 'alert_id': alert_id},  # Store alert_id
        status='new',
        issued_by=request.user.email or request.user.username,
        expires_at=timezone.now() + timedelta(minutes=5)
    )
    command.save()
    
    # Create Audit Log
    ResponseAction(
        user=request.user.email or request.user.username,
        action_type='kill_process',
        target_agent=agent_id,
        command_id=command.command_id,
        reason=request.data.get('reason', 'Manual trigger from dashboard'),
        status='initiated'
    ).save()
    
    return Response({
        'status': 'queued', 
        'command_id': command.command_id,
        'message': 'Kill command queued successfully'
    })

@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@require_analyst_or_admin
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_ISOLATE, method='POST')
def trigger_isolate_host(request):
    """
    UI triggers a host isolation action.
    Requires SOC Analyst role or higher.
    """
        
    agent_id = request.data.get('agent_id')
    
    if not agent_id:
        return Response({'error': 'agent_id required'}, status=status.HTTP_400_BAD_REQUEST)
        
    # Create the command
    command = PendingCommand(
        agent_id=agent_id,
        command_type='isolate_host',
        parameters={},
        status='new',
        issued_by=request.user.email or request.user.username,
        expires_at=timezone.now() + timedelta(minutes=5)
    )
    command.save()
    
    # Create Audit Log
    ResponseAction(
        user=request.user.email or request.user.username,
        action_type='isolate_host',
        target_agent=agent_id,
        command_id=command.command_id,
        reason=request.data.get('reason', 'Manual trigger from dashboard'),
        status='initiated'
    ).save()
    
    return Response({
        'status': 'queued', 
        'command_id': command.command_id,
        'message': 'Isolation command queued successfully'
    })

@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@require_analyst_or_admin
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_ISOLATE, method='POST')
def trigger_deisolate_host(request):
    """
    UI triggers a host de-isolation action.
    Requires SOC Analyst role or higher.
    """
        
    agent_id = request.data.get('agent_id')
    
    if not agent_id:
        return Response({'error': 'agent_id required'}, status=status.HTTP_400_BAD_REQUEST)
        
    # Create the command
    command = PendingCommand(
        agent_id=agent_id,
        command_type='deisolate_host',
        parameters={},
        status='new',
        issued_by=request.user.email or request.user.username,
        expires_at=timezone.now() + timedelta(minutes=5)
    )
    command.save()
    
    # Create Audit Log
    ResponseAction(
        user=request.user.email or request.user.username,
        action_type='deisolate_host',
        target_agent=agent_id,
        command_id=command.command_id,
        reason=request.data.get('reason', 'Manual trigger from dashboard'),
        status='initiated'
    ).save()
    
    return Response({
        'status': 'queued', 
        'command_id': command.command_id,
        'message': 'De-isolation command queued successfully'
    })
