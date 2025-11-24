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

# ==========================================
# AGENT APIs (Polling & Reporting)
# ==========================================

@api_view(['GET'])
@permission_classes([IsAuthenticated])
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
        
    return Response({'status': 'received'})

# ==========================================
# DASHBOARD APIs (Triggers)
# ==========================================

@require_analyst_or_admin
@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
def trigger_kill_process(request):
    """
    UI triggers a kill process action.
    Requires SOC Analyst role or higher.
    """
        
    agent_id = request.data.get('agent_id')
    pid = request.data.get('pid')
    
    if not agent_id or not pid:
        return Response({'error': 'agent_id and pid required'}, status=status.HTTP_400_BAD_REQUEST)
        
    # Create the command
    command = PendingCommand(
        agent_id=agent_id,
        command_type='kill_process',
        parameters={'pid': int(pid)},
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

@require_analyst_or_admin
@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
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

@require_analyst_or_admin
@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
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
