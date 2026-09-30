"""Monitoring data only; no actuator or reset commands."""
from django.db import models


class Device(models.Model):
    device_id = models.CharField(max_length=128, unique=True)
    latest = models.JSONField(default=dict)
    field_updated_at = models.JSONField(default=dict)
    last_seen_at = models.DateTimeField(null=True, blank=True)
    last_received_at = models.DateTimeField(null=True, blank=True)
    last_snapshot_source_at = models.DateTimeField(null=True, blank=True)
    reported_online = models.BooleanField(null=True)


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
    message = models.OneToOneField(InboundMessage, on_delete=models.CASCADE)
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
