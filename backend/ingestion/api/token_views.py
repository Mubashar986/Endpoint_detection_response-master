"""
Token Management API Views.

Admin endpoints for generating and managing enrollment tokens.
"""
from rest_framework.views import APIView
from rest_framework.response import Response
from rest_framework import status
from rest_framework.permissions import IsAuthenticated, IsAdminUser
from django_ratelimit.decorators import ratelimit
from django.utils.decorators import method_decorator
from datetime import datetime, timedelta

from ..models_mongo import EnrollmentToken


class TokenListCreateView(APIView):
    """
    GET /api/v1/tokens/ - List all enrollment tokens
    POST /api/v1/tokens/ - Create new enrollment token
    """
    permission_classes = [IsAuthenticated, IsAdminUser]
    
    def get(self, request):
        """List all enrollment tokens."""
        tokens = EnrollmentToken.objects.order_by('-created_at')
        
        token_list = []
        for token in tokens:
            token_list.append({
                'id': str(token.id),
                'token': token.token[:12] + '...',  # Masked for security
                'full_token': token.token,  # Full token for copy
                'description': token.description,
                'expires_at': token.expires_at.isoformat() if token.expires_at else None,
                'max_uses': token.max_uses,
                'current_uses': token.current_uses,
                'is_active': token.is_active,
                'is_valid': token.is_valid(),
                'created_by': token.created_by,
                'created_at': token.created_at.isoformat() if token.created_at else None,
            })
        
        return Response({
            'count': len(token_list),
            'tokens': token_list
        })
    
    @method_decorator(ratelimit(key='user', rate='20/1m', method='POST', block=True))
    def post(self, request):
        """Create new enrollment token."""
        description = request.data.get('description', '')
        max_uses = request.data.get('max_uses', 1)
        expires_in_hours = request.data.get('expires_in_hours', 24)
        
        # Validate max_uses
        try:
            max_uses = int(max_uses)
        except (TypeError, ValueError):
            return Response(
                {'error': 'max_uses must be an integer'},
                status=status.HTTP_400_BAD_REQUEST
            )
        
        # Calculate expiry
        expires_at = None
        if expires_in_hours and expires_in_hours > 0:
            try:
                expires_at = datetime.utcnow() + timedelta(hours=int(expires_in_hours))
            except (TypeError, ValueError):
                return Response(
                    {'error': 'expires_in_hours must be a valid number'},
                    status=status.HTTP_400_BAD_REQUEST
                )
        
        # Create token
        token = EnrollmentToken(
            description=description,
            max_uses=max_uses,
            expires_at=expires_at,
            created_by=request.user.username
        )
        token.save()
        
        return Response({
            'success': True,
            'token': token.token,
            'id': str(token.id),
            'expires_at': expires_at.isoformat() if expires_at else None,
            'max_uses': max_uses,
            'message': 'Copy this token to the agent installer. It will not be shown again in full.'
        }, status=status.HTTP_201_CREATED)


class TokenRevokeView(APIView):
    """
    DELETE /api/v1/tokens/<token_id>/ - Revoke/deactivate a token
    """
    permission_classes = [IsAuthenticated, IsAdminUser]
    
    def delete(self, request, token_id):
        """Revoke an enrollment token."""
        try:
            token = EnrollmentToken.objects.get(id=token_id)
        except EnrollmentToken.DoesNotExist:
            return Response(
                {'error': 'Token not found'},
                status=status.HTTP_404_NOT_FOUND
            )
        
        token.is_active = False
        token.save()
        
        return Response({
            'success': True,
            'message': f'Token {token.token[:8]}... has been revoked'
        })
