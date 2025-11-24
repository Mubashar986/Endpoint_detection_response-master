# your_app/templatetags/edr_filters.py

from django import template
from datetime import datetime, timezone as dt_timezone
import pytz
import time

register = template.Library()

@register.filter
def to_local_time(unix_timestamp):
    """Convert Unix timestamp to local timezone string."""
    if unix_timestamp is None:
        return "N/A"
    try:
        utc_dt = datetime.fromtimestamp(unix_timestamp, tz=dt_timezone.utc)
        # Auto-detect timezone from Django or system
        try:
            from django.utils import timezone
            local_tz = timezone.get_current_timezone()
        except Exception:
            local_tz = pytz.UTC
        local_dt = utc_dt.astimezone(local_tz)
        return local_dt.strftime("%Y-%m-%d %H:%M:%S")
    except Exception:
        return "Invalid timestamp"
