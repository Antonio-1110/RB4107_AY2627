"""Per-stall incident history built from controller telemetry (ingest/incidents.py)."""
import json
from dataclasses import replace
from datetime import timedelta
from io import StringIO

from django.core.management import call_command
from django.conf import settings
from django.test import TestCase, override_settings
from django.utils import timezone

from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.incidents import seed_demo
from ingest.models import Device, Incident
from ingest.storage import persist, worker_status
from ingest.validation import parse


class IncidentTest(TestCase):
    def setUp(self):
        worker_status(True)
        self.tick = 0
        self.at = timezone.now() - timedelta(hours=1)

    def send(self, state, boot_id="b00t", seconds=10, max_c=None):
        """One live telemetry message, `seconds` after the previous one."""
        self.tick += 1
        self.at += timedelta(seconds=seconds)
        data = demo_telemetry("controller_01", tick=self.tick, boot_id=boot_id)
        data["safety"].update(state=state, shutdown=state == "SHUTDOWN",
                              buzzer=state if state in {"SHUTDOWN", "WARNING", "FAULT"} else "OFF")
        if max_c is not None:
            data["thermal"]["max_c"] = max_c
        persist(parse("rb4107/controller/state", json.dumps(data).encode()), received_at=self.at)

    def only(self):
        self.assertEqual(Incident.objects.count(), 1)
        return Incident.objects.get()

    def test_no_incident_while_attended(self):
        for state in ("IDLE", "MONITORING", "MONITORING", "IDLE"):
            self.send(state)
        self.assertEqual(Incident.objects.count(), 0)

    def test_person_returns_before_the_buzzer(self):
        self.send("MONITORING")
        self.send("UNATTENDED", max_c=150.0)
        start = self.at
        self.send("UNATTENDED", seconds=30, max_c=180.0)
        self.send("MONITORING", seconds=20)
        incident = self.only()
        self.assertEqual(incident.peak, "unattended")
        self.assertEqual(incident.started_at, start)
        self.assertEqual(incident.end_reason, "person_returned")
        self.assertEqual((incident.ended_at - incident.started_at).total_seconds(), 50)
        self.assertEqual(incident.max_temp_c, 180.0)
        self.assertIsNone(incident.warning_at)

    def test_escalation_to_shutdown_ends_with_the_reset(self):
        self.send("MONITORING")
        self.send("UNATTENDED")
        self.send("WARNING", seconds=180)
        warned = self.at
        self.send("SHUTDOWN", seconds=120)
        cut = self.at
        self.send("SHUTDOWN", seconds=60)
        self.assertIsNone(self.only().ended_at)  # latched: still ongoing
        self.send("IDLE", seconds=300)
        incident = self.only()
        self.assertEqual((incident.peak, incident.end_reason), ("shutdown", "operator_reset"))
        self.assertEqual((incident.warning_at, incident.shutdown_at), (warned, cut))

    def test_heating_stopped_during_warning(self):
        self.send("UNATTENDED")
        self.send("WARNING")
        self.send("IDLE")
        self.assertEqual((self.only().peak, self.only().end_reason), ("warning", "heating_stopped"))

    def test_fault_keeps_the_incident_open_and_a_restart_closes_it(self):
        self.send("UNATTENDED")
        self.send("FAULT")
        self.assertIsNone(self.only().ended_at)
        self.send("IDLE", boot_id="n3wb00t")  # new boot: the controller restarted
        self.assertEqual(self.only().end_reason, "controller_restarted")

    def test_each_episode_is_its_own_incident(self):
        for state in ("UNATTENDED", "MONITORING", "UNATTENDED", "WARNING", "MONITORING"):
            self.send(state)
        self.assertEqual(list(Incident.objects.order_by("started_at").values_list("peak", flat=True)),
                         ["unattended", "warning"])

    def test_retained_snapshot_is_not_an_incident(self):
        data = demo_telemetry("controller_01", boot_id="b00t")
        data["safety"].update(state="WARNING", buzzer="WARNING")
        handle(replace(parse("rb4107/controller/state", json.dumps(data).encode()), retained=True))
        self.assertEqual(Incident.objects.count(), 0)

    def test_api_counts_and_lists_by_stall(self):
        for state in ("UNATTENDED", "WARNING", "SHUTDOWN", "IDLE", "UNATTENDED"):
            self.send(state)
        body = self.client.get("/api/stall-history/").json()
        stall = next(s for s in body["stalls"] if "controller_01" in s["device_ids"])
        self.assertEqual((stall["incidents"], stall["warnings"], stall["shutdowns"], stall["ongoing"]), (2, 1, 1, 1))
        self.assertNotIn("incidents", body)  # only with ?stall=
        listed = self.client.get("/api/stall-history/", {"stall": stall["stall_id"]}).json()
        self.assertEqual([i["peak"] for i in listed["incidents"]], ["unattended", "shutdown"])
        self.assertTrue(listed["incidents"][0]["ongoing"])
        self.assertEqual(listed["incidents"][1]["end_reason_text"], "Reset after shutdown")


@override_settings(LOCATION_CATALOG_FILE=settings.BASE_DIR / "locations.demo.json")
class SeedTest(TestCase):
    def test_seed_fills_demo_stalls_once_and_clear_removes_them(self):
        call_command("seed_incident_history", stdout=StringIO())
        count = Incident.objects.count()
        self.assertGreater(count, 0)
        self.assertFalse(Incident.objects.filter(demo=False).exists())
        self.assertFalse(Incident.objects.filter(device__device_id="controller_01").exists())  # real controller
        call_command("seed_incident_history", stdout=StringIO())
        self.assertEqual(Incident.objects.count(), count)
        call_command("seed_incident_history", "--clear", stdout=StringIO())
        self.assertEqual(Incident.objects.count(), 0)

    def test_seeded_incidents_are_consistent(self):
        device = Device.objects.create(device_id="demo-x")
        now = timezone.now()
        seed_demo(device, now, days=90)
        previous_end = None
        for incident in Incident.objects.order_by("started_at"):
            self.assertLess(incident.started_at, incident.ended_at)
            self.assertLess(incident.ended_at, now)
            if previous_end:
                self.assertGreater(incident.started_at, previous_end)
            previous_end = incident.ended_at
            self.assertEqual(incident.shutdown_at is not None, incident.peak == "shutdown")
            self.assertEqual(incident.warning_at is not None, incident.peak != "unattended")
            if incident.peak == "shutdown":
                self.assertEqual(incident.end_reason, "operator_reset")
