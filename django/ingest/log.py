import logging


class RbTagFilter(logging.Filter):
    """Derive the [TAG] shown in log lines from the logger name.

    rb4107.mqtt -> MQTT, rb4107.telemetry -> TELEMETRY, anything else -> DJANGO.
    """

    def filter(self, record: logging.LogRecord) -> bool:
        parts = record.name.split(".", 1)
        record.rb_tag = parts[1].upper() if parts[0] == "rb4107" and len(parts) == 2 else "DJANGO"
        return True
