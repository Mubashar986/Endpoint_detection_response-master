"""
Rate Limiting Utilities for EDR System

This module provides custom rate limiting decorators with enhanced logging
and monitoring capabilities for tracking rate limit violations.
"""

from functools import wraps
from django_ratelimit.decorators import ratelimit as django_ratelimit
from rest_framework.response import Response
from rest_framework import status
import logging

logger = logging.getLogger('ratelimit')


from django_ratelimit.core import is_ratelimited

def ratelimit_with_logging(key, rate, method='POST', group=None):
    """
    Enhanced rate limiting decorator that logs violations.
    
    Args:
        key: Rate limit key (e.g., 'ip', 'user', 'header:X-Agent-Token')
        rate: Rate limit (e.g., '1000/m', '5/1h')
        method: HTTP methods to limit (default: 'POST')
        group: Optional group name for organizing related limits
    
    Usage:
        @ratelimit_with_logging(key='header:HTTP_X_AGENT_TOKEN', rate='1000/m')
        def telemetry_endpoint(request):
            ...
    """
    def decorator(func):
        @wraps(func)
        def wrapper(request, *args, **kwargs):
            # Check if we should check rate limit for this method
            if method is not None and request.method != method:
                return func(request, *args, **kwargs)

            # Ensure group is set (required for is_ratelimited)
            group_name = group or func.__name__

            # Check rate limit manually
            # increment=True means we count this request
            is_limited = is_ratelimited(
                request, 
                group=group_name, 
                key=key, 
                rate=rate, 
                method=method, 
                increment=True
            )

            
            if is_limited:
                # Extract identifying information for logging
                if key.startswith('header:'):
                    header_name = key.replace('header:', '').replace('-', '_')
                    key_value = request.META.get(header_name, 'unknown')
                elif key == 'ip':
                    key_value = request.META.get('REMOTE_ADDR', 'unknown')
                elif key == 'user':
                    key_value = str(request.user) if request.user.is_authenticated else 'anonymous'
                elif key == 'user_or_ip':
                    key_value = str(request.user) if request.user.is_authenticated else request.META.get('REMOTE_ADDR', 'unknown')
                else:
                    key_value = 'unknown'
                
                # Log the violation
                logger.warning(
                    f"Rate limit exceeded | "
                    f"endpoint={func.__name__} | "
                    f"key={key} | "
                    f"value={key_value} | "
                    f"rate={rate} | "
                    f"method={request.method} | "
                    f"ip={request.META.get('REMOTE_ADDR', 'unknown')} | "
                    f"path={request.path}"
                )
                
                # Return 429 Too Many Requests
                return Response({
                    'error': 'Rate limit exceeded',
                    'message': f'You have exceeded the rate limit of {rate}',
                    'retry_after': get_retry_after(rate)
                }, status=status.HTTP_429_TOO_MANY_REQUESTS)
            
            # Request was not limited, proceed to next decorator or view
            return func(request, *args, **kwargs)
        
        return wrapper
    return decorator


def get_retry_after(rate):
    """
    Calculate retry_after value from rate string.
    
    Args:
        rate: Rate string like '100/m' or '5/1h'
    
    Returns:
        int: Seconds until retry
    """
    if not rate:
        return 60
    
    try:
        _, period = rate.split('/')
        
        # Parse period
        if period.endswith('s'):
            return int(period[:-1])
        elif period.endswith('m'):
            return int(period[:-1]) * 60
        elif period.endswith('h'):
            return int(period[:-1]) * 3600
        elif period.endswith('d'):
            return int(period[:-1]) * 86400
        else:
            # Default to period as seconds
            return int(period)
    except:
        return 60  # Default to 60 seconds


# Convenience functions for extracting agent tokens
def get_agent_token(group, request):
    """
    Extract agent token from request headers.
    
    Used as a custom key function for rate limiting.
    """
    token = request.META.get('HTTP_X_AGENT_TOKEN', 'anonymous')
    return f'agent:{token}'


def get_user_or_ip(group, request):
    """
    Extract user ID if authenticated, otherwise IP address.
    
    Used as a custom key function for rate limiting.
    """
    if request.user.is_authenticated:
        return f'user:{request.user.id}'
    else:
        return f'ip:{request.META.get("REMOTE_ADDR", "unknown")}'
