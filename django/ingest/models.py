"""Monitoring data only; no actuator or reset commands. (C4002 tuning commands
are sent straight to MQTT and their answers stored in Device.latest.)"""
from django.db import models


class Device(models.Model):
    device_id = models.CharField(max_length=128, unique=True)
    latest = models.JSONField(default=dict)
    field_updated_at = models.JSONField(default=dict)
    last_seen_at = models.DateTimeField(null=True, blank=True)
    last_received_at = models.DateTimeField(null=True, blank=True)
    last_snapshot_source_at = models.DateTimeField(null=True, blank=True)
    reported_online = models.BooleanField(null=True)
    # Firmware boot_id and the highest sequence seen in that boot: orders
    # messages even while the controller's clock is unset.
    boot_id = models.CharField(max_length=32, blank=True)
    last_sequence = models.PositiveBigIntegerField(null=True)
    # From the newest controller/log message: the log levels in force ({"*": default, tag: level})
    # and how many lines the controller lost since it booted.
    log_levels = models.JSONField(default=dict)
    log_dropped = models.PositiveBigIntegerField(null=True)
    log_received_at = models.DateTimeField(null=True)


class InboundMessage(models.Model):
    device = models.ForeignKey(Device, null=True, on_delete=models.SET_NULL)
    message_id = models.CharField(max_length=64, blank=True, db_index=True)
    received_at = models.DateTimeField(db_index=True)
    source_at = models.DateTimeField(null=True)
    topic = models.CharField(max_length=1024)
    raw = models.TextField()
    retained = models.BooleanField(default=False)
    qos = models.PositiveSmallIntegerField(default=0)
    outcome = models.CharField(max_length=24, default="accepted", db_index=True)
    error = models.TextField(blank=True)

    class Meta:
        # The live radar view reads one topic's last minute; without this index
        # SQLite walks every row of the device (slower as the database grows).
        indexes = [models.Index(fields=["device", "topic", "received_at"], name="inbound_device_topic_received")]


class Reading(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    message = models.OneToOneField(InboundMessage, on_delete=models.CASCADE)
    received_at = models.DateTimeField()
    source_at = models.DateTimeField(null=True)
    values = models.JSONField(default=dict)
    temperature_c = models.FloatField(null=True)

    class Meta:
        indexes = [models.Index(fields=["device", "received_at"])]


class SafetyEvent(models.Model):
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    # Usually one event per message; a message that reveals a controller
    # restart can also carry a "controller_restarted" event.
    message = models.ForeignKey(InboundMessage, on_delete=models.CASCADE)
    received_at = models.DateTimeField()
    source_at = models.DateTimeField(null=True)
    event_type = models.CharField(max_length=80)
    detail = models.JSONField(default=dict)

    class Meta:
        indexes = [models.Index(fields=["device", "received_at"])]


class WorkerStatus(models.Model):
    name = models.CharField(max_length=80, unique=True, default="mqtt")
    connected = models.BooleanField(default=False)
    heartbeat_at = models.DateTimeField(null=True)
    last_message_at = models.DateTimeField(null=True)
    error = models.TextField(blank=True)
    mode = models.CharField(max_length=24, default="mqtt")


class ThermalFrame(models.Model):
    """The latest heat-map picture per thermal node (display only, no history kept)."""
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    sensor_node = models.CharField(max_length=32)
    received_at = models.DateTimeField()
    source_at = models.DateTimeField(null=True)
    frame = models.JSONField(default=dict)  # the message's "frame" object, as sent

    class Meta:
        constraints = [models.UniqueConstraint(fields=["device", "sensor_node"], name="one_frame_per_node")]


class LogLine(models.Model):
    """One line the controller logged (controller/log), for the dashboard's Logs tab."""
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    received_at = models.DateTimeField()
    boot_id = models.CharField(max_length=32, blank=True)
    uptime_ms = models.PositiveBigIntegerField()
    level = models.CharField(max_length=1)  # E W I D V
    tag = models.CharField(max_length=16)
    msg = models.CharField(max_length=128)

    class Meta:
        indexes = [models.Index(fields=["device", "id"], name="logline_device_id"),
                   models.Index(fields=["received_at"], name="logline_received")]


class Incident(models.Model):
    """One unattended-cooking episode of a controller, for the per-stall history.

    Django opens one when the reported state first leaves MONITORING for
    UNATTENDED, WARNING or SHUTDOWN, raises `peak` as it escalates and closes it
    when the state is back to MONITORING or IDLE (or the controller restarts).
    Display only: the controller never reads these. Never pruned.
    """
    PEAKS = ("unattended", "warning", "shutdown")
    device = models.ForeignKey(Device, on_delete=models.CASCADE)
    started_at = models.DateTimeField()
    ended_at = models.DateTimeField(null=True, blank=True)
    peak = models.CharField(max_length=16, default="unattended")
    last_state = models.CharField(max_length=16, default="unattended")
    warning_at = models.DateTimeField(null=True, blank=True)
    shutdown_at = models.DateTimeField(null=True, blank=True)
    end_reason = models.CharField(max_length=40, blank=True)
    max_temp_c = models.FloatField(null=True, blank=True)
    demo = models.BooleanField(default=False)  # made up by seed_incident_history

    class Meta:
        indexes = [models.Index(fields=["device", "started_at"], name="incident_device_started")]
