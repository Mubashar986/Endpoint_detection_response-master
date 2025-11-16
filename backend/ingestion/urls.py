

from django.urls import path
from . import views
from . import dashboard_views

app_name = 'ingestion'

urlpatterns = [
    # ========== EXISTING TELEMETRY API ==========
    path('api/v1/telemetry/', views.telemetry_endpoint, name='telemetry'),
    
    # ========== DASHBOARD APIs (JSON endpoints) ==========
    # Display APIs
    path('api/v1/dashboard/stats/', dashboard_views.stats_api, name='stats_api'),
    path('api/v1/dashboard/alerts/', dashboard_views.alerts_list_api, name='alerts_list_api'),
    path('api/v1/dashboard/alerts/<str:alert_id>/', dashboard_views.alert_detail_api, name='alert_detail_api'),
    
    # SOC Action APIs
    path('api/v1/dashboard/alerts/<str:alert_id>/status/', dashboard_views.alert_update_status, name='alert_status'),
    path('api/v1/dashboard/alerts/<str:alert_id>/assign/', dashboard_views.alert_assign, name='alert_assign'),
    path('api/v1/dashboard/alerts/<str:alert_id>/note/', dashboard_views.alert_add_note, name='alert_note'),
    path('api/v1/dashboard/rules/<str:rule_id>/toggle/', dashboard_views.rule_toggle, name='rule_toggle'),
    path('dashboard/alerts/', dashboard_views.alerts_list_view, name='alerts_list'),

    # Investigation API
    path('api/v1/dashboard/alerts/<str:alert_id>/timeline/', dashboard_views.alert_timeline, name='alert_timeline'),
    
    # ========== DASHBOARD PAGES (HTML views) ==========
    path('dashboard/', dashboard_views.dashboard_home, name='dashboard_home'),
    path('dashboard/alerts/<str:alert_id>/', dashboard_views.alert_detail_view, name='alert_detail'),
    path('dashboard/rules/', dashboard_views.rules_view, name='rules'),
    path('dashboard/events/', dashboard_views.events_view, name='events'),
]
