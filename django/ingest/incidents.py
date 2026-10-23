"""Per-stall history of critical events (the industry partner's "what happened at this stall").

An incident is one unattended-cooking episode: the controller's reported state
leaves MONITORING for UNATTENDED, may climb to WARNING (buzzer) and SHUTDOWN
(supply cut), and ends when it is back to MONITORING or IDLE. It is built from
the controller's telemetry, which the firmware publishes on every state change,
so no firmware change is needed. Display only, and a demo-level record: the end
reason is inferred from the state the controller went to, not reported by it.
"""
import random
from datetime import timedelta

from django.db.models import Count, Max, Q
from django.utils import timezone

from .locations import load_location_catalog, location_for
from .models import Device, Incident

ACTIVE = {"unattended": 1, "warning": 2, "shutdown": 3}  # state -> escalation rank
RESTART = {"boot", "self_test"}
END_REASONS = {
    "person_returned": "Person returned",
    "heating_stopped": "Heating stopped",
    "operator_reset": "Reset after shutdown",
    "controller_restarted": "Controller restarted",
}


def open_incident(device):
    return Incident.objects.filter(device=device, ended_at__isnull=True).order_by("-started_at").first()


def close(incident, at, reason):
    incident.ended_at, incident.end_reason = at, reason
    incident.save()


def record_state(device, state, at, temperature_c=None):
    """Advance the device's incident for one live telemetry state (lower case, as normalised)."""
    incident = open_incident(device)
    if state in ACTIVE:
        if incident is None:
            incident = Incident(device=device, started_at=at, peak=state)
        if ACTIVE[state] > ACTIVE.get(incident.peak, 0):
            incident.peak = state
        if state in ("warning", "shutdown") and incident.warning_at is None:
            incident.warning_at = at  # a SHUTDOWN seen first still sounded the buzzer before it
        if state == "shutdown" and incident.shutdown_at is None:
            incident.shutdown_at = at
        if temperature_c is not None:
            incident.max_temp_c = max(temperature_c, incident.max_temp_c or temperature_c)
        incident.last_state = state
        incident.save()
    elif incident is None:
        return
    elif state in ("monitoring", "idle"):
        if incident.last_state == "shutdown":
            reason = "operator_reset"
        else:
            reason = "person_returned" if state == "monitoring" else "heating_stopped"
        close(incident, at, reason)
    elif state in RESTART:
        close(incident, at, "controller_restarted")
    # FAULT keeps the incident open: the controller can still go on to SHUTDOWN from it.


def controller_restarted(device, at):
    incident = open_incident(device)
    if incident:
        close(incident, at, "controller_restarted")


def serialize(incident, now):
    end = incident.ended_at or now
    return {"id": incident.id, "device_id": incident.device.device_id, "started_at": incident.started_at,
            "ended_at": incident.ended_at, "ongoing": incident.ended_at is None, "peak": incident.peak,
            "warning_at": incident.warning_at, "shutdown_at": incident.shutdown_at,
            "duration_seconds": max(0, (end - incident.started_at).total_seconds()),
            "end_reason": incident.end_reason or None,
            "end_reason_text": END_REASONS.get(incident.end_reason, "Ongoing" if not incident.ended_at else "—"),
            "max_temp_c": incident.max_temp_c, "demo": incident.demo}


def stall_history(now, stall_id=None, limit=500):
    """Counts for every stall, and the incidents of `stall_id` (newest first)."""
    catalog = load_location_catalog()
    counts = {row["device"]: row for row in Incident.objects.values("device").annotate(
        incidents=Count("id"), warnings=Count("id", filter=Q(peak__in=("warning", "shutdown"))),
        shutdowns=Count("id", filter=Q(peak="shutdown")), last=Max("started_at"),
        ongoing=Count("id", filter=Q(ended_at__isnull=True)))}
    stalls = {}
    for device in Device.objects.order_by("device_id"):
        location = location_for(device.device_id, catalog)
        stall = stalls.setdefault(location["stall_id"], {
            "stall_id": location["stall_id"], "stall_name": location["stall_name"], "location": location,
            "demo_only": bool(location.get("demo_only")), "device_ids": [],
            "incidents": 0, "warnings": 0, "shutdowns": 0, "ongoing": 0, "last_incident_at": None,
            "first_incident_at": None})
        stall["device_ids"].append(device.device_id)
        row = counts.get(device.pk)
        if row:
            for key in ("incidents", "warnings", "shutdowns", "ongoing"):
                stall[key] += row[key]
            if stall["last_incident_at"] is None or row["last"] > stall["last_incident_at"]:
                stall["last_incident_at"] = row["last"]
    result = {"stalls": sorted(stalls.values(), key=lambda s: (s["location"].get("site_name", ""), s["stall_name"])),
              "end_reasons": END_REASONS}
    if stall_id is not None:
        stall = stalls.get(stall_id)
        ids = stall["device_ids"] if stall else []
        rows = list(Incident.objects.filter(device__device_id__in=ids).select_related("device")
                    .order_by("-started_at", "-id")[:limit + 1])
        first = Incident.objects.filter(device__device_id__in=ids).order_by("started_at").first()
        if stall:
            stall["first_incident_at"] = first.started_at if first else None
        result.update(stall=stall, truncated=len(rows) > limit, incidents=[serialize(i, now) for i in rows[:limit]])
    return result


def seed_demo(device, now, days=60, seed=None):
    """Made-up past incidents for a simulated stall, marked demo=True. Returns how many."""
    rng = random.Random(seed if seed is not None else device.device_id)
    rows, day, last_end = [], timezone.localtime(now) - timedelta(days=days), None
    while True:
        day += timedelta(hours=rng.uniform(10, 60))
        # Cooking hours, local time.
        start = day.replace(hour=rng.choice((8, 9, 11, 12, 13, 17, 18, 19)), minute=rng.randrange(60))
        if start > now - timedelta(hours=1):
            break
        if last_end and start <= last_end:
            continue
        roll = rng.random()
        peak = "shutdown" if roll < 0.08 else "warning" if roll < 0.35 else "unattended"
        warn_s, shut_s = rng.choice(((180, 300), (60, 150), (30, 120)))
        incident = Incident(device=device, started_at=start, peak=peak, last_state=peak, demo=True,
                            max_temp_c=round(rng.uniform(90, 260), 1))
        if peak == "unattended":
            end_s, reason = rng.uniform(20, warn_s - 5), rng.choice(("person_returned",) * 4 + ("heating_stopped",))
        else:
            incident.warning_at = start + timedelta(seconds=warn_s)
            if peak == "warning":
                end_s, reason = warn_s + rng.uniform(5, shut_s - warn_s - 5), rng.choice(
                    ("person_returned",) * 3 + ("heating_stopped",))
            else:
                incident.shutdown_at = start + timedelta(seconds=shut_s)
                end_s, reason = shut_s + rng.uniform(120, 1800), "operator_reset"
        incident.ended_at, incident.end_reason = start + timedelta(seconds=end_s), reason
        last_end = incident.ended_at
        rows.append(incident)
    Incident.objects.bulk_create(rows)
    return len(rows)
