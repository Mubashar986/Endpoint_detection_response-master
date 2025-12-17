from rest_framework.decorators import api_view, permission_classes, authentication_classes
from rest_framework.permissions import IsAuthenticated
from rest_framework.authentication import SessionAuthentication
from rest_framework.response import Response
from ..models_mongo import Agent, AgentConfig
from datetime import datetime
import secrets

# ==========================================
# AGENT DETAIL & CONFIG API
# ==========================================

@api_view(['GET', 'PUT'])
@authentication_classes([SessionAuthentication])
@permission_classes([IsAuthenticated])
def agent_detail(request, agent_id):
    """
    GET: Retrieve agent details, status, health metrics, and current configuration.
    PUT: Update agent settings (e.g., assign a different config policy).
    """
    try:
        agent = Agent.objects.get(agent_id=agent_id)
    except Agent.DoesNotExist:
        return Response({"error": "Agent not found"}, status=404)
    
    # GET: Return full details including health stats and config
    if request.method == 'GET':
        # Fetch assigned config
        config_data = None
        current_config = None
        if agent.config_id:
            current_config = AgentConfig.objects.filter(config_id=agent.config_id).first()
        
        # Fallback to default if assigned config missing
        if not current_config:
            current_config = AgentConfig.objects.filter(is_default=True).first()

        if current_config:
            config_data = {
                "config_id": current_config.config_id,
                "name": current_config.name,
                "version": current_config.version,
                "modules": current_config.config_json.get("modules", {}),
                "communication": current_config.config_json.get("communication", {})
            }

        return Response({
            "agent_id": agent.agent_id,
            "hostname": agent.hostname,
            "os_version": agent.os_version,
            "agent_version": agent.agent_version,
            "status": "online" if agent.is_online else "offline",
            "last_seen": agent.last_seen,
            "first_seen": agent.first_seen,
            "identity_token_snippet": agent.identity_token[:10] + "..." if agent.identity_token else None,
            
            # Health Metrics (from Heartbeat)
            "health": {
                "cpu_percent": agent.cpu_percent,
                "memory_mb": agent.memory_mb,
                "uptime_seconds": agent.uptime_seconds,
                "events_sent": agent.events_sent,
                "last_heartbeat": agent.last_heartbeat
            },
            
            # Configuration
            "config_assigned": config_data,
            "config_sync_status": agent.config_status  # SYNCED/PENDING
        })
    
    # PUT: Update agent (specifically config assignment)
    elif request.method == 'PUT':
        new_config_id = request.data.get('config_id')
        if new_config_id:
            # Verify config exists
            if AgentConfig.objects.filter(config_id=new_config_id).count() == 0:
                return Response({"error": "Configuration ID not found"}, status=400)
            
            agent.config_id = new_config_id
            agent.config_status = "PENDING"  # Mark as pending until next heartbeat confirms
            agent.save()
            return Response({"status": "updated", "message": f"Assigned policy {new_config_id}"})
        
        return Response({"status": "no_change", "message": "No valid fields to update"})


# ==========================================
# TOKEN MANAGEMENT API
# ==========================================

@api_view(['POST'])
@authentication_classes([SessionAuthentication])
@permission_classes([IsAuthenticated])
def revoke_agent_token(request, agent_id):
    """
    Revoke an agent's identity token.
    The agent will be effectively kicked off and failing auth until re-enrollment logic triggers (if implemented)
    or manual re-installation.
    """
    try:
        agent = Agent.objects.get(agent_id=agent_id)
        agent.identity_token = None
        agent.is_online = False
        agent.save()
        return Response({"status": "success", "message": "Token revoked. Agent is now unauthorized."})
    except Agent.DoesNotExist:
        return Response({"error": "Agent not found"}, status=404)


@api_view(['POST'])
@authentication_classes([SessionAuthentication])
@permission_classes([IsAuthenticated])
def rotate_agent_token(request, agent_id):
    """
    Regenerate the identity token. 
    NOTE: This breaks the agent's connection until the new token is manually placed on the agent
    OR the agent has a re-enrollment capability (not yet implemented).
    """
    try:
        agent = Agent.objects.get(agent_id=agent_id)
        new_token = secrets.token_hex(32)
        agent.identity_token = new_token
        agent.save()
        
        return Response({
            "status": "success", 
            "message": "Token rotated successfully.",
            "new_token": new_token  # Returned once for admin to copy
        })
    except Agent.DoesNotExist:
        return Response({"error": "Agent not found"}, status=404)
