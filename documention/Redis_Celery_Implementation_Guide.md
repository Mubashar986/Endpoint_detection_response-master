# Redis & Celery Implementation Guide: EDR System

## 1. Overview
This document details the exact implementation of the **Async Ingestion Pipeline** in your EDR system (`Endpoint_detection_response-master`). It explains how Django, Redis, and Celery interact to handle high-throughput telemetry without blocking the API.

## 2. Architecture Diagram
```mermaid
graph LR
    A[EDR Agent] -->|POST /telemetry| B(Django API)
    B -->|telemetry_ingest.delay()| C{Redis Queue}
    C -->|JSON Task| D[Celery Worker]
    D -->|Save| E[(MongoDB)]
    D -->|Evaluate| F[Rule Engine]
```

## 3. Implementation Details

### A. Configuration (`backend/edr_server/settings.py`)
This file configures the connection to Redis and sets the execution mode for Windows.

```python
# Location: backend/edr_server/settings.py

# 1. Broker URL: Tells Celery where to send messages (Redis DB 0)
CELERY_BROKER_URL = 'redis://localhost:6379/0'

# 2. Result Backend: Tells Celery where to store task results (Success/Fail)
CELERY_RESULT_BACKEND = 'redis://localhost:6379/0'

# 3. Security: Only allow JSON serialization (prevents pickle code execution attacks)
CELERY_ACCEPT_CONTENT = ['json']
CELERY_TASK_SERIALIZER = 'json'

# 4. Windows Compatibility:
# 'gevent' pool is required for concurrency on Windows.
# 'solo' pool works but is single-threaded (bottleneck).
CELERY_WORKER_POOL = 'gevent'
CELERY_TASK_ALWAYS_EAGER = False  # False = Async (True would disable Celery for testing)
```

### B. App Initialization (`backend/edr_server/celery.py`)
This file creates the Celery application instance and loads the Django settings.

```python
# Location: backend/edr_server/celery.py
from celery import Celery
import os

# Set the default Django settings module
os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'edr_server.settings')

# Create the Celery app
app = Celery('edr_server')

# Load config from settings.py (namespace='CELERY' means it looks for CELERY_*)
app.config_from_object('django.conf:settings', namespace='CELERY')

# Auto-discover tasks in all installed apps (e.g., ingestion/tasks.py)
app.autodiscover_tasks()
```

### C. The Background Task (`backend/ingestion/tasks.py`)
This is the **Consumer**. It contains the logic that runs asynchronously.

```python
# Location: backend/ingestion/tasks.py
from celery import shared_task
from .models import TelemetryEvent
from .rule_engine import DetectionEngine

@shared_task
def telemetry_ingest(data):
    """
    Worker Logic:
    1. Receives raw JSON data.
    2. Saves it to MongoDB.
    3. Runs the Detection Engine.
    """
    try:
        # Step 1: Persistence
        event = TelemetryEvent(
            event_id=data['event_id'],
            agent_id=data['agent_id'],
            # ... other fields ...
            raw_data=data
        )
        event.save()

        # Step 2: Detection (CPU Intensive)
        # This is why we use Celery. Regex matching takes time.
        alerts = DetectionEngine.evaluate_event(event.event_id)
        
        return {'status': 'success', 'alerts_created': len(alerts)}
        
    except Exception as e:
        # Logs error but doesn't crash the main API
        logger.error(f"Failed to process telemetry: {str(e)}")
        raise
```

### D. The API Trigger (`backend/ingestion/views.py`)
This is the **Producer**. It receives the HTTP request and pushes the task to Redis.

```python
# Location: backend/ingestion/views.py
from .tasks import telemetry_ingest

@api_view(['POST'])
def telemetry_endpoint(request):
    # ... validation logic ...
    
    for event in events_to_process:
        # The .delay() method is the key.
        # It serializes the 'event' dict to JSON and pushes it to Redis.
        # It returns immediately, NOT waiting for the task to finish.
        task = telemetry_ingest.delay(event)
        
    return Response({'status': 'accepted'}, status=status.HTTP_201_CREATED)
```

## 4. How to Verify It Is Working

### 1. Check Redis
You can inspect the queue directly to see if messages are flowing.
```bash
# In terminal
redis-cli
> KEYS *
1) "celery"      # The list holding pending tasks
2) "celery-task-meta-..." # The results of completed tasks
> LLEN celery    # Check length of queue (should be 0 if worker is fast)
```

### 2. Check Celery Logs
When running the worker, you should see:
```text
[INFO/MainProcess] Task ingestion.tasks.telemetry_ingest[<UUID>] received
[WARNING/MainProcess] 🚨 Rule-based detection: 1 alerts created
[INFO/MainProcess] Task ingestion.tasks.telemetry_ingest[<UUID>] succeeded in 0.02s
```

## 5. Troubleshooting

*   **Task Pending Forever**: The worker is not running. Run `celery -A edr_server worker --pool=gevent --concurrency=10 -l info`.
*   **Connection Refused**: Redis is not running. Start it with `redis-server`.
*   **Serialization Error**: You passed a Django Model object to `.delay()`. Only pass simple types (dict, int, str).

## 6. Future Improvements
*   **Redis Caching**: Enable Redis as a cache backend (DB 1) to speed up the Dashboard.
*   **Flower**: Install `flower` to visualize the Celery queue in a web UI.
