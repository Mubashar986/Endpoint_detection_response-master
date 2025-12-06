# ingestion/routing.py
"""
WebSocket URL Routing for EDR Agents
====================================
Defines the WebSocket URL patterns for the ingestion app.

URL Pattern:
    ws://server:8000/ws/agent/  →  AgentConsumer
"""

from django.urls import re_path
from . import consumers

# WebSocket URL patterns
# These are separate from HTTP URL patterns in urls.py
websocket_urlpatterns = [
    # Agent WebSocket endpoint
    # Matches: ws://server:8000/ws/agent/
    re_path(r'ws/agent/$', consumers.AgentConsumer.as_asgi()),
]
