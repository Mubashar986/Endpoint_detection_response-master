from celery import Celery
import os

os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings')

app = Celery('edr_server')
app.config_from_object('django.conf:settings', namespace='CELERY')
app.autodiscover_tasks()
