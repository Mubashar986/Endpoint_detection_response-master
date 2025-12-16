from rest_framework import viewsets, status
from rest_framework.decorators import api_view, permission_classes
from rest_framework.permissions import IsAuthenticated
from rest_framework.response import Response
from ..models_mongo import AgentConfig, Agent
from ..auth import IsAgentAuthenticated
from datetime import datetime, timezone

# ==========================================
# AGENT-FACING ENDPOINTS
# ==========================================

@api_view(['GET'])
@permission_classes([IsAgentAuthenticated])
def agent_config_pull(request):
    """
    Allow an agent to download its assigned configuration.
    Endpoint: GET /api/v1/config/
    """
    try:
        agent = request.auth  # This is the Agent object from IsAgentAuthenticated
        
        # 1. Determine which config ID to use
        # If agent has a specific config assigned, use it. Otherwise 'default'.
        config_id = agent.config_id if agent.config_id else "default"
        
        try:
            config = AgentConfig.objects.get(config_id=config_id)
        except AgentConfig.DoesNotExist:
            # Fallback: validation should ensure 'default' always exists, but safety first
            try:
                config = AgentConfig.objects.get(config_id="default")
            except AgentConfig.DoesNotExist:
                # Emergency fallback if even default is missing
                # Try to find ANY config marked as default
                config = AgentConfig.objects.filter(is_default=True).first()
                if not config:
                     return Response({
                        "error": "No configuration available",
                        "code": "config_missing"
                    }, status=status.HTTP_404_NOT_FOUND)

        # 2. Return the configuration payload
        # We strip internal metadata and return clean JSON
        return Response({
            "config_id": config.config_id,
            "version": config.version,
            "config": config.config_json
        })
        
    except Exception as e:
        return Response({
            "error": str(e),
            "code": "internal_error"
        }, status=status.HTTP_500_INTERNAL_SERVER_ERROR)


# ==========================================
# ADMIN-FACING ENDPOINTS
# ==========================================

class ConfigViewSet(viewsets.ViewSet):
    """
    Admin API for managing agent configuration policies.
    Endpoint: /api/v1/configs/
    """
    permission_classes = [IsAuthenticated]  # Only logged-in users (admins/analysts)

    def list(self, request):
        """List all available configurations."""
        configs = AgentConfig.objects.all().order_by('-is_default', 'name')
        data = [{
            "config_id": c.config_id,
            "name": c.name,
            "version": c.version,
            "is_default": c.is_default
        } for c in configs]
        return Response(data)

    def retrieve(self, request, pk=None):
        """Get a single configuration details."""
        try:
            config = AgentConfig.objects.get(config_id=pk)
            return Response({
                "config_id": config.config_id,
                "name": config.name,
                "version": config.version,
                "is_default": config.is_default,
                "config_json": config.config_json,
                "created_by": config.created_by,
                "created_at": config.created_at
            })
        except AgentConfig.DoesNotExist:
            return Response(status=status.HTTP_404_NOT_FOUND)

    def create(self, request):
        """Create a new configuration policy."""
        try:
            # Extract fields
            name = request.data.get('name')
            config_json = request.data.get('config_json', {})
            is_default = request.data.get('is_default', False)
            config_id = request.data.get('config_id') # Allow manual ID if needed
            
            # Create object
            config = AgentConfig(
                name=name,
                config_json=config_json,
                is_default=is_default,
                created_by=request.user.username
            )
            if config_id:
                config.config_id = config_id
                
            config.save()
            
            return Response({
                "message": "Configuration created successfully",
                "config_id": config.config_id
            }, status=status.HTTP_201_CREATED)
            
        except Exception as e:
            return Response({"error": str(e)}, status=status.HTTP_400_BAD_REQUEST)

    def update(self, request, pk=None):
        """Update an existing policy (Increments version)."""
        try:
            config = AgentConfig.objects.get(config_id=pk)
            
            # Update fields if present
            if 'name' in request.data:
                config.name = request.data['name']
            
            if 'config_json' in request.data:
                config.config_json = request.data['config_json']
                # Increment version on config change
                config.version += 1
                config.updated_at = datetime.now(timezone.utc)
            
            if 'is_default' in request.data:
                config.is_default = request.data['is_default']
            
            config.save()
            
            return Response({
                "message": "Configuration updated",
                "version": config.version
            })
            
        except AgentConfig.DoesNotExist:
            return Response(status=status.HTTP_404_NOT_FOUND)
        except Exception as e:
            return Response({"error": str(e)}, status=status.HTTP_400_BAD_REQUEST)

    def destroy(self, request, pk=None):
        """Delete a configuration (Prevent if default)."""
        try:
            config = AgentConfig.objects.get(config_id=pk)
            if config.is_default:
                return Response({"error": "Cannot delete default configuration"}, status=status.HTTP_400_BAD_REQUEST)
            
            config.delete()
            return Response(status=status.HTTP_204_NO_CONTENT)
            
        except AgentConfig.DoesNotExist:
            return Response(status=status.HTTP_404_NOT_FOUND)
