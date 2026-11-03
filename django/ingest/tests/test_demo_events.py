"""Made-up controller events for the simulated stalls' Event log (ingest/demo_events.py)."""
from datetime import timedelta
from io import StringIO

from django.conf import settings
from django.core.management import call_command
from django.test import TestCase, override_settings
from django.utils import timezone

from ingest.demo_events import clear_demo_events, seed_demo_events
from ingest.incidents import seed_demo
from ingest.models import Device, Incident, SafetyEvent

REASONS = {"heating detected", "heating stopped", "person absent", "person returned", "person returned during warning",
           "heating stopped while unattended", "heating stopped during warning", "unattended timeout",
           "shutdown timeout", "operator reset", "controller restarted", ""}


class DemoEventsTest(TestCase):
    def setUp(self):
        self.now = timezone.now()
        self.device = Device.objects.create(device_id="demo-test")
        seed_demo(self.device, self.now, days=30, profile="risky")

    def test_events_cover_the_last_day_and_its_incidents(self):
        count = seed_demo_events(self.device, "shutdown", self.now)
        events = SafetyEvent.objects.filter(device=self.device)
        self.assertEqual(events.count(), count)
        self.assertTrue(all(self.now - timedelta(hours=24) <= e.received_at <= self.now for e in events))
        self.assertTrue(all(e.detail["demo"] and e.detail["safety"]["reason"] in REASONS | {None} for e in events))
        # Every incident of the last day starts with "person absent" at the same moment.
        absent = set(events.filter(detail__safety__reason="person absent").values_list("received_at", flat=True))
        for incident in Incident.objects.filter(device=self.device, demo=True,
                                                started_at__gte=self.now - timedelta(hours=24)):
            self.assertIn(incident.started_at, absent)
        # The shutdown stall's own lead-in: the shutdown it is showing now.
        self.assertTrue(events.filter(event_type="shutdown", received_at__gte=self.now - timedelta(minutes=5)).exists())

    def test_the_events_api_lists_them(self):
        seed_demo_events(self.device, "fault_radars", self.now)
        listed = self.client.get("/api/devices/demo-test/events/", {"hours": 24}).json()["events"]
        self.assertTrue(listed)
        self.assertTrue(any(e["event_type"] == "fault_raised" and "presence_a_unavailable" in e["detail"]["note"]
                            for e in listed))

    def test_clear_removes_only_made_up_events(self):
        seed_demo_events(self.device, "idle", self.now)
        self.assertGreater(clear_demo_events(), 0)
        self.assertFalse(SafetyEvent.objects.exists())


@override_settings(LOCATION_CATALOG_FILE=settings.BASE_DIR / "locations.demo.json")
class SeedCommandEventsTest(TestCase):
    def test_seed_adds_events_once_and_clear_removes_them(self):
        call_command("seed_incident_history", stdout=StringIO())
        count = SafetyEvent.objects.count()
        self.assertGreater(count, 0)
        self.assertFalse(SafetyEvent.objects.filter(device__device_id="controller_01").exists())  # real controller
        call_command("seed_incident_history", stdout=StringIO())
        self.assertEqual(SafetyEvent.objects.count(), count)
        call_command("seed_incident_history", "--clear", stdout=StringIO())
        self.assertFalse(SafetyEvent.objects.exists())
