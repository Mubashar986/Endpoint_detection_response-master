"""
Agent Enrollment API.

Handles the one-time registration of agents using enrollment tokens.
"""
from rest_framework.views import APIView
from rest_framework.response import Response
from rest_framework import status
from rest_framework.permissions import AllowAny
from django_ratelimit.decorators import ratelimit
from django.utils.decorators import method_decorator
import secrets
from datetime import datetime

from ..models_mongo import Agent, EnrollmentToken


@method_decorator(
    ratelimit(key='ip', rate='10/1m', method='POST', block=True),
    name='dispatch'
)
class EnrollAgentView(APIView):
    """
    POST /api/v1/enroll/
    
    Register a new agent using an enrollment token.
    Returns a permanent identity token for future authentication.
    
    Request Body:
        enrollment_token: str - The one-time token from installer
        agent_id: str - Unique agent identifier (UUID)
        hostname: str - Machine hostname
        os_version: str - Operating system version
        agent_version: str - Agent software version
    
    Response:
        success: bool
        identity_token: str - Permanent token for future API calls
        config_id: str - Assigned configuration ID (if any)
    """
    permission_classes = [AllowAny]  # No auth required - uses enrollment token
    
    def post(self, request):
        # Extract required fields
        enrollment_token = request.data.get('enrollment_token')
        agent_id = request.data.get('agent_id')
        hostname = request.data.get('hostname', 'Unknown')
        os_version = request.data.get('os_version', 'Unknown')
        agent_version = request.data.get('agent_version', '1.0.0')
        
        # Validate required fields
        if not enrollment_token or not agent_id:
            return Response(
                {'error': 'enrollment_token and agent_id are required'},
                status=status.HTTP_400_BAD_REQUEST
            )
        
        # Validate enrollment token
        try:
            token_obj = EnrollmentToken.objects.get(token=enrollment_token)
        except EnrollmentToken.DoesNotExist:
            return Response(
                {'error': 'Invalid enrollment token'},
                status=status.HTTP_401_UNAUTHORIZED
            )
        
        if not token_obj.is_valid():
            return Response(
                {'error': 'Enrollment token expired or max uses reached'},
                status=status.HTTP_401_UNAUTHORIZED
            )
        
        # Generate identity token (64 hex chars = 256 bits)
        identity_token = secrets.token_hex(32)
        
        # Check if agent already exists (re-enrollment)
        try:
            agent = Agent.objects.get(agent_id=agent_id)
            # Re-enrollment - update existing
            agent.hostname = hostname
            agent.os_version = os_version
            agent.agent_version = agent_version
            agent.identity_token = identity_token
            agent.identity_token_created_at = datetime.utcnow()
            agent.enrollment_token_id = str(token_obj.id)
            agent.last_seen = datetime.utcnow()
            agent.is_active = True
            agent.is_online = True
            agent.save()
        except Agent.DoesNotExist:
            # New agent
            agent = Agent(
                agent_id=agent_id,
                hostname=hostname,
                os_version=os_version,
                agent_version=agent_version,
                identity_token=identity_token,
                identity_token_created_at=datetime.utcnow(),
                enrollment_token_id=str(token_obj.id),
                first_seen=datetime.utcnow(),
                last_seen=datetime.utcnow(),
                is_active=True,
                is_online=True
            )
            agent.save()
        
        # Increment enrollment token usage
        token_obj.increment_usage()
        
        return Response({
            'success': True,
            'agent_id': agent.agent_id,
            'identity_token': identity_token,  # Agent saves this to auth.secret
            'config_id': agent.config_id,
            'message': 'Agent registered successfully. Save the identity_token to auth.secret.'
        }, status=status.HTTP_201_CREATED)
