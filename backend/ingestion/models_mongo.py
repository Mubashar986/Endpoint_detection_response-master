from mongoengine import Document, StringField, DictField, DateTimeField, IntField
from datetime import datetime
import uuid

class PendingCommand(Document):
    """
    Command queue - stores commands waiting to be executed by agents.
    This implements the "HTTP Polling" pattern where agents poll for these records.
    """
    # Unique ID for the command (UUID)
    command_id = StringField(required=True, unique=True, default=lambda: str(uuid.uuid4()))
    
    # Target endpoint identifier
    agent_id = StringField(required=True)
    
    # Command details
    command_type = StringField(required=True)  # e.g., 'kill_process', 'isolate_host'
    parameters = DictField()  # e.g., {'pid': 1234}
    
    # Status tracking
    # Options: 'new', 'in_progress', 'completed', 'failed', 'timeout'
    status = StringField(required=True, default='new')
    
    # Result from agent
    result = DictField()  # e.g., {'status': 'success', 'message': 'Process killed'}
    
    # Metadata
    issued_by = StringField(required=True)  # Analyst email/username
    related_alert_id = StringField()  # Optional link to an alert
    
    # Timestamps
    created_at = DateTimeField(default=datetime.utcnow)
    expires_at = DateTimeField()  # Auto-delete/timeout after X minutes
    completed_at = DateTimeField()
    
    meta = {
        'collection': 'pending_commands',
        'indexes': [
            'agent_id',
            'status',
            'expires_at'  # For cleanup/TTL
        ]
    }

class ResponseAction(Document):
    """
    Audit trail for all response actions taken.
    This is an append-only log for compliance and history.
    """
    action_id = StringField(required=True, unique=True, default=lambda: str(uuid.uuid4()))
    
    # Who did what
    user = StringField(required=True)  # Analyst email
    action_type = StringField(required=True)  # 'kill_process', 'isolate_host'
    target_agent = StringField(required=True)
    
    # Context
    command_id = StringField()  # Link to PendingCommand
    alert_id = StringField()
    reason = StringField()  # Why was this action taken?
    
    # Outcome
    status = StringField(default='initiated')  # 'initiated', 'success', 'failed'
    result_summary = StringField()
    
    timestamp = DateTimeField(default=datetime.utcnow)
    
    meta = {
        'collection': 'response_actions',
        'indexes': [
            '-timestamp',
            'user',
            'target_agent'
        ]
    }
