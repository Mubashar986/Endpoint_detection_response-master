"""
ASGI config for edr_server project.

It exposes the ASGI callable as a module-level variable named ``application``.

This file now handles both HTTP and WebSocket protocols using Django Channels.

For more information on this file, see
https://docs.djangoproject.com/en/5.2/howto/deployment/asgi/
"""

import os
from django.core.asgi import get_asgi_application

# Set the settings module BEFORE importing anything else
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings')

# Initialize Django ASGI application early to ensure settings are loaded
django_asgi_app = get_asgi_application()

# Import Channels components AFTER Django is initialized
from channels.routing import ProtocolTypeRouter, URLRouter
from channels.auth import AuthMiddlewareStack
import ingestion.routing

# ProtocolTypeRouter routes connections based on protocol type (HTTP or WebSocket)
application = ProtocolTypeRouter({
    # HTTP requests go to standard Django views
    "http": django_asgi_app,
    
    # WebSocket requests go to our consumer via the URL router
    # AuthMiddlewareStack adds Django session/user info to the connection scope
    "websocket": AuthMiddlewareStack(
        URLRouter(
            ingestion.routing.websocket_urlpatterns
        )
    ),
})
