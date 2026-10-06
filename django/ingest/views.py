import json
import math
import re
from datetime import timedelta
from django.conf import settings
from django.http import JsonResponse
from django.shortcuts import get_object_or_404
from django.utils import timezone
from django.utils.dateparse import parse_datetime
from django.views.decorators.cache import never_cache
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_GET, require_POST

from . import commands, validation
from .models import Device, InboundMessage, Reading, SafetyEvent, ThermalFrame, WorkerStatus
from .locations import (active_alerts, aggregate_stalls, classify_display_state,
                        fleet_summary, load_location_catalog, location_for)


def age(at, now):
    return max(0, (now - at).total_seconds()) if at else None


def worker_info(now):
    worker = WorkerStatus.objects.filter(name="mqtt").first()
    fresh = bool(worker and worker.heartbeat_at and age(worker.heartbeat_at, now) <= settings.WORKER_STALE_SECONDS)
    return {"connected": bool(fresh and worker.connected), "running": fresh,
            "mode": worker.mode if worker else "mqtt",
            "heartbeat_at": worker.heartbeat_at if worker else None,
            "last_message_at": worker.last_message_at if worker else None,
            "error": worker.error if worker else "Worker has not started"}


def serialize(device, now, worker, catalog=None):
    seen_age = age(device.last_seen_at, now)
    if device.reported_online is False:
        connection = "offline"
    elif not worker["connected"]:
        connection = "monitor_unavailable"
    elif seen_age is None:
        connection = "unknown"
    elif seen_age > settings.DEVICE_STALE_SECONDS:
        connection = "stale"
    else:
        connection = "online"
    result = {"device_id": device.device_id, "connection": connection,
            "last_seen_at": device.last_seen_at, "last_received_at": device.last_received_at,
            "age_seconds": seen_age, "reported_online": device.reported_online,
            "values": device.latest, "field_updated_at": device.field_updated_at,
            "field_age_seconds": {key: age(parse_datetime(value) if value else None, now)
                                  for key, value in device.field_updated_at.items()},
            "stale_after_seconds": settings.DEVICE_STALE_SECONDS,
            "location": location_for(device.device_id, catalog)}
    result["display_state"] = classify_display_state(result)
    return result


@require_GET
@never_cache
def health(request):
    now = timezone.now()
    return JsonResponse({"server_time": now, "worker": worker_info(now),
                         "invalid_messages_24h": InboundMessage.objects.filter(
                             outcome="invalid", received_at__gte=now - timedelta(days=1)).count()})


@require_GET
@never_cache
def devices(request):
    now = timezone.now()
    worker = worker_info(now)
    catalog = load_location_catalog()
    serialized = [serialize(device, now, worker, catalog) for device in Device.objects.order_by("device_id")]
    stalls = aggregate_stalls(serialized)
    return JsonResponse({"server_time": now, "worker": worker, "catalog": catalog["meta"],
                         "summary": fleet_summary(stalls), "alerts": active_alerts(stalls),
                         "stalls": stalls, "devices": serialized})


@require_GET
@never_cache
def latest(request, device_id):
    now = timezone.now()
    catalog = load_location_catalog()
    return JsonResponse(serialize(get_object_or_404(Device, device_id=device_id), now, worker_info(now), catalog))


def query_bounds(request):
    try:
        hours = float(request.GET.get("hours", "1"))
        limit = int(request.GET.get("limit", "360"))
        if not math.isfinite(hours) or not 0 < hours <= 168 or not 1 <= limit <= 2000:
            raise ValueError()
        return hours, limit
    except (ValueError, OverflowError):
        raise ValueError("hours must be >0 and <=168; limit must be 1..2000") from None


@require_GET
@never_cache
def history(request, device_id):
    device = get_object_or_404(Device, device_id=device_id)
    try:
        hours, limit = query_bounds(request)
    except ValueError as error:
        return JsonResponse({"error": str(error)}, status=400)
    rows = list(Reading.objects.filter(device=device, received_at__gte=timezone.now()-timedelta(hours=hours),
                                      temperature_c__isnull=False).order_by("-received_at", "-id")
                .values("received_at", "source_at", "temperature_c")[:limit+1])
    truncated = len(rows) > limit
    return JsonResponse({"device_id": device_id, "clock": "server_received_at", "truncated": truncated,
                         "points": list(reversed(rows[:limit]))})


@require_GET
@never_cache
def events(request, device_id):
    device = get_object_or_404(Device, device_id=device_id)
    try:
        hours, limit = query_bounds(request)
    except ValueError as error:
        return JsonResponse({"error": str(error)}, status=400)
    rows = list(SafetyEvent.objects.filter(
        device=device, received_at__gte=timezone.now()-timedelta(hours=hours)
    ).order_by("-received_at", "-id").values(
        "received_at", "source_at", "event_type", "detail")[:limit+1])
    return JsonResponse({"truncated": len(rows) > limit, "events": rows[:limit]})


NODE_NAME = re.compile(r"^node_\d{2,3}$")


@csrf_exempt  # lab prototype: the API has no login (docs/c4002_tuning.md)
@require_POST
def c4002_command(request, device_id, node):
    """Send one C4002 tuning command to a presence node. The answer arrives
    later as sensors/<node>/c4002_config and shows up in /latest/."""
    device = get_object_or_404(Device, device_id=device_id)
    sensor = device.latest.get("sensors", {}).get(node)
    if not NODE_NAME.match(node) or not sensor or sensor.get("role") != "presence":
        return JsonResponse({"error": f"{node} is not a presence node of {device_id}"}, status=404)
    try:
        body = json.loads(request.body or b"{}")
        command = commands.build_c4002_command(device_id, body)
    except (ValueError, commands.CommandError) as error:
        return JsonResponse({"error": str(error)}, status=400)
    topic = commands.command_topic(node)
    try:
        commands.publish(topic, command)
    except commands.BrokerUnavailable as error:
        return JsonResponse({"error": str(error)}, status=503)
    return JsonResponse({"request_id": command["request_id"], "topic": topic, "command": command}, status=202)


@csrf_exempt  # lab prototype: the API has no login (docs/remote_reset.md)
@require_POST
def reset(request, device_id):
    """Send the operator reset to a controller. It releases a latched SHUTDOWN;
    the dashboard sees the result as reset_required going false in /latest/."""
    device = get_object_or_404(Device, device_id=device_id)
    try:
        command = commands.build_reset_command(device.device_id)
    except commands.CommandError as error:
        return JsonResponse({"error": str(error)}, status=400)
    topic = commands.controller_command_topic()
    try:
        commands.publish(topic, command)
    except commands.BrokerUnavailable as error:
        return JsonResponse({"error": str(error)}, status=503)
    return JsonResponse({"request_id": command["request_id"], "topic": topic, "command": command}, status=202)


LIVE_WINDOW_SECONDS = 60
LIVE_MAX_SAMPLES = 400


@require_GET
@never_cache
def c4002_live(request, device_id, node):
    """Raw C4002 results of one presence node (sensors/<node>/c4002_live), oldest
    first: the last minute, or only the ones after ?after=<id> when given."""
    device = get_object_or_404(Device, device_id=device_id)
    if not NODE_NAME.match(node):
        return JsonResponse({"error": "bad node name"}, status=404)
    try:
        after = int(request.GET.get("after", "0"))
    except ValueError:
        return JsonResponse({"error": "after must be an integer"}, status=400)
    # Exact topic and received_at ordering let SQLite use inbound_device_topic_received
    # instead of walking every row of the device.
    topic = f"{validation.topic_prefix(settings.RB4107_MQTT['TOPIC'])}/sensors/{node}/c4002_live"
    rows = InboundMessage.objects.filter(
        device=device, topic=topic, outcome="accepted",
        received_at__gte=timezone.now() - timedelta(seconds=LIVE_WINDOW_SECONDS), id__gt=after,
    ).order_by("-received_at", "-id").values("id", "received_at", "raw")[:LIVE_MAX_SAMPLES]
    samples = []
    for row in reversed(list(rows)):
        data = json.loads(row["raw"])
        samples.append({"id": row["id"], "received_at": row["received_at"],
                        **{key: data.get(key) for key in ("node_uptime_ms", "results", "target", "gate_size_cm",
                                                          "presence_gates", "presence", "motion",
                                                          "calibration_remaining_s")}})
    return JsonResponse({"node": node, "window_seconds": LIVE_WINDOW_SECONDS,
                         "last_id": samples[-1]["id"] if samples else after, "samples": samples})


@require_GET
@never_cache
def thermal_frame(request, device_id):
    """The latest heat-map picture of each thermal node of one controller (display only)."""
    device = get_object_or_404(Device, device_id=device_id)
    now = timezone.now()
    frames = [{"sensor_node": row.sensor_node, "received_at": row.received_at, "source_at": row.source_at,
               "age_seconds": age(row.received_at, now), **row.frame}
              for row in ThermalFrame.objects.filter(device=device).order_by("sensor_node")]
    return JsonResponse({"device_id": device_id, "stale_after_seconds": settings.DEVICE_STALE_SECONDS,
                         "frames": frames})
