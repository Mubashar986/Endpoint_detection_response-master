from django.core.management.base import BaseCommand
from ingestion.models_mongo import AgentConfig

class Command(BaseCommand):
    help = 'Creates demo agent configuration profiles'

    def handle(self, *args, **options):
        # 1. DDoS Detection Profile
        # Focused on Network and DNS
        ddos_config, created = AgentConfig.objects.update_or_create(
            config_id="ddos-detector",
            defaults={
                "name": "DDoS Detection Profile",
                "is_default": False,
                "version": 1,
                "created_by": "system",
                "config_json": {
                    "_config_version": 1,
                    "modules": {
                        "process_monitor": {"enabled": False},
                        "network_monitor": {"enabled": True},
                        "file_monitor": {"enabled": False},
                        "registry_monitor": {"enabled": False},
                        "dns_monitor": {"enabled": True}
                    },
                    "communication": {
                        "heartbeat_interval_seconds": 10,  # Fast updates for demo
                        "batch_size": 50,
                        "command_poll_interval_seconds": 5,
                        "enable_http_polling": True
                    }
                }
            }
        )
        self.stdout.write(self.style.SUCCESS(f'Processed profile: {ddos_config.name}'))

        # 2. File Integrity Profile
        # Focused on File and Registry changes
        file_config, created = AgentConfig.objects.update_or_create(
            config_id="file-integrity",
            defaults={
                "name": "File Integrity Monitor",
                "is_default": False,
                "version": 1,
                "created_by": "system",
                "config_json": {
                    "_config_version": 1,
                    "modules": {
                        "process_monitor": {"enabled": False},
                        "network_monitor": {"enabled": False},
                        "file_monitor": {"enabled": True},
                        "registry_monitor": {"enabled": True},
                        "dns_monitor": {"enabled": False}
                    },
                     "communication": {
                        "heartbeat_interval_seconds": 30,
                        "batch_size": 100,
                        "command_poll_interval_seconds": 10,
                        "enable_http_polling": True
                    }
                }
            }
        )
        self.stdout.write(self.style.SUCCESS(f'Processed profile: {file_config.name}'))

        # 3. Full Monitoring Profile (ensure it exists)
        full_config, created = AgentConfig.objects.update_or_create(
            config_id="full-monitoring",
            defaults={
                "name": "Full Monitoring Profile",
                "is_default": False,
                "version": 1,
                "created_by": "system",
                "config_json": {
                    "_config_version": 1,
                    "modules": {
                        "process_monitor": {"enabled": True},
                        "network_monitor": {"enabled": True},
                        "file_monitor": {"enabled": True},
                        "registry_monitor": {"enabled": True},
                        "dns_monitor": {"enabled": True}
                    },
                     "communication": {
                        "heartbeat_interval_seconds": 10,
                        "batch_size": 100,
                        "command_poll_interval_seconds": 5,
                        "enable_http_polling": True
                    }
                }
            }
        )
        self.stdout.write(self.style.SUCCESS(f'Processed profile: {full_config.name}'))
