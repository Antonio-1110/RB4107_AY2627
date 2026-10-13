"""The retention job: old high-rate data goes, telemetry is thinned, events stay."""
import json
from datetime import timedelta

from django.test import TestCase
from django.utils import timezone

from ingest import retention
from ingest.demo import demo_telemetry
from ingest.models import InboundMessage, Reading, SafetyEvent
from ingest.storage import persist, worker_status
from ingest.validation import parse

NOW = timezone.now()
POLICY = retention.Policy(live_hours=24, full_hours=24, thin_seconds=60, days=30, other_days=90)


def store(topic, payload, age):
    return persist(parse(f"rb4107/{topic}", json.dumps(payload).encode()), received_at=NOW - age)


def telemetry(seq, age):
    data = demo_telemetry("controller_01", boot_id="b00t")
    data["sequence"] = seq
    return store("controller/state", data, age)


class RetentionTest(TestCase):
    def setUp(self):
        worker_status(True)

    def test_high_rate_topics_go_after_a_day(self):
        heartbeat = {"schema_version": 2, "type": "heartbeat", "controller_id": "controller_01", "timestamp": None,
                     "uptime_ms": 1, "safety_state": "IDLE", "safety_loop_count": 5, "sequence": 1}
        old = store("controller/heartbeat", heartbeat, timedelta(hours=30))
        new = store("controller/heartbeat", {**heartbeat, "sequence": 2}, timedelta(hours=2))
        result = retention.prune(NOW, POLICY)
        self.assertEqual(result["high_rate"], 1)
        self.assertFalse(InboundMessage.objects.filter(pk=old.pk).exists())
        self.assertTrue(InboundMessage.objects.filter(pk=new.pk).exists())

    def test_telemetry_is_thinned_after_a_day_and_deleted_after_the_limit(self):
        recent = [telemetry(i, timedelta(hours=1, seconds=i)) for i in range(1, 4)]      # full resolution
        day_old = [telemetry(10 + i, timedelta(hours=40, seconds=i)) for i in range(0, 90, 10)]  # 9 rows, 90 s
        ancient = telemetry(500, timedelta(days=40))
        result = retention.prune(NOW, POLICY)
        self.assertEqual(result["telemetry_expired"], 1)
        self.assertFalse(InboundMessage.objects.filter(pk=ancient.pk).exists())
        self.assertTrue(all(InboundMessage.objects.filter(pk=row.pk).exists() for row in recent))
        # 90 s of readings every 10 s fall into two or three 60 s buckets: one survivor per bucket.
        kept = sum(InboundMessage.objects.filter(pk=row.pk).exists() for row in day_old)
        self.assertIn(kept, (2, 3))
        self.assertEqual(result["telemetry_thinned"], len(day_old) - kept)
        # The Reading goes with its message.
        self.assertEqual(Reading.objects.filter(message__in=[r.pk for r in day_old]).count(), kept)

    def test_events_and_their_messages_are_never_deleted(self):
        event = {"schema_version": 2, "type": "event", "controller_id": "controller_01", "timestamp": None,
                 "uptime_ms": 1, "event": "shutdown",
                 "safety": {"state": "SHUTDOWN", "from_state": "WARNING", "reason": "shutdown timeout", "unattended_ms": 90000},
                 "fault": None, "sequence": 9}
        row = store("events/shutdown", event, timedelta(days=400))
        self.assertTrue(SafetyEvent.objects.filter(message=row).exists())
        retention.prune(NOW, POLICY)
        self.assertTrue(InboundMessage.objects.filter(pk=row.pk).exists())
        self.assertTrue(SafetyEvent.objects.filter(message=row).exists())

    def test_a_restart_event_keeps_the_telemetry_row_that_revealed_it(self):
        first = telemetry(1, timedelta(days=60))
        data = demo_telemetry("controller_01", boot_id="n3w")
        data["sequence"] = 1
        restart = store("controller/state", data, timedelta(days=50))
        self.assertTrue(SafetyEvent.objects.filter(event_type="controller_restarted", message=restart).exists())
        retention.prune(NOW, POLICY)
        self.assertFalse(InboundMessage.objects.filter(pk=first.pk).exists())
        self.assertTrue(InboundMessage.objects.filter(pk=restart.pk).exists())
        self.assertTrue(SafetyEvent.objects.filter(message=restart).exists())

    def test_dry_run_changes_nothing(self):
        telemetry(1, timedelta(days=40))
        before = InboundMessage.objects.count()
        result = retention.prune(NOW, POLICY, dry_run=True)
        self.assertEqual(result["telemetry_expired"], 1)
        self.assertEqual(InboundMessage.objects.count(), before)
