from django.shortcuts import render, redirect, get_object_or_404
from django.contrib.auth.decorators import login_required, user_passes_test
from django.contrib.auth.models import User, Group
from django.contrib import messages
from django.http import JsonResponse
from .detection_models import DetectionRule
import json
from datetime import datetime

# Helper to check if user is admin
def is_admin(user):
    return user.is_authenticated and user.is_superuser

@user_passes_test(is_admin)
def admin_dashboard(request):
    """
    Main Admin Dashboard View
    """
    context = {
        'total_users': User.objects.count(),
        'total_rules': DetectionRule.objects.count(),
        'active_rules': DetectionRule.objects.filter(enabled=True).count(),
        'recent_users': User.objects.order_by('-date_joined')[:5]
    }
    return render(request, 'admin/dashboard.html', context)

# ========== USER MANAGEMENT ==========

@user_passes_test(is_admin)
def user_list(request):
    users = User.objects.all().order_by('id')
    return render(request, 'admin/user_list.html', {'users': users})

@user_passes_test(is_admin)
def user_create(request):
    if request.method == 'POST':
        username = request.POST.get('username')
        email = request.POST.get('email')
        password = request.POST.get('password')
        role_name = request.POST.get('role')

        if User.objects.filter(username=username).exists():
            messages.error(request, 'Username already exists')
            return redirect('ingestion:admin_user_list')

        user = User.objects.create_user(username=username, email=email, password=password)
        
        if role_name:
            try:
                group = Group.objects.get(name=role_name)
                user.groups.add(group)
            except Group.DoesNotExist:
                pass
        
        messages.success(request, f'User {username} created successfully')
        return redirect('ingestion:admin_user_list')
    
    # GET request - show form (handled in modal usually, but can be separate page)
    groups = Group.objects.all()
    return render(request, 'admin/user_form.html', {'groups': groups})

@user_passes_test(is_admin)
def user_edit(request, user_id):
    user = get_object_or_404(User, id=user_id)
    if request.method == 'POST':
        user.email = request.POST.get('email')
        role_name = request.POST.get('role')
        
        # Update Role
        user.groups.clear()
        if role_name:
            try:
                group = Group.objects.get(name=role_name)
                user.groups.add(group)
            except Group.DoesNotExist:
                pass
        
        user.save()
        messages.success(request, f'User {user.username} updated')
        return redirect('ingestion:admin_user_list')

    groups = Group.objects.all()
    current_role = user.groups.first().name if user.groups.exists() else ''
    return render(request, 'admin/user_form.html', {
        'edit_user': user, 
        'groups': groups,
        'current_role': current_role
    })

@user_passes_test(is_admin)
def user_delete(request, user_id):
    user = get_object_or_404(User, id=user_id)
    if user.is_superuser:
        messages.error(request, "Cannot delete a superuser")
    else:
        user.delete()
        messages.success(request, "User deleted")
    return redirect('ingestion:admin_user_list')

# ========== RULE MANAGEMENT ==========

@user_passes_test(is_admin)
def rule_builder(request, rule_id=None):
    rule = None
    if rule_id:
        try:
            rule = DetectionRule.objects.get(rule_id=rule_id)
        except DetectionRule.DoesNotExist:
            messages.error(request, f"Rule {rule_id} not found")
            return redirect('ingestion:rules')

    if request.method == 'POST':
        name = request.POST.get('name')
        severity = request.POST.get('severity')
        description = request.POST.get('description')
        logic_json = request.POST.get('logic')

        try:
            detection_logic = json.loads(logic_json)
        except json.JSONDecodeError:
            messages.error(request, "Invalid JSON logic")
            return render(request, 'admin/rule_builder.html', {'rule': rule})

        if rule:
            rule.name = name
            rule.severity = severity
            rule.description = description
            rule.detection_logic = detection_logic
            rule.save()
            messages.success(request, "Rule updated")
        else:
            DetectionRule.objects.create(
                rule_id=f"RULE-{name[:10].upper().replace(' ', '-')}-{datetime.now().timestamp():.0f}",
                name=name,
                severity=severity,
                description=description,
                detection_logic=detection_logic,
                enabled=True,
                author=request.user.username
            )
            messages.success(request, "Rule created")
        
        return redirect('ingestion:rules') # Redirect to main rules list for now

    # Prepare rule data for template
    context = {'rule': rule}
    if rule and rule.detection_logic:
        # Convert detection_logic dict to pretty JSON string for textarea
        context['rule_logic_json'] = json.dumps(rule.detection_logic, indent=2)
    
    return render(request, 'admin/rule_builder.html', context)
