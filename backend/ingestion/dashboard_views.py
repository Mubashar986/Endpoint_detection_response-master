"""
Dashboard Views - Display data to SOC analysts

Why separate file?
- Keep dashboard logic separate from API logic
- Easy to maintain and test
- Clean code organization

What it does:
- Fetches data from MongoDB
- Formats for HTML templates
- Handles SOC actions (mark resolved, assign, etc.)
"""

from django.shortcuts import render, get_object_or_404
from django.http import JsonResponse
from django.views.decorators.http import require_http_methods
from django.contrib.auth.decorators import login_required
from datetime import datetime, timedelta, timezone
import json

from .models import TelemetryEvent
from .detection_models import DetectionRule, Alert

# ========== UTILITY FUNCTIONS ==========

def calculate_time_ago(dt):
    """Convert datetime to human-readable format (e.g., '5 min ago')"""
    if not dt:
        return "Unknown"
    
    if dt.tzinfo is None:
        # If dt is naive, make it aware (assume UTC)
        dt = dt.replace(tzinfo=timezone.utc)
    
    now = datetime.now(timezone.utc)
    diff = now - dt
    
    if diff.seconds < 60:
        return "Just now"
    elif diff.seconds < 3600:
        minutes = diff.seconds // 60
        return f"{minutes} min ago"
    elif diff.seconds < 86400:
        hours = diff.seconds // 3600
        return f"{hours} hour ago" if hours == 1 else f"{hours} hours ago"
    else:
        days = diff.days
        return f"{days} day ago" if days == 1 else f"{days} days ago"


# ========== API ENDPOINTS (JSON responses) ==========

@require_http_methods(["GET"])
def stats_api(request):
    """
    API: Get system statistics for dashboard
    
    Why separate API?
    - Frontend can fetch JSON without page reload
    - Real-time data updates
    - Works with JavaScript
    
    Returns:
    {
        "total_events": 15847,
        "total_rules": 6,
        "active_rules": 6,
        "total_alerts": 23,
        "unresolved_alerts": 5,
        "critical_alerts": 2
    }
    """
    
    # Query MongoDB for statistics
    total_events = TelemetryEvent.objects.count()
    total_rules = DetectionRule.objects.count()
    active_rules = DetectionRule.objects.filter(enabled=True).count()
    total_alerts = Alert.objects.count()
    
    # Filter alerts by status and severity
    unresolved = Alert.objects.filter(alert_status='UNRESOLVED').count()
    critical = Alert.objects.filter(
        severity='CRITICAL', 
        alert_status='UNRESOLVED'
    ).count()
    
    # Calculate detection rate
    detection_rate = (total_alerts / total_events * 100) if total_events > 0 else 0
    
    # Return JSON response
    return JsonResponse({
        'total_events': total_events,
        'total_rules': total_rules,
        'active_rules': active_rules,
        'total_alerts': total_alerts,
        'unresolved_alerts': unresolved,
        'critical_alerts': critical,
        'detection_rate': f"{detection_rate:.2f}%"
    })


@require_http_methods(["GET"])
def alerts_list_api(request):
    """
    API: Get list of alerts with filters
    
    Query parameters:
    - status: UNRESOLVED, RESOLVED, FALSE_POSITIVE
    - severity: CRITICAL, HIGH, MEDIUM, LOW
    - limit: How many to return (default: 20)
    
    Returns:
    [
        {
            "alert_id": "ALT-20241030-A3F2C1",
            "severity": "CRITICAL",
            "rule_name": "Suspicious Encoded PowerShell",
            "endpoint_id": "DESKTOP-8HQ1T92",
            "status": "UNRESOLVED",
            "time_ago": "5 min ago"
        },
        ...
    ]
    """
    
    # Get filter parameters from URL
    status = request.GET.get('status', 'UNRESOLVED')
    severity = request.GET.get('severity')
    limit = int(request.GET.get('limit', 20))
    
    # Build MongoDB query
    query = {'alert_status': status}
    if severity:
        query['severity'] = severity
    
    # Fetch alerts, sorted by newest first
    alerts = Alert.objects.filter(**query).order_by('-first_detected')[:limit]
    
    # Format for JSON response
    alerts_data = []
    for alert in alerts:
        alerts_data.append({
            'alert_id': alert.alert_id,
            'severity': alert.severity,
            'rule_name': alert.rule_name,
            'endpoint_id': alert.endpoint_id,
            'hostname': alert.hostname,
            'status': alert.alert_status,
            'time_ago': calculate_time_ago(alert.first_detected),
            'first_detected': alert.first_detected.isoformat()
        })
    
    return JsonResponse({
        'count': len(alerts_data),
        'alerts': alerts_data
    })


@require_http_methods(["GET"])
def alert_detail_api(request, alert_id):
    """
    API: Get full alert details
    
    Returns complete alert information for investigation
    """
    
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    
    return JsonResponse({
        'alert_id': alert.alert_id,
        'rule_id': alert.rule_id,
        'rule_name': alert.rule_name,
        'severity': alert.severity,
        'confidence': alert.confidence,
        'status': alert.alert_status,
        
        # Endpoint info
        'endpoint_id': alert.endpoint_id,
        'hostname': alert.hostname,
        
        # MITRE ATT&CK
        'mitre_tactics': alert.mitre_tactics,
        'mitre_techniques': alert.mitre_techniques,
        
        # Evidence
        'evidence': alert.evidence_summary,
        'matched_indicators': alert.evidence_summary.get('matched_indicators', []),
        
        # Timeline
        'first_detected': alert.first_detected.isoformat(),
        'last_detected': alert.last_detected.isoformat(),
        'occurrence_count': alert.occurrence_count,
        
        # Investigation
        'assigned_analyst': alert.assigned_analyst,
        'notes': [
            {
                'timestamp': note['timestamp'].isoformat(),
                'analyst': note['analyst'],
                'text': note['note']
            }
            for note in alert.notes
        ]
    })


# ========== SOC ACTION ENDPOINTS ==========

@require_http_methods(["POST"])
def alert_update_status(request, alert_id):
    """
    SOC Action: Mark alert as RESOLVED or FALSE_POSITIVE
    
    Why this matters:
    - Analysts need to mark threats as handled
    - False positives must be tracked (reduces alert fatigue)
    - Tracks SOC response time
    
    Post data:
    {
        "status": "RESOLVED" or "FALSE_POSITIVE",
        "note": "Malware removed from endpoint" (optional)
    }
    """
    
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    
    # Parse request body
    try:
        data = json.loads(request.body)
    except json.JSONDecodeError:
        return JsonResponse({'error': 'Invalid JSON'}, status=400)
    
    status = data.get('status')
    note = data.get('note', '')
    
    # Validate status
    if status not in ['RESOLVED', 'FALSE_POSITIVE']:
        return JsonResponse({'error': 'Invalid status'}, status=400)
    
    # Update alert
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


@require_http_methods(["POST"])
def alert_assign(request, alert_id):
    """
    SOC Action: Assign alert to analyst
    
    Why this matters:
    - Track who's investigating what
    - Prevent duplicate work
    - Enable team workflow
    
    Post data:
    {
        "analyst_email": "analyst@company.com"
    }
    """
    
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


@require_http_methods(["POST"])
def alert_add_note(request, alert_id):
    """
    SOC Action: Add investigation note to alert
    
    Why this matters:
    - Document investigation progress
    - Create audit trail
    - Share findings with team
    
    Post data:
    {
        "note": "Investigated command line. Found legitimate script."
    }
    """
    
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


@require_http_methods(["POST"])
def rule_toggle(request, rule_id):
    """
    SOC Action: Enable or disable detection rule
    
    Why this matters:
    - Disable noisy rules temporarily
    - Enable new rules for testing
    - Tune detection sensitivity
    
    No post data needed - toggles current state
    """
    
    try:
        rule = DetectionRule.objects.get(rule_id=rule_id)
    except DetectionRule.DoesNotExist:
        return JsonResponse({'error': 'Rule not found'}, status=404)
    
    # Toggle enabled state
    rule.enabled = not rule.enabled
    rule.save()
    
    return JsonResponse({
        'success': True,
        'rule_id': rule.rule_id,
        'enabled': rule.enabled
    })


@require_http_methods(["GET"])
def alert_timeline(request, alert_id):
    """
    Investigation: Get timeline of related events
    
    Why this matters:
    - Understand the attack sequence
    - Correlate related events
    - Reconstruct incident timeline
    
    Returns all events from same endpoint around alert time
    """
    
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return JsonResponse({'error': 'Alert not found'}, status=404)
    
    # Get time window (1 hour before and after alert)
    start_time = alert.first_detected - timedelta(minutes=30)
    end_time = alert.first_detected + timedelta(minutes=30)
    
    # Find all events from same endpoint in time window
    timeline_events = TelemetryEvent.objects.filter(
        agent_id=alert.endpoint_id,
        created_at__gte=start_time,
        created_at__lte=end_time
    ).order_by('created_at')
    
    events_data = []
    for event in timeline_events:
        events_data.append({
            'event_id': event.event_id,
            'type': event.event_type,
            'timestamp': event.created_at.isoformat(),
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


def dashboard_home(request):
    """
    View: Main dashboard page
    
    Renders HTML with data
    """
    
    # Get data
    total_events = TelemetryEvent.objects.count()
    total_rules = DetectionRule.objects.count()
    active_rules = DetectionRule.objects.filter(enabled=True).count()
    total_alerts = Alert.objects.count()
    unresolved = Alert.objects.filter(alert_status='UNRESOLVED').count()
    
    # Get recent alerts
    recent_alerts = Alert.objects.filter(
        alert_status='UNRESOLVED'
    ).order_by('-first_detected')[:10]
    
    # Get active rules
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



def alert_detail_view(request, alert_id):
    """
    View: Single alert detail page
    """
    
    try:
        alert = Alert.objects.get(alert_id=alert_id)
    except Alert.DoesNotExist:
        return render(request, 'dashboard/404.html', {'message': 'Alert not found'}, status=404)
    
    # Get related events
    recent_events = TelemetryEvent.objects.filter(
        agent_id=alert.endpoint_id
    ).order_by('-created_at')[:5]
    
    context = {
        'alert': alert,
        'time_ago': calculate_time_ago(alert.first_detected),
        'recent_events': recent_events
    }
    
    return render(request, 'dashboard/alert_detail.html', context)



def rules_view(request):
    """
    View: Rules management page
    """
    
    rules = DetectionRule.objects.all().order_by('-severity', 'rule_id')
    
    context = {
        'rules': rules,
        'total_rules': rules.count(),
        'enabled_rules': rules.filter(enabled=True).count()
    }
    
    return render(request, 'dashboard/rules.html', context)



def events_view(request):
    """
    View: Events log page
    """
    
    # Get filter parameters
    event_type = request.GET.get('type', '')
    limit = int(request.GET.get('limit', 50))
    
    # Build query
    query = {}
    if event_type:
        query['event_type'] = event_type
    
    # Fetch events
    events = TelemetryEvent.objects.filter(**query).order_by('-created_at')[:limit]
    
    context = {
        'events': events,
        'event_count': len(events),
        'event_type_filter': event_type
    }
    
    return render(request, 'dashboard/events.html', context)


def alerts_list_view(request):
    """View all alerts"""
    alerts = Alert.objects.all().order_by('-first_detected')[:100]
    return render(request, 'dashboard/alert_list.html', {'alerts': alerts})
