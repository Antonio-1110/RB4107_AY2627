"""Per-stall history of critical events (the industry partner's "what happened at this stall").

An incident is one unattended-cooking episode: the controller's reported state
leaves MONITORING for UNATTENDED, may climb to WARNING (buzzer) and SHUTDOWN
(supply cut), and ends when it is back to MONITORING or IDLE. It is built from
the controller's telemetry, which the firmware publishes on every state change,
so no firmware change is needed. Display only, and a demo-level record: the end
reason is inferred from the state the controller went to, not reported by it.
"""
import random
from datetime import datetime, time, timedelta
from statistics import median

from django.conf import settings
from django.db.models import Count, Max, Q
from django.db.models.functions import TruncDate
from django.utils import timezone

from .locations import COOKING_STATES, load_location_catalog, location_for
from .models import Device, InboundMessage, Incident, Reading

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


PERIOD_DAYS = (14, 28, 90)  # the dashboard's period picker: 2 weeks, 4 weeks, 3 months
# How a finished countdown ended, for the recovery table: (key, label, peak, end reasons, timed from).
# "Timed from" is the moment the clock starts: the countdown's start, the buzzer or the shut-off.
RECOVERY = (
    ("before_buzzer", "Back before the buzzer", "unattended", ("person_returned", "heating_stopped"), "started_at"),
    ("after_buzzer", "Back after the buzzer", "warning", ("person_returned",), "warning_at"),
    ("stove_off_after_buzzer", "Stove turned off after the buzzer", "warning", ("heating_stopped",), "warning_at"),
    ("after_shutoff", "Reset after a shut-off", "shutdown", ("operator_reset",), "shutdown_at"),
    ("controller_restarted", "Controller restarted (not timed)", None, ("controller_restarted",), None),
)


def _spread(seconds):
    """Count, median and slowest of a list of durations in seconds (None when empty)."""
    return {"count": len(seconds), "median_seconds": median(seconds) if seconds else None,
            "slowest_seconds": max(seconds) if seconds else None}


def _week_start(at):
    day = timezone.localtime(at).date()
    return day - timedelta(days=day.weekday())  # Monday, in the server's time zone


def cooking_days(device_pks, since):
    """{device pk: set of local dates} with at least one stored Reading in a cooking state.

    Readings are thinned and deleted after RB4107_RETENTION_DAYS (30 by default), so for
    a longer period this only covers the newest part of it."""
    days = {}
    rows = (Reading.objects.filter(device__in=device_pks, received_at__gte=since,
                                   values__safety_state__in=list(COOKING_STATES))
            .annotate(day=TruncDate("received_at")).values_list("device", "day").distinct())
    for device, day in rows:
        days.setdefault(device, set()).add(day)
    return days


def period_summary(incidents, now, since, days, cooking):
    """What a fire-safety inspector asks about one stall over the period: how often the
    countdown started, how many reached the buzzer and the shut-off, how long recovery took."""
    count = len(incidents)
    buzzer = sum(1 for i in incidents if i.peak in ("warning", "shutdown"))
    shutoff = sum(1 for i in incidents if i.peak == "shutdown")
    weeks = {}
    week = _week_start(since)
    while week <= _week_start(now):
        first, last = max(week, timezone.localtime(since).date()), min(week + timedelta(days=6),
                                                                         timezone.localtime(now).date())
        weeks[week] = {"week_start": week.isoformat(), "days_in_period": (last - first).days + 1,
                       "countdown_only": 0, "buzzer": 0, "shutoff": 0}
        week += timedelta(days=7)
    bar = {"unattended": "countdown_only", "warning": "buzzer", "shutdown": "shutoff"}
    for incident in incidents:
        weeks[_week_start(incident.started_at)][bar.get(incident.peak, "countdown_only")] += 1
    recovery, recovered = {}, []
    for key, label, peak, reasons, start_field in RECOVERY:
        matched = [i for i in incidents if i.ended_at and i.end_reason in reasons and (peak is None or i.peak == peak)]
        timed = [(i.ended_at - getattr(i, start_field)).total_seconds() for i in matched
                 if start_field and getattr(i, start_field)]
        recovery[key] = {"label": label, **_spread(timed), "count": len(matched)}
        if start_field:
            recovered += [(i.ended_at - i.started_at).total_seconds() for i in matched]
    on_cooking_days = sum(1 for i in incidents if timezone.localtime(i.started_at).date() in cooking)
    return {
        "days": days, "since": since,
        "countdowns": count, "buzzer": buzzer, "shutoff": shutoff,
        "ongoing": sum(1 for i in incidents if i.ended_at is None),
        "countdowns_per_week": round(count * 7 / days, 2),
        "buzzer_share": buzzer / count if count else None,
        "shutoff_share": shutoff / buzzer if buzzer else None,
        "cooking_days": len(cooking), "countdowns_on_cooking_days": on_cooking_days,
        "countdowns_per_cooking_day": round(on_cooking_days / len(cooking), 2) if cooking else None,
        # Countdown start to the end, over every recovery except a controller restart.
        "usual_recovery": _spread(recovered),
        "recovery": recovery,
        "weeks": list(weeks.values()),
    }


def stall_history(now, stall_id=None, limit=500, days=None):
    """Counts for every stall, and the incidents of `stall_id` (newest first).

    With `days` (one of PERIOD_DAYS) every stall also gets a `period` summary of
    its last `days` days, and the incident list keeps to that period."""
    catalog = load_location_catalog()
    counts = {row["device"]: row for row in Incident.objects.values("device").annotate(
        incidents=Count("id"), warnings=Count("id", filter=Q(peak__in=("warning", "shutdown"))),
        shutdowns=Count("id", filter=Q(peak="shutdown")), last=Max("started_at"),
        ongoing=Count("id", filter=Q(ended_at__isnull=True)))}
    since = now - timedelta(days=days) if days else None
    in_period, cooking = {}, {}
    if days:
        for incident in Incident.objects.filter(started_at__gte=since).order_by("started_at"):
            in_period.setdefault(incident.device_id, []).append(incident)
    stalls, devices = {}, {}
    for device in Device.objects.order_by("device_id"):
        location = location_for(device.device_id, catalog)
        stall = stalls.setdefault(location["stall_id"], {
            "stall_id": location["stall_id"], "stall_name": location["stall_name"], "location": location,
            "demo_only": bool(location.get("demo_only")), "device_ids": [],
            "incidents": 0, "warnings": 0, "shutdowns": 0, "ongoing": 0, "last_incident_at": None,
            "first_incident_at": None})
        stall["device_ids"].append(device.device_id)
        devices.setdefault(location["stall_id"], []).append(device.pk)
        row = counts.get(device.pk)
        if row:
            for key in ("incidents", "warnings", "shutdowns", "ongoing"):
                stall[key] += row[key]
            if stall["last_incident_at"] is None or row["last"] > stall["last_incident_at"]:
                stall["last_incident_at"] = row["last"]
    if days:
        cooking = cooking_days([pk for pks in devices.values() for pk in pks], since)
        for key, stall in stalls.items():
            # A stall with two controllers cooks on a day when either of them does.
            stall["period"] = period_summary(
                sorted((i for pk in devices[key] for i in in_period.get(pk, [])), key=lambda i: i.started_at),
                now, since, days, set().union(*(cooking.get(pk, set()) for pk in devices[key])))
    result = {"stalls": sorted(stalls.values(), key=lambda s: (s["location"].get("site_name", ""), s["stall_name"])),
              "end_reasons": END_REASONS, "days": days, "since": since}
    if stall_id is not None:
        stall = stalls.get(stall_id)
        ids = stall["device_ids"] if stall else []
        listed = Incident.objects.filter(device__device_id__in=ids)
        if since:
            listed = listed.filter(started_at__gte=since)
        rows = list(listed.select_related("device").order_by("-started_at", "-id")[:limit + 1])
        first = Incident.objects.filter(device__device_id__in=ids).order_by("started_at").first()
        if stall:
            stall["first_incident_at"] = first.started_at if first else None
        result.update(stall=stall, truncated=len(rows) > limit, incidents=[serialize(i, now) for i in rows[:limit]])
    return result


# Made-up stall habits for the demo, so the stalls differ on the Stall history page:
# (hours between countdowns, share reaching the buzzer, share of those reaching the shut-off).
DEMO_PROFILES = {
    "careful": ((24, 72), 0.15, 0.05),
    "typical": ((10, 48), 0.30, 0.20),
    "busy": ((4, 20), 0.30, 0.15),
    "risky": ((8, 36), 0.55, 0.40),
}
DEMO_TOPIC = "demo/cooking_day"  # the made-up cooking-day Readings' message topic


def seed_demo(device, now, days=90, seed=None, profile="typical"):
    """Made-up past incidents for a simulated stall, marked demo=True, plus one made-up
    cooking Reading per day the stall was open (for "countdowns per cooking day").
    Returns how many incidents."""
    rng = random.Random(seed if seed is not None else device.device_id)
    gap, buzzer_share, shutoff_share = DEMO_PROFILES[profile]
    rows, day, last_end = [], timezone.localtime(now) - timedelta(days=days), None
    while True:
        day += timedelta(hours=rng.uniform(*gap))
        # Cooking hours, local time.
        start = day.replace(hour=rng.choice((8, 9, 11, 12, 13, 17, 18, 19)), minute=rng.randrange(60))
        if start > now - timedelta(hours=1):
            break
        if last_end and start <= last_end + timedelta(minutes=10):
            continue
        roll = rng.random()
        peak = ("shutdown" if roll < buzzer_share * shutoff_share else
                "warning" if roll < buzzer_share else "unattended")
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
        if rng.random() < 0.02:  # now and then the controller restarts mid-countdown
            end_s, reason = rng.uniform(10, end_s), "controller_restarted"
            if incident.shutdown_at and start + timedelta(seconds=end_s) < incident.shutdown_at:
                incident.peak = incident.last_state = "warning"
                incident.shutdown_at = None
            if incident.warning_at and start + timedelta(seconds=end_s) < incident.warning_at:
                incident.peak = incident.last_state = "unattended"
                incident.warning_at = None
        incident.ended_at, incident.end_reason = start + timedelta(seconds=end_s), reason
        last_end = incident.ended_at
        rows.append(incident)
    Incident.objects.bulk_create(rows)
    seed_demo_cooking_days(device, now, days, rng, {timezone.localtime(i.started_at).date() for i in rows})
    return len(rows)


def seed_demo_cooking_days(device, now, days, rng, busy_days):
    """One made-up cooking Reading at noon on each day the stall was open: every day with
    a countdown, and most others (closed one day in seven). Only for the days Reading
    retention keeps (RB4107_RETENTION_DAYS), like real ones."""
    keep = min(days, int(settings.RB4107_RETENTION_DAYS))
    today = timezone.localtime(now).date()
    for back in range(keep):
        day = today - timedelta(days=back)
        at = timezone.make_aware(datetime.combine(day, time(12)))
        if at > now or (day not in busy_days and rng.random() < 1 / 7):
            continue
        message = InboundMessage.objects.create(device=device, received_at=at, topic=DEMO_TOPIC,
                                                raw='{"demo": true}', outcome="demo")
        Reading.objects.create(device=device, message=message, received_at=at,
                               values={"safety_state": "monitoring", "demo": True})


def clear_demo():
    """Delete every demo incident and made-up cooking Reading. Returns how many incidents."""
    InboundMessage.objects.filter(topic=DEMO_TOPIC).delete()  # their Readings go with them
    return Incident.objects.filter(demo=True).delete()[0]
