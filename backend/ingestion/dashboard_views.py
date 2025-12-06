"""
Dashboard Views - Display data to SOC analysts

Why separate file?
- Keep dashboard logic separate from API logic
- Easy to maintain and test
- Clean code organization

What it does:
- Fetches data from MongoDB
- Formats for HTML templates (all times are now local timezone-aware)
- Handles SOC actions (mark resolved, assign, etc.)
"""

from django.shortcuts import render, get_object_or_404
from django.http import JsonResponse
from django.views.decorators.http import require_http_methods
from django.contrib.auth.decorators import login_required
from django.utils import timezone  # Django timezone utilities
from datetime import timedelta
import pytz
import json

from rest_framework.decorators import api_view, permission_classes, authentication_classes
from rest_framework.permissions import IsAuthenticated
from rest_framework.authentication import TokenAuthentication, SessionAuthentication

from .models import TelemetryEvent
from .detection_models import DetectionRule, Alert
from .rbac_decorators import require_analyst_or_admin, can_toggle_rules, get_user_role, can_take_response_actions
from .ratelimit_utils import ratelimit_with_logging
from django.conf import settings

# ========== UTILITY FUNCTIONS ==========

def to_local(dt):
    """
    Convert an aware/naive datetime (assumed UTC) into server's local timezone.
    Uses pytz.UTC instead of 'timezone.utc'.
    """
    if not dt:
        return None
    # Ensure dt is aware and UTC
    if timezone.is_naive(dt):
        dt = timezone.make_aware(dt, pytz.UTC)
    else:
        dt = dt.astimezone(pytz.UTC)
    local_tz = timezone.get_current_timezone()
    return timezone.localtime(dt, local_tz)

def calculate_time_ago(dt):
    """Convert datetime to human-readable format (e.g., '5 min ago')"""
    if not dt:
        return "Unknown"
    # Ensure dt is aware, UTC
    if timezone.is_naive(dt):
        dt = timezone.make_aware(dt, pytz.UTC)
    else:
        dt = dt.astimezone(pytz.UTC)
    now = timezone.now()
    diff = now - dt
    if diff.total_seconds() < 60:
        return "Just now"
    elif diff.total_seconds() < 3600:
        minutes = int(diff.total_seconds() // 60)
        return f"{minutes} min ago"
    elif diff.total_seconds() < 86400:
        hours = int(diff.total_seconds() // 3600)
        return f"{hours} hour ago" if hours == 1 else f"{hours} hours ago"
    else:
        days = diff.days
        return f"{days} day ago" if days == 1 else f"{days} days ago"

# ========== API ENDPOINTS (JSON responses) ==========

@api_view(['GET'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_READ, method='GET')
def stats_api(request):
    total_events = TelemetryEvent.objects.count()
    total_rules = DetectionRule.objects.count()
    active_rules = DetectionRule.objects.filter(enabled=True).count()
    total_alerts = Alert.objects.count()
    unresolved = Alert.objects.filter(alert_status='UNRESOLVED').count()
    critical = Alert.objects.filter(
        severity='CRITICAL', 
        alert_status='UNRESOLVED'
    ).count()
    detection_rate = (total_alerts / total_events * 100) if total_events > 0 else 0
    return JsonResponse({
        'total_events': total_events,
        'total_rules': total_rules,
        'active_rules': active_rules,
        'total_alerts': total_alerts,
        'unresolved_alerts': unresolved,
        'critical_alerts': critical,
        'detection_rate': f"{detection_rate:.2f}%"
    })

@api_view(['GET'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_READ, method='GET')
def alerts_list_api(request):
    status = request.GET.get('status', 'UNRESOLVED')
    severity = request.GET.get('severity')
    limit = int(request.GET.get('limit', 20))
    query = {'alert_status': status}
    if severity:
        query['severity'] = severity
    alerts = Alert.objects.filter(**query).order_by('-first_detected')[:limit]
    alerts_data = []
    for alert in alerts:
        utc_first = alert.first_detected if alert.first_detected else None
        utc_last = alert.last_detected if getattr(alert, 'last_detected', None) else None
        alerts_data.append({
            'alert_id': alert.alert_id,
            'severity': alert.severity,
            'rule_name': alert.rule_name,
            'endpoint_id': alert.endpoint_id,
            'hostname': alert.hostname,
            'status': alert.alert_status,
            'time_ago': calculate_time_ago(utc_first),
            'first_detected': utc_first.isoformat() if utc_first else None,
            'first_detected_local': to_local(utc_first).isoformat() if utc_first else None,
            'last_detected': utc_last.isoformat() if utc_last else None,
            'last_detected_local': to_local(utc_last).isoformat() if utc_last else None,
        })
    return JsonResponse({'count': len(alerts_data), 'alerts': alerts_data})

@api_view(['GET'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_READ, method='GET')
def alert_detail_api(request, alert_id):
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    utc_first = alert.first_detected if alert.first_detected else None
    utc_last = alert.last_detected if getattr(alert, 'last_detected', None) else None
    return JsonResponse({
        'alert_id': alert.alert_id,
        'rule_id': alert.rule_id,
        'rule_name': alert.rule_name,
        'severity': alert.severity,
        'confidence': alert.confidence,
        'status': alert.alert_status,
        'endpoint_id': alert.endpoint_id,
        'hostname': alert.hostname,
        'mitre_tactics': alert.mitre_tactics,
        'mitre_techniques': alert.mitre_techniques,
        'evidence': alert.evidence_summary,
        'matched_indicators': alert.evidence_summary.get('matched_indicators', []),
        'first_detected': utc_first.isoformat() if utc_first else None,
        'first_detected_local': to_local(utc_first).isoformat() if utc_first else None,
        'last_detected': utc_last.isoformat() if utc_last else None,
        'last_detected_local': to_local(utc_last).isoformat() if utc_last else None,
        'occurrence_count': alert.occurrence_count,
        'assigned_analyst': alert.assigned_analyst,
        'notes': [
            {
                'timestamp': note['timestamp'].isoformat(),
                'analyst': note['analyst'],
                'text': note['note']
            } for note in alert.notes
        ]
    })

# ========== SOC ACTION ENDPOINTS ==========

@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@require_analyst_or_admin
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_WRITE, method='POST')
def alert_update_status(request, alert_id):
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    try:
        data = json.loads(request.body)
    except json.JSONDecodeError:
        return JsonResponse({'error': 'Invalid JSON'}, status=400)
    status = data.get('status')
    note = data.get('note', '')
    if status not in ['RESOLVED', 'FALSE_POSITIVE']:
        return JsonResponse({'error': 'Invalid status'}, status=400)
    analyst = request.user.email if request.user.is_authenticated else 'system'
    if status == 'RESOLVED':
        alert.mark_resolved(analyst, note)
    elif status == 'FALSE_POSITIVE':
        alert.mark_false_positive(analyst, note)
    return JsonResponse({
        'success': True,
        'alert_id': alert.alert_id,
        'new_status': alert.alert_status
    })

@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@require_analyst_or_admin
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_WRITE, method='POST')
def alert_assign(request, alert_id):
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    data = json.loads(request.body)
    analyst = data.get('analyst_email', request.user.email if request.user.is_authenticated else None)
    if not analyst:
        return JsonResponse({'error': 'No analyst specified'}, status=400)
    alert.assign_to(analyst)
    return JsonResponse({
        'success': True,
        'alert_id': alert.alert_id,
        'assigned_to': analyst
    })

@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@require_analyst_or_admin # Assuming adding a note is also a response action
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_WRITE, method='POST')
def alert_add_note(request, alert_id):
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    data = json.loads(request.body)
    note_text = data.get('note', '')
    if not note_text:
        return JsonResponse({'error': 'Note cannot be empty'}, status=400)
    analyst = request.user.email if request.user.is_authenticated else 'system'
    alert.add_note(analyst, note_text)
    return JsonResponse({
        'success': True,
        'alert_id': alert.alert_id,
        'note_added': note_text
    })

@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@require_analyst_or_admin  # Rule toggling requires analyst permissions
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_ADMIN, method='POST')
def rule_toggle(request, rule_id):
    try:
        rule = DetectionRule.objects.get(rule_id=rule_id)
    except DetectionRule.DoesNotExist:
        return JsonResponse({'error': 'Rule not found'}, status=404)
    rule.enabled = not rule.enabled
    rule.save()
    return JsonResponse({
        'success': True,
        'rule_id': rule.rule_id,
        'enabled': rule.enabled
    })

@api_view(['GET'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_READ, method='GET')
def alert_timeline(request, alert_id):
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    start_time = alert.first_detected - timedelta(minutes=30)
    end_time = alert.first_detected + timedelta(minutes=30)
    timeline_events_qs = TelemetryEvent.objects.filter(
        agent_id=alert.endpoint_id,
        created_at__gte=start_time,
        created_at__lte=end_time
    ).order_by('created_at')
    timeline_events = list(timeline_events_qs)
    events_data = []
    for event in timeline_events:
        utc_created = event.created_at
        events_data.append({
            'event_id': event.event_id,
            'type': event.event_type,
            'timestamp': utc_created.isoformat() if utc_created else None,
            'timestamp_local': to_local(utc_created).isoformat() if utc_created else None,
            'severity': event.severity,
            'is_alert': event.event_id in alert.matched_event_ids
        })
    return JsonResponse({
        'alert_id': alert_id,
        'endpoint': alert.endpoint_id,
        'time_window': f"{start_time} to {end_time}",
        'event_count': len(events_data),
        'timeline': events_data
    })

# ========== PAGE VIEWS (HTML templates) ==========

@login_required
def dashboard_home(request):
    total_events = TelemetryEvent.objects.count()
    total_rules = DetectionRule.objects.count()
    active_rules = DetectionRule.objects.filter(enabled=True).count()
    total_alerts = Alert.objects.count()
    unresolved = Alert.objects.filter(alert_status='UNRESOLVED').count()
    recent_alerts_qs = Alert.objects.filter(
        alert_status='UNRESOLVED'
    ).order_by('-first_detected')[:10]
    recent_alerts = list(recent_alerts_qs)
    for alert in recent_alerts:
        if hasattr(alert, "first_detected"):
            alert.first_detected = to_local(alert.first_detected)
    active_rule_list = DetectionRule.objects.filter(enabled=True)
    context = {
        'total_events': total_events,
        'total_rules': total_rules,
        'active_rules': active_rules,
        'total_alerts': total_alerts,
        'unresolved': unresolved,
        'recent_alerts': recent_alerts,
        'active_rule_list': active_rule_list
    }
    return render(request, 'dashboard/home.html', context)

@login_required
def alert_detail_view(request, alert_id):
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return render(request, 'dashboard/404.html', {'message': 'Alert not found'}, status=404)
    alert.first_detected = to_local(alert.first_detected)
    alert.last_detected = to_local(alert.last_detected)
    recent_events_qs = TelemetryEvent.objects.filter(
        agent_id=alert.endpoint_id
    ).order_by('-created_at')[:5]
    recent_events = list(recent_events_qs)
    for ev in recent_events:
        if hasattr(ev, "created_at"):
            ev.created_at = to_local(ev.created_at)
    context = {
        'alert': alert,
        'time_ago': calculate_time_ago(alert.first_detected),
        'recent_events': recent_events,
        'can_manage_alerts': can_take_response_actions(request.user)
    }
    return render(request, 'dashboard/alert_detail.html', context)

@login_required
def rules_view(request):
    rules = DetectionRule.objects.all().order_by('-severity', 'rule_id')
    context = {
        'rules': rules,
        'total_rules': rules.count(),
        'enabled_rules': rules.filter(enabled=True).count(),
        'can_toggle_rules': can_toggle_rules(request.user),
        'can_create_edit_rules': request.user.is_superuser
    }
    return render(request, 'dashboard/rules.html', context)

@login_required
def events_view(request):
    event_type = request.GET.get('type', '')
    limit = int(request.GET.get('limit', 50))
    query = {}
    if event_type:
        query['event_type'] = event_type
    events_qs = TelemetryEvent.objects.filter(**query).order_by('-created_at')[:limit]
    events = list(events_qs)
    for ev in events:
        if hasattr(ev, "created_at"):
            ev.created_at = to_local(ev.created_at)
    context = {
        'events': events,
        'event_count': len(events),
        'event_type_filter': event_type
    }
    return render(request, 'dashboard/events.html', context)

from .models_mongo import ResponseAction


@login_required
def alerts_list_view(request):
    alerts_qs = Alert.objects.all().order_by('-first_detected')[:100]
    alerts = list(alerts_qs)
    for alert in alerts:
        if hasattr(alert, "first_detected"):
            alert.first_detected = to_local(alert.first_detected)
    return render(request, 'dashboard/alert_list.html', {'alerts': alerts})

@login_required
def response_actions_list(request):
    """
    View for the Response Actions Audit Trail page.
    """
    actions_qs = ResponseAction.objects.all().order_by('-timestamp')[:100]
    actions = list(actions_qs)
    for action in actions:
        if hasattr(action, "timestamp"):
            action.timestamp = to_local(action.timestamp)
            
    return render(request, 'dashboard/response_actions.html', {'actions': actions})

@api_view(['GET'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='user', rate=settings.RATELIMIT_DASHBOARD_READ, method='GET')
def global_search(request):
    """
    Global omnisearch API (P0-013)
    Searches across alerts and endpoints
    Returns max 5 results per category for fast typeahead
    """
    from mongoengine import Q
    
    query = request.GET.get('q', '').strip()
    
    if len(query) < 2:
        return JsonResponse({'alerts': [], 'endpoints': []})
    
    # Search alerts using Q objects for OR logic (MongoEngine syntax)
    alerts_qs = Alert.objects.filter(
        Q(alert_id__icontains=query) |
        Q(rule_name__icontains=query) |
        Q(endpoint_id__icontains=query)
    )
    
    # Limit to 5 most recent for typeahead performance
    alerts = list(alerts_qs.order_by('-first_detected')[:5])
    
    alerts_data = []
    for alert in alerts:
        alerts_data.append({
            'alert_id': alert.alert_id,
            'severity': alert.severity,
            'rule_name': alert.rule_name,
            'endpoint_id': alert.endpoint_id,
            'status': alert.alert_status
        })
    
    # Search endpoints (unique endpoint_ids)
    # Get distinct endpoints matching query
    # distinct() returns a list, so we slice it directly
    endpoints = Alert.objects.filter(
        endpoint_id__icontains=query
    ).distinct('endpoint_id')
    
    # Limit to 5
    endpoints = endpoints[:5]
    
    return JsonResponse({
        'alerts': alerts_data,
        'endpoints': endpoints
    })


@api_view(['POST'])
@authentication_classes([SessionAuthentication, TokenAuthentication])
@permission_classes([IsAuthenticated])
@ratelimit_with_logging(key='user', rate='50/m', method='POST')
def bulk_alert_action(request):
    """
    Bulk operations API (P0-004) - Optimized
    Handles bulk resolve, false positive, and assignment using atomic updates.
    """
    import json
    from datetime import datetime, timezone
    
    try:
        data = request.data
        alert_ids = data.get('alert_ids', [])
        action = data.get('action')
        note = data.get('note', '')
        assignee = data.get('assignee')
        
        if not alert_ids or not action:
            return JsonResponse({'error': 'Missing alert_ids or action'}, status=400)
            
        # Validate action
        valid_actions = ['resolve', 'false_positive', 'assign']
        if action not in valid_actions:
            return JsonResponse({'error': f'Invalid action. Must be one of: {", ".join(valid_actions)}'}, status=400)
            
        user_email = request.user.email
        timestamp = datetime.now(timezone.utc)
        success_count = 0
        
        # Perform Bulk Updates
        # We use .update() to bypass the expensive Alert.save() method which recalculates stats
        
        if action == 'resolve':
            note_doc = {
                "timestamp": timestamp,
                "analyst": user_email,
                "note": f"Resolved: {note}" if note else "Resolved"
            }
            
            success_count = Alert.objects.filter(alert_id__in=alert_ids).update(
                set__alert_status="RESOLVED",
                set__resolved_at=timestamp,
                push__notes=note_doc
            )
            
        elif action == 'false_positive':
            note_doc = {
                "timestamp": timestamp,
                "analyst": user_email,
                "note": f"False Positive: {note}" if note else "Marked as False Positive"
            }
            
            success_count = Alert.objects.filter(alert_id__in=alert_ids).update(
                set__alert_status="FALSE_POSITIVE",
                set__resolved_at=timestamp,
                push__notes=note_doc
            )
            
        elif action == 'assign':
            if not assignee:
                return JsonResponse({'error': 'Missing assignee email'}, status=400)
                
            note_doc = {
                "timestamp": timestamp,
                "analyst": user_email,
                "note": f"Bulk assignment note: {note}" if note else f"Assigned to {assignee}"
            }
            
            success_count = Alert.objects.filter(alert_id__in=alert_ids).update(
                set__assigned_analyst=assignee,
                set__alert_status="INVESTIGATING",
                push__notes=note_doc
            )
        
        # If success_count is 0, it might mean IDs were invalid or not found
        if success_count == 0 and len(alert_ids) > 0:
             # Double check if any exist to give better error
             existing = Alert.objects.filter(alert_id__in=alert_ids).count()
             if existing == 0:
                 return JsonResponse({'error': 'No matching alerts found to update'}, status=404)
        
        return JsonResponse({
            'success_count': success_count,
            'total_requested': len(alert_ids),
            'errors': [] # Bulk update doesn't give per-item errors, but it's atomic-ish
        })
        
    except json.JSONDecodeError:
        return JsonResponse({'error': 'Invalid JSON'}, status=400)
    except Exception as e:
        return JsonResponse({'error': str(e)}, status=500)
