from rest_framework import serializers
from datetime import datetime, timezone


class TelemetrySerializer(serializers.Serializer):
    agent_id = serializers.CharField(required=True)
    event_id = serializers.CharField(required=True)
    event_type = serializers.ChoiceField(choices=['process', 'file', 'network'], required=True)
    timestamp = serializers.IntegerField(required=True)
    severity = serializers.CharField(required=True)
    version = serializers.CharField(required=True)
    host = serializers.DictField(required=True)
    process = serializers.DictField(required=False)
    file = serializers.DictField(required=False)
    network = serializers.DictField(required=False)
    
    def validate_timestamp(self, value):
        """
        Convert Unix timestamp (integer) to UTC datetime object.
        
        Args:
            value: Unix timestamp (seconds since epoch) as integer
            
        Returns:
            datetime: UTC datetime object
            
        Example:
            Input: 1485714600
            Output: datetime(2017, 1, 29, 19, 30, tzinfo=UTC)
        """
        try:
            if isinstance(value, int) or isinstance(value, float):
                # Convert Unix timestamp to UTC datetime
                # Using timezone.utc ensures we store in UTC
                dt = datetime.fromtimestamp(value, tz=timezone.utc)
                return dt
            elif isinstance(value, str):
                # Try parsing ISO format string
                dt = datetime.fromisoformat(value)
                # Ensure it has timezone info (set to UTC if naive)
                if dt.tzinfo is None:
                    dt = dt.replace(tzinfo=timezone.utc)
                return dt
            else:
                raise serializers.ValidationError(
                    "Timestamp must be Unix timestamp (int) or ISO string"
                )
        except (ValueError, OSError) as e:
            raise serializers.ValidationError(f"Invalid timestamp: {str(e)}")
    
    def validate(self, data):
        """
        Validate that event_type-specific data is present.
        """
        event_type = data.get('event_type')
        
        if event_type == 'process' and 'process' not in data:
            raise serializers.ValidationError(
                "process event requires 'process' field"
            )
        elif event_type == 'file' and 'file' not in data:
            raise serializers.ValidationError(
                "file event requires 'file' field"
            )
        elif event_type == 'network' and 'network' not in data:
            raise serializers.ValidationError(
                "network event requires 'network' field"
            )
        
        return data
