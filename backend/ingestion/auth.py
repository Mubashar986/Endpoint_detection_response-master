"""
Custom Authentication Backend for EDR Agents.

Separates machine authentication (agents) from human authentication (dashboard users).
"""
from rest_framework.authentication import BaseAuthentication
from rest_framework.permissions import BasePermission
from rest_framework.exceptions import AuthenticationFailed
from ingestion.models_mongo import Agent
from datetime import datetime, timezone


class AgentTokenAuthentication(BaseAuthentication):
    """
    Custom authentication for EDR Agents.
    
    Expected header format:
        Authorization: AgentToken <64-character-hex-token>
    
    Usage in views:
        - request.auth = Agent object
        - request.user = None (agents aren't users)
    """
    keyword = "AgentToken"

    def authenticate(self, request):
        """
        Authenticate the request and return (user, auth) tuple.
        
        Returns:
            (None, Agent) if valid agent token
            None if not our auth type (lets other backends try)
        
        Raises:
            AuthenticationFailed if token invalid/revoked
        """
        auth_header = request.headers.get("Authorization", "")
        
        # Check if this is our auth type
        if not auth_header.startswith(f"{self.keyword} "):
            return None  # Not our auth type, pass to next authenticator
        
        # Extract token
        try:
            token = auth_header.split(' ', 1)[1]
        except IndexError:
            raise AuthenticationFailed("Invalid authorization header format")
        
        # Validate token length (64 hex chars expected)
        if len(token) != 64:
            raise AuthenticationFailed("Invalid token format")

        # Lookup agent using correct field name
        try:
            agent = Agent.objects.get(identity_token=token, is_active=True)
        except Agent.DoesNotExist:
            raise AuthenticationFailed('Invalid or revoked agent token')
        
        # Update last_seen
        agent.last_seen = datetime.now(timezone.utc)
        agent.save()
        
        # Return (user, auth) - user is None for machine auth
        return (None, agent)
    
    def authenticate_header(self, request):
        """
        Return string for WWW-Authenticate header on 401 response.
        """
        return self.keyword


class IsAgentAuthenticated(BasePermission):
    """
    Custom permission that allows access if request was authenticated
    via AgentTokenAuthentication (request.auth is an Agent object).
    
    Use this instead of IsAuthenticated for agent endpoints.
    """
    
    def has_permission(self, request, view):
        # Check if auth is an Agent object (from AgentTokenAuthentication)
        if request.auth is not None and isinstance(request.auth, Agent):
            return True
        
        # Also allow if user is authenticated (for admin testing)
        if hasattr(request, 'user') and request.user and request.user.is_authenticated:
            return True
        
        return False


class IsAgentOrAuthenticated(BasePermission):
    """
    Allows access if EITHER:
    - Agent token auth (request.auth is Agent)
    - User token auth (request.user is authenticated)
    
    Use for endpoints that both agents and admins can access.
    """
    
    def has_permission(self, request, view):
        # Agent auth
        if request.auth is not None and isinstance(request.auth, Agent):
            return True
        
        # User auth
        if hasattr(request, 'user') and request.user and request.user.is_authenticated:
            return True
        
        return False

