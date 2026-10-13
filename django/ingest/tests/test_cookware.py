"""Cookware profile from the dashboard: the command endpoint and the telemetry fields."""
import json
from unittest import mock

from django.test import TestCase

from ingest import validation
from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.normalization import normalize
from ingest.storage import worker_status
from ingest.validation import parse

URL = "/api/devices/controller_01/cookware/"


class CookwareCommandTest(TestCase):
    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="3f9a01c2")).encode()))

    def post(self, body):
        return self.client.post(URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_every_profile_is_published(self, publish):
        for name in ("mixed", "light", "heavy", "liquid"):
            with self.subTest(name=name):
                response = self.post({"cookware": name})
                self.assertEqual(response.status_code, 202, response.content)
                topic, command = publish.call_args.args
                self.assertEqual(topic, "rb4107/controller/command")
                self.assertEqual((command["action"], command["cookware"]), ("cookware", name))
                validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "cookware_defaults")
        self.assertNotIn("cookware", command)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        for body in [{}, {"cookware": "steel"}, {"cookware": 3}, {"cookware": None}, {"cookware": "light", "x": 1},
                     {"cookware": "light", "defaults": True}, {"defaults": False}, ["light"]]:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_the_schema_refuses_a_cookware_value_on_another_action(self):
        command = {"schema_version": 2, "type": "controller_command", "controller_id": "c", "request_id": 1,
                   "action": "reset", "cookware": "light"}
        with self.assertRaises(validation.InvalidMessage):
            validation.check_command(command)


class CookingTrendTelemetryTest(TestCase):
    def test_trend_fields_reach_latest(self):
        worker_status(True)
        data = demo_telemetry("controller_01", boot_id="b00t", scenario="cooling")
        self.assertEqual(data["safety"]["hold"], "cooling")
        self.assertEqual(data["safety"]["cooking"]["phase"], "cooling")
        handle(parse("rb4107/controller/state", json.dumps(data).encode()))
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertEqual(values["hold"], "cooling")
        self.assertEqual(values["cooking_phase"], "cooling")
        self.assertEqual(values["cookware_profile"], "light")
        self.assertEqual(values["cookware_tau_s"], 300)
        self.assertEqual(values["cookware_source"], "menuconfig")
        self.assertLess(values["cooking_rate_c_per_min"], 0)

    def test_tier_fields_reach_latest(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="b00t", scenario="hot_pan")).encode()))
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertEqual(values["tier"], "critical")
        self.assertEqual(values["effective_warning_seconds"], 30)
        self.assertEqual(values["effective_shutdown_seconds"], 120)
        self.assertEqual(values["tiers"]["elevated"]["temp_c"], 120.0)
        self.assertEqual(values["tiers"]["critical"]["shutdown_seconds"], 120)
        self.assertEqual(values["tiers"]["critical"]["rate_c_per_min"], 20.0)

    def test_older_firmware_reports_none(self):
        data = demo_telemetry("controller_01", boot_id="b00t")
        for key in ("hold", "cookware", "tier", "effective_warning_ms", "effective_shutdown_ms", "tiers"):
            data["safety"].pop(key)
        for key in ("phase", "rate_c_per_min", "settle_c"):
            data["safety"]["cooking"].pop(key)
        fields = normalize(parse("rb4107/controller/state", json.dumps(data).encode()))
        self.assertIsNone(fields["cooking_phase"])
        self.assertIsNone(fields["hold"])
        self.assertIsNone(fields["tier"])
        self.assertIsNone(fields["cookware_profile"])


class TiersCommandTest(TestCase):
    URL = "/api/devices/controller_01/tiers/"
    GOOD = {"elevated": {"temp_c": 120, "warning_s": 60, "shutdown_s": 150},
            "critical": {"temp_c": 200, "rate_c_per_min": 20, "rate_min_temp_c": 150, "warning_s": 30, "shutdown_s": 120}}

    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="3f9a01c2")).encode()))

    def post(self, body):
        return self.client.post(self.URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_tiers_are_published_in_firmware_units(self, publish):
        response = self.post(self.GOOD)
        self.assertEqual(response.status_code, 202, response.content)
        topic, command = publish.call_args.args
        self.assertEqual(topic, "rb4107/controller/command")
        self.assertEqual(command["action"], "tiers")
        self.assertEqual((command["tier1_temp_dc"], command["tier1_warning_ms"], command["tier1_shutdown_ms"]), (1200, 60000, 150000))
        self.assertEqual((command["tier2_temp_dc"], command["tier2_rate_dc_per_min"], command["tier2_rate_min_temp_dc"]), (2000, 200, 1500))
        self.assertEqual((command["tier2_warning_ms"], command["tier2_shutdown_ms"]), (30000, 120000))
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_a_tier_can_be_switched_off_with_a_zero_temperature(self, publish):
        body = json.loads(json.dumps(self.GOOD))
        body["critical"]["temp_c"] = 0
        body["critical"]["rate_c_per_min"] = 0
        self.assertEqual(self.post(body).status_code, 202)
        self.assertEqual(publish.call_args.args[1]["tier2_temp_dc"], 0)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "tiers_defaults")
        self.assertNotIn("tier1_temp_dc", command)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        def changed(group, key, value):
            body = json.loads(json.dumps(self.GOOD))
            body[group][key] = value
            return body
        bad = [{}, {"defaults": False}, [1], {"elevated": self.GOOD["elevated"]},
               changed("elevated", "temp_c", -1), changed("elevated", "temp_c", 301),
               changed("critical", "warning_s", 0), changed("critical", "shutdown_s", 3601),
               changed("critical", "rate_c_per_min", 1001), changed("elevated", "warning_s", "60"),
               changed("critical", "temp_c", True), changed("critical", "temp_c", 100),  # critical below elevated
               {**self.GOOD, "extra": 1}]
        for body in bad:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_the_schema_refuses_tier_values_on_another_action(self):
        command = {"schema_version": 2, "type": "controller_command", "controller_id": "c", "request_id": 1,
                   "action": "reset", "tier1_temp_dc": 1200}
        with self.assertRaises(validation.InvalidMessage):
            validation.check_command(command)

    def test_the_source_reaches_latest(self):
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="b00t", scenario="hot_pan")).encode()))
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertEqual(values["tiers_source"], "menuconfig")


class AttendanceCommandTest(TestCase):
    URL = "/api/devices/controller_01/attendance/"
    GOOD = {"pause_budget_s": 30, "pause_budget_pct": 25, "fusion": "all"}

    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="3f9a01c2")).encode()))

    def post(self, body):
        return self.client.post(self.URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_attendance_is_published_in_firmware_units(self, publish):
        self.assertEqual(self.post(self.GOOD).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "attendance")
        self.assertEqual((command["pause_budget_ms"], command["pause_budget_pct"], command["presence_fusion"]),
                         (30000, 25, "all"))
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_no_pause_and_either_radar(self, publish):
        self.assertEqual(self.post({"pause_budget_s": 0, "pause_budget_pct": 0, "fusion": "any"}).status_code, 202)
        self.assertEqual(publish.call_args.args[1]["pause_budget_ms"], 0)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "attendance_defaults")
        self.assertNotIn("pause_budget_ms", command)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        bad = [{}, {"defaults": False}, [1], {"pause_budget_s": 30}, {**self.GOOD, "fusion": "most"},
               {**self.GOOD, "pause_budget_s": -1}, {**self.GOOD, "pause_budget_s": 601},
               {**self.GOOD, "pause_budget_pct": 101}, {**self.GOOD, "pause_budget_s": "30"},
               {**self.GOOD, "pause_budget_s": True}, {**self.GOOD, "extra": 1}]
        for body in bad:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_fields_reach_latest(self):
        data = demo_telemetry("controller_01", boot_id="b00t", scenario="passing")
        handle(parse("rb4107/controller/state", json.dumps(data).encode()))
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertIs(values["paused"], True)
        self.assertEqual(values["pause_used_seconds"], 4)
        self.assertEqual(values["pause_budget_seconds"], 30)
        self.assertIs(values["nearby"], True)
        self.assertIs(values["occupied"], False)  # nearby, but not at the stove
        self.assertEqual(values["presence_filter"]["fusion"], "all")
        self.assertEqual(values["presence_filter"]["pause_budget_seconds"], 30)
        self.assertEqual(values["presence_filter"]["return_seconds"], 5)
