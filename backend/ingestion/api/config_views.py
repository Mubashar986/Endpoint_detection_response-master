"""
Agent Configuration API Views.

Endpoints for agents to download their configuration and for admins to manage configs.
"""
from rest_framework.views import APIView
from rest_framework.response import Response
from rest_framework import status
from rest_framework.permissions import IsAuthenticated, IsAdminUser, AllowAny
from datetime import datetime

from ..models_mongo import Agent, AgentConfig
from ..auth import AgentTokenAuthentication, IsAgentAuthenticated


class AgentConfigView(APIView):
    """
    GET /api/v1/config/ - Agent downloads its assigned configuration
    
    Uses AgentToken authentication - agent gets its assigned config.
    """
    permission_classes = [IsAgentAuthenticated]
    
    def get(self, request):
        """Agent downloads its configuration."""
        agent = request.auth  # Set by AgentTokenAuthentication
        
        if not agent:
            return Response(
                {'error': 'Agent authentication required'},
                status=status.HTTP_401_UNAUTHORIZED
            )
        
        # Get agent's assigned config
        if agent.config_id:
            try:
                config = AgentConfig.objects.get(config_id=agent.config_id)
                config_data = config.config_json
                config_version = config.version
                config_name = config.name
            except AgentConfig.DoesNotExist:
                # Fallback to default config
                config_data = self._get_default_config()
                config_version = 1
                config_name = "Default (fallback)"
        else:
            # No config assigned - use default
            config_data = self._get_default_config()
            config_version = 1
            config_name = "Default"
        
        # Update agent's config status
        agent.config_status = 'SYNCED'
        agent.config_version = config_version
        agent.save()
        
        return Response({
            'config_name': config_name,
            'config_version': config_version,
            'config': config_data,
            'updated_at': datetime.utcnow().isoformat()
        })
    
    def _get_default_config(self):
        """Return default configuration for agents."""
        return {
            "modules": {
                "file_monitor": {"enabled": True},
                "process_monitor": {"enabled": True},
                "network_monitor": {"enabled": False}
            },
            "telemetry": {
                "batch_size": 100,
                "heartbeat_interval": 30
            },
            "exclusions": {
                "paths": [],
                "processes": []
            }
        }


class ConfigListCreateView(APIView):
    """
    GET /api/v1/configs/ - List all configurations (Admin)
    POST /api/v1/configs/ - Create new configuration (Admin)
    """
    permission_classes = [IsAuthenticated, IsAdminUser]
    
    def get(self, request):
        """List all agent configurations."""
        configs = AgentConfig.objects.order_by('-created_at')
        
        config_list = []
        for config in configs:
            config_list.append({
                'id': config.config_id,
                'name': config.name,
                'version': config.version,
                'config_json': config.config_json,
                'created_by': config.created_by,
                'created_at': config.created_at.isoformat() if config.created_at else None,
            })
        
        return Response({
            'count': len(config_list),
            'configs': config_list
        })
    
    def post(self, request):
        """Create new agent configuration."""
        name = request.data.get('name', 'New Policy')
        config_json = request.data.get('config_json', {})
        
        # Get next version for this name
        existing = AgentConfig.objects(name=name).order_by('-version').first()
        next_version = (existing.version + 1) if existing else 1
        
        config = AgentConfig(
            name=name,
            version=next_version,
            config_json=config_json,
            created_by=request.user.username
        )
        config.save()
        
        return Response({
            'success': True,
            'config_id': config.config_id,
            'name': config.name,
            'version': config.version
        }, status=status.HTTP_201_CREATED)


class ConfigAssignView(APIView):
    """
    POST /api/v1/configs/<config_id>/assign/ - Assign config to agents (Admin)
    """
    permission_classes = [IsAuthenticated, IsAdminUser]
    
    def post(self, request, config_id):
        """Assign a configuration to one or more agents."""
        agent_ids = request.data.get('agent_ids', [])
        
        if not agent_ids:
            return Response(
                {'error': 'agent_ids list is required'},
                status=status.HTTP_400_BAD_REQUEST
            )
        
        # Verify config exists
        try:
            config = AgentConfig.objects.get(config_id=config_id)
        except AgentConfig.DoesNotExist:
            return Response(
                {'error': 'Configuration not found'},
                status=status.HTTP_404_NOT_FOUND
            )
        
        # Update agents
        updated_count = 0
        for agent_id in agent_ids:
            try:
                agent = Agent.objects.get(agent_id=agent_id)
                agent.config_id = config_id
                agent.config_status = 'PENDING'
                agent.save()
                updated_count += 1
            except Agent.DoesNotExist:
                continue
        
        return Response({
            'success': True,
            'updated_agents': updated_count,
            'config_name': config.name,
            'config_version': config.version
        })
