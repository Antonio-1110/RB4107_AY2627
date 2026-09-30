"""Transactional persistence after firmware JSON-schema validation."""
import hashlib
import json

from django.db import transaction
from django.utils import timezone
from django.utils.dateparse import parse_datetime

from .models import Device, InboundMessage, Reading, SafetyEvent, WorkerStatus
from .normalization import normalize


def source_time(data):
    value = data.get("timestamp")
    try:
        parsed = parse_datetime(value) if isinstance(value, str) else None
        return parsed if parsed and timezone.is_aware(parsed) else None
    except (ValueError, TypeError):
        return None


def merge(target, patch, stamps, stamp, *, retained=False, prefix=""):
    for key, value in patch.items():
        path = f"{prefix}.{key}" if prefix else key
        if isinstance(value, dict):
            if not isinstance(target.get(key), dict):
                target[key] = {}
            merge(target[key], value, stamps, stamp, retained=retained, prefix=path)
        elif retained and key in target:
            continue  # Retained replay must never overwrite a live/last-known value.
        else:
            old = stamps.get(path)
            if old and stamp and parse_datetime(old) > parse_datetime(stamp):
                continue
            target[key] = value
        if not retained or path not in stamps:
            stamps[path] = stamp


def track_boot(device, message, row, now, source):
    """Order live messages by (boot_id, sequence), which works without a wall clock.

    Returns "live", "duplicate" (sequence already seen in this boot) or
    "historical" (older than the newest message of this boot). A new boot_id
    means the controller restarted, which is logged as an event.
    """
    data = message.data
    boot_id, sequence = data.get("boot_id"), data.get("sequence")
    if message.retained or not boot_id or not isinstance(sequence, int):
        return "live"
    if device.boot_id == boot_id:
        if device.last_sequence is not None and sequence <= device.last_sequence:
            return "duplicate" if sequence == device.last_sequence else "historical"
        device.last_sequence = sequence
        return "live"
    previous = device.boot_id
    device.boot_id, device.last_sequence = boot_id, sequence
    if previous:
        SafetyEvent.objects.create(device=device, message=row, received_at=now, source_at=source,
                                   event_type="controller_restarted",
                                   detail={"previous_boot_id": previous, "boot_id": boot_id,
                                           "note": f"New boot {boot_id} (was {previous})"})
    return "live"


def persist(message, received_at=None):
    now = received_at or timezone.now()
    data = message.data
    raw = json.dumps(data, sort_keys=True, separators=(",", ":"), allow_nan=False)
    fingerprint = hashlib.sha256(raw.encode()).hexdigest()
    source = source_time(data)
    with transaction.atomic():
        device, _ = Device.objects.get_or_create(device_id=data["controller_id"])
        device = Device.objects.select_for_update().get(pk=device.pk)
        # Only suppress MQTT DUP retransmissions. Identical data after a reboot
        # is legitimate: the firmware does not yet publish a boot/session ID.
        if message.duplicate:
            previous = InboundMessage.objects.filter(device=device, message_id=fingerprint).order_by("-id").first()
            if previous:
                return previous
        row = InboundMessage.objects.create(
            device=device, topic=message.topic, raw=raw, message_id=fingerprint,
            received_at=now, source_at=source, retained=message.retained, qos=message.qos,
            outcome="retained" if message.retained else "accepted",
        )
        device.last_received_at = now
        order = track_boot(device, message, row, now, source)
        if order != "live":
            row.outcome = order
            row.save(update_fields=["outcome"])
        if message.type == "controller_status":
            if not message.retained or data["online"] is False:
                device.reported_online = data["online"]
            # Retained online never proves the controller is alive.
            if not message.retained and data["online"]:
                device.last_seen_at = now
        elif message.type == "event":
            detail = dict(data)
            detail["note"] = " · ".join(str(part) for part in (
                data["safety"]["reason"], data["fault"]["name"] if data["fault"] else None
            ) if part)
            SafetyEvent.objects.create(device=device, message=row, received_at=now,
                source_at=source, event_type=data["event"], detail=detail)
            # Queued events are historical; never restore supply or replace the
            # controller's current full telemetry with an old event's state.
        else:
            fields = normalize(message)
            historical = order != "live" or bool(
                message.type == "telemetry" and source and
                device.last_snapshot_source_at and source < device.last_snapshot_source_at)
            stamp = None if message.retained else min(now, source or now).isoformat()
            if not historical:
                merge(device.latest, fields, device.field_updated_at, stamp, retained=message.retained)
                if not message.retained and message.type in {"telemetry", "heartbeat", "presence", "thermal"}:
                    device.last_seen_at = min(now, source or now)
                    device.reported_online = True
                if not message.retained and message.type == "telemetry":
                    device.last_snapshot_source_at = min(now, source) if source else None
            elif order == "live":
                row.outcome = "historical"
                row.save(update_fields=["outcome"])
            if message.type == "telemetry" and not message.retained and order != "duplicate":
                Reading.objects.create(device=device, message=row, received_at=now, source_at=source,
                                       values=fields, temperature_c=fields.get("temperature_c"))
        device.save()
        WorkerStatus.objects.filter(name="mqtt").update(last_message_at=now)
        return row


def record_rejection(topic, payload, reason, retained=False, qos=0):
    InboundMessage.objects.create(topic=topic[:1024], raw=payload[:8192].decode("utf-8", errors="replace"),
        received_at=timezone.now(), retained=retained, qos=qos, outcome="invalid", error=str(reason)[:1000])


def worker_status(connected=False, *, mode="mqtt", error=""):
    WorkerStatus.objects.update_or_create(name="mqtt", defaults={
        "connected": connected, "mode": mode, "error": error, "heartbeat_at": timezone.now(),
    })
