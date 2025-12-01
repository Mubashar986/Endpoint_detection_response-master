
from django.urls import path
from . import views, dashboard_views, admin_views
from . import command_views

app_name = 'ingestion'

from django.contrib.auth import views as auth_views

urlpatterns = [
    # ========== CUSTOM ADMIN INTERFACE ==========
    path('dashboard/admin/', admin_views.admin_dashboard, name='admin_dashboard'),
    path('dashboard/admin/users/', admin_views.user_list, name='admin_user_list'),
    path('dashboard/admin/users/create/', admin_views.user_create, name='admin_user_create'),
    path('dashboard/admin/users/<int:user_id>/edit/', admin_views.user_edit, name='admin_user_edit'),
    path('dashboard/admin/users/<int:user_id>/delete/', admin_views.user_delete, name='admin_user_delete'),
    path('dashboard/admin/rules/create/', admin_views.rule_builder, name='admin_rule_create'),
    path('dashboard/admin/rules/<str:rule_id>/edit/', admin_views.rule_builder, name='admin_rule_edit'),

    # ========== PAGE VIEWS (HTML) ==========
    path('accounts/login/', auth_views.LoginView.as_view(), name='login'),
    path('accounts/logout/', auth_views.LogoutView.as_view(next_page='ingestion:login'), name='logout'),

    # ========== EXISTING TELEMETRY API ==========
    path('api/v1/telemetry/', views.telemetry_endpoint, name='telemetry'),
    
    # ========== RESPONSE ACTION APIs (New) ==========
    # Agent Communication
    path('api/v1/commands/poll/', command_views.poll_commands, name='poll_commands'),
    path('api/v1/commands/result/<str:command_id>/', command_views.report_command_result, name='report_command_result'),
    
    # Dashboard Triggers
    path('api/v1/response/kill_process/', command_views.trigger_kill_process, name='trigger_kill_process'),
    path('api/v1/response/isolate_host/', command_views.trigger_isolate_host, name='trigger_isolate_host'),
    path('api/v1/response/deisolate_host/', command_views.trigger_deisolate_host, name='trigger_deisolate_host'),
    
    # ========== DASHBOARD APIs (JSON endpoints) ==========
    # Display APIs
    path('api/v1/dashboard/stats/', dashboard_views.stats_api, name='stats_api'),
    path('api/v1/dashboard/alerts/', dashboard_views.alerts_list_api, name='alerts_list_api'),
    path('api/v1/dashboard/alerts/<str:alert_id>/', dashboard_views.alert_detail_api, name='alert_detail_api'),
    
    # SOC Action APIs
    path('api/v1/dashboard/alerts/<str:alert_id>/status/', dashboard_views.alert_update_status, name='alert_status'),
    path('api/v1/dashboard/alerts/<str:alert_id>/assign/', dashboard_views.alert_assign, name='alert_assign'),
    path('api/v1/dashboard/alerts/<str:alert_id>/assign/', dashboard_views.alert_assign, name='alert_assign'),
    path('api/v1/dashboard/alerts/<str:alert_id>/note/', dashboard_views.alert_add_note, name='alert_note'),
    path('api/v1/alerts/bulk/', dashboard_views.bulk_alert_action, name='bulk_alert_action'), # P0-004 Bulk Ops
    path('api/v1/dashboard/rules/<str:rule_id>/toggle/', dashboard_views.rule_toggle, name='rule_toggle'),
    path('dashboard/alerts/', dashboard_views.alerts_list_view, name='alerts_list'),
    path('dashboard/response-actions/', dashboard_views.response_actions_list, name='response_actions_list'),

    # Investigation API
    path('api/v1/dashboard/alerts/<str:alert_id>/timeline/', dashboard_views.alert_timeline, name='alert_timeline'),
    
    # Global Search API (P0-013)
    path('api/v1/search/', dashboard_views.global_search, name='global_search'),
    
    # ========== DASHBOARD PAGES (HTML views) ==========
    path('dashboard/', dashboard_views.dashboard_home, name='dashboard_home'),
    path('dashboard/alerts/<str:alert_id>/', dashboard_views.alert_detail_view, name='alert_detail'),
    path('dashboard/rules/', dashboard_views.rules_view, name='rules'),
    path('dashboard/events/', dashboard_views.events_view, name='events'),
]
