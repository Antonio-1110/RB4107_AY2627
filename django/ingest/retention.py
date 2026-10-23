"""Keep the database from growing without limit (docs/dashboard.md, "Storage").

Every accepted MQTT message is stored with its raw JSON, and telemetry a second
time as a Reading, so a controller adds roughly 0.9 GB a day at the default
rates. The raw high-rate topics are only useful for a day, and the telemetry
history only needs full resolution for the chart window. This job:

  * deletes the high-rate topics (radar live results, heartbeats, the per-node
    presence and thermal topics) after `live_hours`;
  * thins telemetry (and its Reading) to one per `thin_seconds` once it is older
    than `full_hours`, and deletes it after `days`;
  * deletes everything else (faults, node status, tuning replies, rejected
    messages) after `other_days`;
  * deletes the controller's log lines (the Logs tab) after `log_days`.

Safety events, and the messages that carry them, are never deleted: they are
small and they are the log of what the controller did.
"""
from __future__ import annotations

import logging
from dataclasses import dataclass
from datetime import timedelta

from django.conf import settings
from django.utils import timezone

from .models import InboundMessage, LogLine

log = logging.getLogger("rb4107.retention")

HIGH_RATE_SUFFIXES = ("/c4002_live", "/controller/heartbeat", "/presence", "/thermal")
TELEMETRY_SUFFIX = "/controller/state"
BATCH = 2000


@dataclass(frozen=True)
class Policy:
    live_hours: float = 24
    full_hours: float = 24
    thin_seconds: int = 60
    days: float = 30
    other_days: float = 90
    log_days: float = 7

    @classmethod
    def from_settings(cls) -> "Policy":
        return cls(**{field: getattr(settings, f"RB4107_RETENTION_{field.upper()}", getattr(cls, field))
                      for field in cls.__dataclass_fields__})


def _suffix_filter(suffixes):
    from django.db.models import Q
    query = Q()
    for suffix in suffixes:
        query |= Q(topic__endswith=suffix)
    return query


def _delete_ids(ids, dry_run):
    ids = list(ids)
    if not dry_run:
        for start in range(0, len(ids), BATCH):
            InboundMessage.objects.filter(id__in=ids[start:start + BATCH]).delete()
    return len(ids)


def prune(now=None, policy: Policy | None = None, *, dry_run: bool = False) -> dict:
    """Apply the policy once. Returns how many messages each rule removed (or would remove)."""
    now = now or timezone.now()
    policy = policy or Policy.from_settings()
    keep = InboundMessage.objects.filter(safetyevent__isnull=True)  # events and their messages stay
    result = {}

    live_cutoff = now - timedelta(hours=policy.live_hours)
    result["high_rate"] = _delete_ids(
        keep.filter(_suffix_filter(HIGH_RATE_SUFFIXES), received_at__lt=live_cutoff).values_list("id", flat=True),
        dry_run)

    telemetry = keep.filter(topic__endswith=TELEMETRY_SUFFIX)
    result["telemetry_expired"] = _delete_ids(
        telemetry.filter(received_at__lt=now - timedelta(days=policy.days)).values_list("id", flat=True), dry_run)

    thin_ids, seen = [], set()
    window = telemetry.filter(received_at__lt=now - timedelta(hours=policy.full_hours),
                              received_at__gte=now - timedelta(days=policy.days))
    for pk, received_at, device_id in window.order_by("received_at").values_list("id", "received_at", "device_id").iterator(chunk_size=5000):
        bucket = (device_id, int(received_at.timestamp() // max(policy.thin_seconds, 1)))
        if bucket in seen:
            thin_ids.append(pk)
        else:
            seen.add(bucket)
    result["telemetry_thinned"] = _delete_ids(thin_ids, dry_run)

    other = keep.exclude(_suffix_filter(HIGH_RATE_SUFFIXES + (TELEMETRY_SUFFIX,)))
    result["other"] = _delete_ids(
        other.filter(received_at__lt=now - timedelta(days=policy.other_days)).values_list("id", flat=True), dry_run)

    old_lines = LogLine.objects.filter(received_at__lt=now - timedelta(days=policy.log_days))
    result["log_lines"] = old_lines.count() if dry_run else old_lines.delete()[0]

    if any(result.values()):
        log.info("%s %s", "would prune" if dry_run else "pruned", ", ".join(f"{k}={v}" for k, v in result.items()))
    return result
