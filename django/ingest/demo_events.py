"""Made-up controller events for the simulated stalls' Event log (demo only).

The last 24 hours of a demo stall as its controller would have reported them: cooking starting and
stopping, that day's made-up incidents (the same ones the Stall history shows, seeded first by
incidents.seed_demo), the odd Wi-Fi drop, and the events that led to the state the simulator is
playing now (its "demo_scenario"). The wording follows the rules table's reported reasons
(docs/safety_rules.md). Each is marked {"demo": true} and stored under its own message topic, so
clear_demo_events() finds them; the controller never sees any of it.
"""
import random
from datetime import timedelta

from django.utils import timezone

from .models import InboundMessage, Incident, SafetyEvent

DEMO_EVENT_TOPIC = "demo/event"
COOKING_STATES = {"monitoring", "unattended", "warning", "shutdown", "fault_radars", "fault_node_offline",
                  "network_loss", "cooling", "hot_pan", "passing"}
# Cooking sessions of a day, local time: (start hour, end hour).
SESSIONS = ((7, 10), (11, 14), (17, 21))


def _event(device, at, event, state, from_state, reason, fault=None, unattended_s=0):
    message = InboundMessage.objects.create(device=device, received_at=at, source_at=at, topic=DEMO_EVENT_TOPIC,
                                            raw='{"demo": true}', outcome="demo")
    note = " · ".join(part for part in (reason, fault["name"] if fault else None) if part)
    SafetyEvent.objects.create(
        device=device, message=message, received_at=at, source_at=at, event_type=event,
        detail={"demo": True, "event": event, "fault": fault, "note": note,
                "safety": {"state": state, "from_state": from_state, "reason": reason,
                           "unattended_ms": int(unattended_s * 1000)}})


def _incident_events(device, incident):
    """The events one incident produces, from leaving the stove to how it ended."""
    start = incident.started_at
    _event(device, start, "state_change", "UNATTENDED", "MONITORING", "person absent")
    last = "UNATTENDED"
    if incident.warning_at:
        _event(device, incident.warning_at, "warning", "WARNING", "UNATTENDED", "unattended timeout",
               unattended_s=(incident.warning_at - start).total_seconds())
        last = "WARNING"
    if incident.shutdown_at:
        _event(device, incident.shutdown_at, "shutdown", "SHUTDOWN", "WARNING", "shutdown timeout",
               unattended_s=(incident.shutdown_at - start).total_seconds())
        last = "SHUTDOWN"
    end = incident.ended_at
    if end is None:
        return
    if incident.end_reason == "operator_reset":
        _event(device, end, "state_change", "IDLE", "SHUTDOWN", "operator reset")
    elif incident.end_reason == "heating_stopped":
        _event(device, end, "state_change", "IDLE", last,
               "heating stopped during warning" if last == "WARNING" else "heating stopped while unattended")
    elif incident.end_reason == "person_returned":
        _event(device, end, "state_change", "MONITORING", last,
               "person returned during warning" if last == "WARNING" else "person returned")
    elif incident.end_reason == "controller_restarted":
        _event(device, end, "controller_restarted", "BOOT", None, "controller restarted")


def _lead_in(device, scenario, now):
    """The events behind the state the simulator shows now."""
    def ago(seconds):
        return now - timedelta(seconds=seconds)
    if scenario in {"unattended", "passing", "hot_pan"}:
        _event(device, ago(70), "state_change", "UNATTENDED", "MONITORING", "person absent")
    elif scenario in {"warning", "cooling"}:
        _event(device, ago(230), "state_change", "UNATTENDED", "MONITORING", "person absent")
        if scenario == "warning":
            _event(device, ago(80), "warning", "WARNING", "UNATTENDED", "unattended timeout", unattended_s=150)
    elif scenario == "shutdown":
        _event(device, ago(420), "state_change", "UNATTENDED", "MONITORING", "person absent")
        _event(device, ago(270), "warning", "WARNING", "UNATTENDED", "unattended timeout", unattended_s=150)
        _event(device, ago(120), "shutdown", "SHUTDOWN", "WARNING", "shutdown timeout", unattended_s=300)
    elif scenario == "fault_radars":
        for name in ("presence_a_unavailable", "presence_b_unavailable"):
            _event(device, ago(95), "fault_raised", "UNATTENDED", "MONITORING", None,
                   fault={"name": name, "class": "SAFETY"})
        _event(device, ago(94), "state_change", "UNATTENDED", "MONITORING", "person absent")
    elif scenario == "fault_node_offline":
        _event(device, ago(600), "fault_raised", "MONITORING", None, None,
               fault={"name": "presence_b_node_offline", "class": "SAFETY"})


def seed_demo_events(device, scenario, now=None, seed=None):
    """24 hours of made-up events for one simulated stall. Returns how many."""
    now = now or timezone.now()
    rng = random.Random(seed if seed is not None else f"events-{device.device_id}")
    since = now - timedelta(hours=24)
    before = SafetyEvent.objects.filter(device=device).count()
    cooking_now = scenario in COOKING_STATES or scenario == "checking"  # checking is MONITORING
    today = timezone.localtime(now).replace(hour=0, minute=0, second=0, microsecond=0)
    for day in (today - timedelta(days=1), today):
        for start_h, end_h in SESSIONS:
            start = day + timedelta(hours=start_h, minutes=rng.randrange(0, 40))
            end = day + timedelta(hours=end_h, minutes=rng.randrange(0, 40))
            if end <= since or start >= now:
                continue
            if start >= since:
                _event(device, start, "state_change", "MONITORING", "IDLE", "heating detected")
            # A session still going ends now for a stall that isn't cooking (it stopped a little while ago).
            stop = end if end < now - timedelta(minutes=15) else None if cooking_now else now - timedelta(minutes=20)
            if stop and stop > start:
                _event(device, stop, "state_change", "IDLE", "MONITORING", "heating stopped")
    if cooking_now and not any(day + timedelta(hours=s) <= now <= day + timedelta(hours=e, minutes=40)
                               for day in (today,) for s, e in SESSIONS):
        # The simulator says it is cooking now, outside the usual hours: it started a little while ago.
        _event(device, now - timedelta(minutes=rng.randrange(25, 50)), "state_change", "MONITORING", "IDLE",
               "heating detected")
    # Only the made-up past incidents: a live one (from the simulator's telemetry) has its own lead-in below.
    for incident in Incident.objects.filter(device=device, demo=True, started_at__gte=since, started_at__lt=now):
        _incident_events(device, incident)
    if rng.random() < 0.5:  # a short Wi-Fi drop: the controller keeps protecting, only telemetry stops
        at = since + timedelta(minutes=rng.randrange(60, 23 * 60))
        fault = {"name": "mqtt_disconnected", "class": "TELEMETRY"}
        _event(device, at, "fault_raised", "MONITORING", None, None, fault=fault)
        _event(device, at + timedelta(seconds=rng.randrange(20, 180)), "fault_cleared", "MONITORING", None, None,
               fault=fault)
    _lead_in(device, scenario, now)
    return SafetyEvent.objects.filter(device=device).count() - before


def clear_demo_events():
    """Delete every made-up event. Returns how many."""
    count = SafetyEvent.objects.filter(message__topic=DEMO_EVENT_TOPIC).count()
    InboundMessage.objects.filter(topic=DEMO_EVENT_TOPIC).delete()  # their events go with them
    return count
