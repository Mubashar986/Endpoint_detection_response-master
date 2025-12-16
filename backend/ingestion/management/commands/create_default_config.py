"""
Django management command to create default agent configuration.

Usage:
    python manage.py create_default_config
"""

from django.core.management.base import BaseCommand
from ingestion.models_mongo import AgentConfig


class Command(BaseCommand):
    help = 'Creates the default agent configuration if it does not exist'

    def handle(self, *args, **options):
        # Check if default already exists
        existing = AgentConfig.objects.filter(config_id="default").first()
        
        if existing:
            self.stdout.write(self.style.WARNING(
                f'Default config already exists (version {existing.version})'
            ))
            return
        
        # Create default configuration
        default_config = AgentConfig(
            config_id="default",
            name="Default Policy",
            is_default=True,
            version=1,
            config_json={
                "_config_version": 1,
                "communication": {
                    "heartbeat_interval_seconds": 30,
                    "batch_size": 100,
                    "command_poll_interval_seconds": 5,
                    "enable_http_polling": True,
                    "compression_level": 3
                },
                "modules": {
                    "file_monitor": {"enabled": True},
                    "process_monitor": {"enabled": True},
                    "network_monitor": {"enabled": True},
                    "registry_monitor": {"enabled": False}
                },
                "filters": {
                    "excluded_event_ids": [],
                    "excluded_processes": ["chrome.exe", "firefox.exe"],
                    "sampling_rate_percent": 100
                },
                "performance": {
                    "buffer_max_size": 1000,
                    "max_cpu_percent": 10
                },
                "security": {
                    "response_actions": {"enabled": True},
                    "auto_update": {"enabled": True},
                    "agent_mode": "active"
                }
            },
            created_by="system"
        )
        default_config.save()
        
        self.stdout.write(self.style.SUCCESS(
            'Successfully created default agent configuration'
        ))
        self.stdout.write(f'  Config ID: {default_config.config_id}')
        self.stdout.write(f'  Version: {default_config.version}')
