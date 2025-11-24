from django.core.management.base import BaseCommand
from django.contrib.auth.models import Group, Permission
from django.contrib.contenttypes.models import ContentType
from ingestion.models import TelemetryEvent  # Just to get app_label if needed

class Command(BaseCommand):
    help = 'Initialize SOC Roles and Permissions'

    def handle(self, *args, **options):
        # 0. Create Permissions manually (since MongoDB models don't auto-generate them)
        content_type = ContentType.objects.get_for_model(Permission)  # Use a dummy content type or create one
        
        # Define permissions to create
        custom_permissions = [
            ('can_kill_process', 'Can kill process'),
            ('can_isolate_host', 'Can isolate host'),
            ('view_alert', 'Can view alert'),
            ('change_alert', 'Can change alert'),
            ('delete_alert', 'Can delete alert'),
        ]

        for codename, name in custom_permissions:
            Permission.objects.get_or_create(
                codename=codename,
                content_type=content_type,
                defaults={'name': name}
            )
            self.stdout.write(f'Checked permission: {codename}')

        # 1. Define Roles (Admin is now just Superuser)
        roles = {
            'SOC Analyst': [
                'auth.can_kill_process', # Note: app_label will be 'auth' if we use Permission content_type
                'auth.can_isolate_host',
                'auth.view_alert',
                'auth.change_alert',
            ],
            'SOC Viewer': [
                'auth.view_alert',
            ]
        }

        for role_name, perms in roles.items():
            group, created = Group.objects.get_or_create(name=role_name)
            if created:
                self.stdout.write(self.style.SUCCESS(f'Created group: {role_name}'))
            else:
                self.stdout.write(f'Group exists: {role_name}')

            # Assign permissions
            for perm_code in perms:
                try:
                    app_label, codename = perm_code.split('.')
                    permission = Permission.objects.get(content_type__app_label=app_label, codename=codename)
                    group.permissions.add(permission)
                    self.stdout.write(f'  + Added {perm_code}')
                except Permission.DoesNotExist:
                    self.stdout.write(self.style.WARNING(f'  ! Permission not found: {perm_code}'))

        self.stdout.write(self.style.SUCCESS('RBAC Initialization Complete'))
