"""
rbac_decorators.py

Role-Based Access Control (RBAC) decorators for the EDR system.

Permission Matrix:
- SOC Viewer: Read-only access (view dashboard, alerts, events, rules)
- SOC Analyst: Can view + take response actions (kill process, isolate, toggle rules)
- Superuser/Admin: Full access (all analyst permissions + user management, rule creation)
"""

from functools import wraps
from django.shortcuts import redirect
from django.contrib import messages


def require_analyst_or_admin(view_func):
    """
    Decorator to require SOC Analyst role or higher.
    
    Allows:
    - Users in 'SOC Analyst' group
    - Superusers
    
    Denies:
    - SOC Viewers
    - Unauthenticated users
    """
    @wraps(view_func)
    def wrapper(request, *args, **kwargs):
        if not request.user.is_authenticated:
            messages.error(request, "You must be logged in to access this page")
            return redirect('ingestion:login')
        
        # Superusers always have access
        if request.user.is_superuser:
            return view_func(request, *args, **kwargs)
        
        # Check if user is in SOC Analyst group
        if request.user.groups.filter(name='SOC Analyst').exists():
            return view_func(request, *args, **kwargs)
        
        # Deny access
        messages.error(request, "You don't have permission to perform this action. SOC Analyst role required.")
        return redirect('ingestion:dashboard_home')
    
    return wrapper


def require_admin(view_func):
    """
    Decorator to require Superuser/Admin role.
    
    Allows:
    - Superusers only
    
    Denies:
    - SOC Analysts
    - SOC Viewers
    - Unauthenticated users
    """
    @wraps(view_func)
    def wrapper(request, *args, **kwargs):
        if not request.user.is_authenticated:
            messages.error(request, "You must be logged in to access this page")
            return redirect('ingestion:login')
        
        # Only superusers have admin access
        if request.user.is_superuser:
            return view_func(request, *args, **kwargs)
        
        # Deny access
        messages.error(request, "You don't have permission to access the admin panel. Superuser role required.")
        return redirect('ingestion:dashboard_home')
    
    return wrapper


def get_user_role(user):
    """
    Helper function to get the user's primary role.
    
    Returns:
    - 'superuser' if user.is_superuser
    - 'soc_analyst' if in SOC Analyst group
    - 'soc_viewer' if in SOC Viewer group  
    - 'no_role' if no role assigned
    """
    if not user.is_authenticated:
        return 'unauthenticated'
    
    if user.is_superuser:
        return 'superuser'
    
    if user.groups.filter(name='SOC Analyst').exists():
        return 'soc_analyst'
    
    if user.groups.filter(name='SOC Viewer').exists():
        return 'soc_viewer'
    
    return 'no_role'


def can_toggle_rules(user):
    """Check if user can enable/disable detection rules."""
    return user.is_superuser or user.groups.filter(name='SOC Analyst').exists()


def can_create_edit_rules(user):
    """Check if user can create or edit detection rules."""
    return user.is_superuser


def can_take_response_actions(user):
    """Check if user can kill processes or isolate hosts."""
    return user.is_superuser or user.groups.filter(name='SOC Analyst').exists()


def can_manage_users(user):
    """Check if user can manage other users."""
    return user.is_superuser
