from mongoengine import Document, StringField, DateTimeField, DictField, BooleanField
from datetime import datetime, timezone


class TelemetryEvent(Document):
    event_id = StringField(required=True, unique=True)
    agent_id = StringField(required=True)
    event_type = StringField(required=True)
    timestamp = DateTimeField(required=True)  # Will store UTC datetime
    severity = StringField(required=True)
    raw_data = DictField(required=True)
    processed = BooleanField(default=False)
    created_at = DateTimeField(default=lambda: datetime.now(timezone.utc))
    
    meta = {
        'collection': 'telemetry_events',
        'indexes': ['agent_id', 'event_type', 'timestamp', 'created_at']
    }
    
    def __str__(self):
        return f"Event {self.event_id} from {self.agent_id} at {self.timestamp}"
