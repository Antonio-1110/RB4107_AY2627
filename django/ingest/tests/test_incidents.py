"""Per-stall incident history built from controller telemetry (ingest/incidents.py)."""
import json
from dataclasses import replace
from datetime import date, datetime, timedelta
from io import StringIO

from django.core.management import call_command
from django.conf import settings
from django.test import TestCase, override_settings
from django.utils import timezone

from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.incidents import DEMO_TOPIC, period_bounds, stall_history, seed_demo
from ingest.models import Device, InboundMessage, Incident, Reading
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


class PeriodTest(TestCase):
    """The Stall history page's period summary, checked by hand against a small set of incidents."""
    NOW = timezone.make_aware(datetime(2026, 11, 4, 12))  # a Wednesday, server time (Asia/Singapore)

    def setUp(self):
        self.device = Device.objects.create(device_id="controller_01")
        ago = lambda days, seconds=0: self.NOW - timedelta(days=days) + timedelta(seconds=seconds)
        rows = (  # days ago, peak, buzzer after s, shut-off after s, ended after s, end reason
            (2, "unattended", None, None, 60, "person_returned"),
            (3, "unattended", None, None, 100, "heating_stopped"),
            (5, "unattended", None, None, 40, "controller_restarted"),
            (10, "warning", 120, None, 150, "person_returned"),
            (12, "warning", 60, None, 150, "person_returned"),
            (20, "shutdown", 60, 180, 780, "operator_reset"),
            (40, "shutdown", 60, 180, 400, "operator_reset"),  # only in the 3-month period
        )
        for days, peak, warn, cut, end, reason in rows:
            Incident.objects.create(device=self.device, started_at=ago(days), peak=peak,
                                    warning_at=warn and ago(days, warn), shutdown_at=cut and ago(days, cut),
                                    ended_at=ago(days, end), end_reason=reason)
        Incident.objects.create(device=self.device, started_at=self.NOW - timedelta(hours=1))  # ongoing
        for days, state in ((2, "monitoring"), (10, "unattended"), (3, "idle")):  # idle: not cooking
            message = InboundMessage.objects.create(device=self.device, received_at=ago(days), topic="t", raw="{}")
            Reading.objects.create(device=self.device, message=message, received_at=ago(days),
                                   values={"safety_state": state})

    def period(self, days):
        body = stall_history(self.NOW, days=days)
        return next(s for s in body["stalls"] if "controller_01" in s["device_ids"])["period"]

    def test_four_weeks_by_hand(self):
        p = self.period(28)
        self.assertEqual((p["countdowns"], p["buzzer"], p["shutoff"], p["ongoing"]), (7, 3, 1, 1))
        self.assertEqual(p["countdowns_per_week"], 1.75)  # 7 in 4 weeks
        self.assertAlmostEqual(p["buzzer_share"], 3 / 7)
        self.assertAlmostEqual(p["shutoff_share"], 1 / 3)  # of the buzzer events
        # Cooking on 2 days (2 and 10 days ago); one countdown started on each.
        self.assertEqual((p["cooking_days"], p["countdowns_on_cooking_days"], p["countdowns_per_cooking_day"]),
                         (2, 2, 1.0))
        r = p["recovery"]
        self.assertEqual([r["before_buzzer"][k] for k in ("count", "median_seconds", "slowest_seconds")], [2, 80, 100])
        self.assertEqual([r["after_buzzer"][k] for k in ("count", "median_seconds", "slowest_seconds")], [2, 60, 90])
        self.assertEqual([r["after_shutoff"][k] for k in ("count", "median_seconds", "slowest_seconds")], [1, 600, 600])
        self.assertEqual(r["controller_restarted"]["count"], 1)
        self.assertIsNone(r["controller_restarted"]["median_seconds"])
        self.assertEqual(r["stove_off_after_buzzer"]["count"], 0)
        # Start to end of the 5 recovered countdowns: 60, 100, 150, 150, 780 (not the restart, not ongoing).
        self.assertEqual((p["usual_recovery"]["median_seconds"], p["usual_recovery"]["slowest_seconds"]), (150, 780))
        # Monday weeks from Mon 5 Oct (the period starts Wed 7 Oct) to Mon 2 Nov (today is Wed 4 Nov).
        self.assertEqual([(w["week_start"], w["days_in_period"], w["countdown_only"], w["buzzer"], w["shutoff"])
                          for w in p["weeks"]],
                         [("2026-10-05", 5, 0, 0, 0), ("2026-10-12", 7, 0, 0, 1), ("2026-10-19", 7, 0, 2, 0),
                          ("2026-10-26", 7, 2, 0, 0), ("2026-11-02", 3, 2, 0, 0)])

    def test_the_period_changes_the_numbers(self):
        self.assertEqual((self.period(14)["countdowns"], self.period(14)["buzzer"]), (6, 2))  # incl. the ongoing one
        three_months = self.period(90)
        self.assertEqual((three_months["countdowns"], three_months["shutoff"]), (8, 2))
        self.assertEqual(three_months["recovery"]["after_shutoff"]["median_seconds"], 410)  # 600 and 220
        self.assertEqual(len(three_months["weeks"]), 14)

    def test_any_number_of_days(self):
        week = self.period(7)
        self.assertEqual((week["countdowns"], week["countdowns_per_week"], week["days"]), (4, 4.0, 7))  # 2, 3, 5 days ago + ongoing
        self.assertEqual((self.period(1)["countdowns"], self.period(1)["countdowns_per_week"]), (1, 7.0))

    def test_date_range_by_hand(self):
        # Tue 20 Oct to Sun 25 Oct, whole days: the warnings 12 days ago (Fri 23) and 10 days ago (Sun 25).
        period = period_bounds(self.NOW, start=date(2026, 10, 20), end=date(2026, 10, 25))
        self.assertEqual(period, (timezone.make_aware(datetime(2026, 10, 20)), timezone.make_aware(datetime(2026, 10, 26))))
        body = stall_history(self.NOW, period=period)
        p = next(s for s in body["stalls"] if "controller_01" in s["device_ids"])["period"]
        self.assertEqual((p["countdowns"], p["buzzer"], p["shutoff"], p["days"]), (2, 2, 0, 6))
        self.assertEqual(p["countdowns_per_week"], round(2 / (6 / 7), 2))
        self.assertEqual((p["cooking_days"], p["countdowns_on_cooking_days"]), (1, 1))  # the reading on Sun 25
        self.assertEqual([(w["week_start"], w["days_in_period"], w["buzzer"]) for w in p["weeks"]],
                         [("2026-10-19", 6, 2)])
        # A range ending today stops at now; one ending Sat 24 leaves out Sunday's warning.
        self.assertEqual(period_bounds(self.NOW, start=date(2026, 11, 1))[1], self.NOW)
        saturday = stall_history(self.NOW, period=period_bounds(self.NOW, start=date(2026, 10, 20), end=date(2026, 10, 24)))
        self.assertEqual(next(s for s in saturday["stalls"] if s["period"]["countdowns"])["period"]["countdowns"], 1)

    def test_no_countdowns_gives_no_shares(self):
        Incident.objects.all().delete()
        p = self.period(14)
        self.assertEqual((p["countdowns"], p["buzzer_share"], p["shutoff_share"]), (0, None, None))
        self.assertIsNone(p["usual_recovery"]["median_seconds"])

    def test_api_days_parameter(self):
        body = self.client.get("/api/stall-history/", {"days": "28"}).json()
        stall = next(s for s in body["stalls"] if "controller_01" in s["device_ids"])
        self.assertEqual(stall["period"]["days"], 28)
        self.assertIn("period", stall)
        listed = self.client.get("/api/stall-history/", {"days": "14", "stall": stall["stall_id"]}).json()
        self.assertTrue(listed["incidents"])
        self.assertTrue(all(i["started_at"] >= listed["since"] for i in listed["incidents"]))
        self.assertEqual(self.client.get("/api/stall-history/", {"days": "7"}).status_code, 200)
        for bad in ("0", "367", "abc", "", "2.5"):
            self.assertEqual(self.client.get("/api/stall-history/", {"days": bad}).status_code, 400)
        today = timezone.localdate()
        ranged = self.client.get("/api/stall-history/", {"from": str(today - timedelta(days=13)), "to": str(today)})
        self.assertEqual(ranged.status_code, 200)
        self.assertEqual(ranged.json()["since"][:10], str(today - timedelta(days=13)))
        for bad in ({"from": str(today), "to": str(today - timedelta(days=1))},  # backwards
                    {"from": str(today), "to": str(today + timedelta(days=1))},  # the future
                    {"to": str(today)}, {"from": "yesterday"},
                    {"from": str(today - timedelta(days=366)), "to": str(today)},  # 367 days
                    {"days": "7", "from": str(today)}):
            self.assertEqual(self.client.get("/api/stall-history/", bad).status_code, 400, bad)
        self.assertNotIn("period", self.client.get("/api/stall-history/").json()["stalls"][0])  # all-time as before


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
        self.assertTrue(Reading.objects.filter(message__topic=DEMO_TOPIC).exists())  # made-up cooking days
        call_command("seed_incident_history", "--clear", stdout=StringIO())
        self.assertEqual(Incident.objects.count(), 0)
        self.assertFalse(Reading.objects.exists())

    def test_demo_stalls_differ(self):
        call_command("seed_incident_history", stdout=StringIO())
        body = stall_history(timezone.now(), days=28)
        demo = [s["period"] for s in body["stalls"] if s["demo_only"]]
        self.assertGreater(len({p["countdowns"] for p in demo}), 2)
        self.assertTrue(any(p["shutoff"] for p in demo))
        self.assertTrue(all(p["cooking_days"] >= 20 for p in demo))  # open most days

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
                self.assertIn(incident.end_reason, ("operator_reset", "controller_restarted"))
