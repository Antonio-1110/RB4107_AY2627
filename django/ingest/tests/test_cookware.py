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
        self.assertEqual(values["tier"], "tier4")
        self.assertEqual(values["effective_warning_seconds"], 60)
        self.assertEqual(values["effective_shutdown_seconds"], 120)
        self.assertEqual([t["tier"] for t in values["tiers"]["levels"]], [2, 3, 4, 5])
        self.assertEqual(values["tiers"]["levels"][0]["temp_c"], 100.0)
        self.assertEqual(values["tiers"]["levels"][3]["warning_seconds"], 30)
        self.assertEqual(values["tiers"]["levels"][3]["shutdown_seconds"], 60)
        self.assertTrue(values["tiers"]["rate"])

    def test_older_firmware_tiers_are_not_shown(self):
        data = demo_telemetry("controller_01", boot_id="b00t")
        data["safety"]["tiers"] = {"elevated": {"temp_c": 120.0, "warning_ms": 60000, "shutdown_ms": 150000},
                                   "source": "menuconfig"}
        fields = normalize(parse("rb4107/controller/state", json.dumps(data).encode()))
        self.assertIsNone(fields["tiers"])

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
    GOOD = {"levels": [{"temp_c": 100, "warning_s": 120, "shutdown_s": 240},
                       {"temp_c": 150, "warning_s": 90, "shutdown_s": 180},
                       {"temp_c": 200, "warning_s": 60, "shutdown_s": 120},
                       {"temp_c": 250, "warning_s": 30, "shutdown_s": 60}]}

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
        self.assertEqual((command["tier2_temp_dc"], command["tier2_warning_ms"], command["tier2_shutdown_ms"]), (1000, 120000, 240000))
        self.assertEqual((command["tier5_temp_dc"], command["tier5_warning_ms"], command["tier5_shutdown_ms"]), (2500, 30000, 60000))
        self.assertFalse(any(key.startswith("tier1_") for key in command))
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_a_tier_can_be_switched_off_with_a_zero_temperature(self, publish):
        body = json.loads(json.dumps(self.GOOD))
        body["levels"][1]["temp_c"] = 0  # tier 3 off: tier 4 may still start above tier 2
        self.assertEqual(self.post(body).status_code, 202)
        self.assertEqual(publish.call_args.args[1]["tier3_temp_dc"], 0)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "tiers_defaults")
        self.assertNotIn("tier2_temp_dc", command)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        def changed(level, key, value):
            body = json.loads(json.dumps(self.GOOD))
            body["levels"][level][key] = value
            return body
        bad = [{}, {"defaults": False}, [1], {"levels": self.GOOD["levels"][:3]},
               {"levels": self.GOOD["levels"] + [self.GOOD["levels"][3]]},
               changed(0, "temp_c", -1), changed(0, "temp_c", 301),
               changed(3, "warning_s", 0), changed(3, "shutdown_s", 3601), changed(0, "warning_s", "60"),
               changed(3, "temp_c", True), changed(3, "temp_c", 190),  # tier 5 below tier 4
               changed(1, "rate_c_per_min", 20),  # no such field any more
               {**self.GOOD, "extra": 1}]
        for body in bad:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_the_schema_refuses_tier_values_on_another_action(self):
        command = {"schema_version": 2, "type": "controller_command", "controller_id": "c", "request_id": 1,
                   "action": "reset", "tier2_temp_dc": 1000}
        with self.assertRaises(validation.InvalidMessage):
            validation.check_command(command)

    def test_the_source_reaches_latest(self):
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="b00t", scenario="hot_pan")).encode()))
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertEqual(values["tiers_source"], "menuconfig")


class RadarRulesCommandTest(TestCase):
    URL = "/api/devices/controller_01/radar-rules/"
    GOOD = {"mode": "enforce", "quiet_gap_s": 1.5, "dwell_s": 4, "stationary_min_s": 1.5, "settle_window_s": 4,
            "settle_span_cm": 60, "zone_max_cm": 0}

    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="3f9a01c2")).encode()))

    def post(self, body):
        return self.client.post(self.URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_published_in_firmware_units(self, publish):
        self.assertEqual(self.post(self.GOOD).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "radar_rules")
        self.assertEqual((command["radar_mode"], command["radar_quiet_gap_ms"], command["radar_dwell_ms"],
                          command["radar_stationary_min_ms"], command["radar_settle_window_ms"],
                          command["radar_settle_span_cm"], command["radar_zone_max_cm"]),
                         ("enforce", 1500, 4000, 1500, 4000, 60, 0))
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "radar_rules_defaults")
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        bad = [{}, [1], {"defaults": False}, {**self.GOOD, "mode": "loud"}, {**self.GOOD, "dwell_s": -1},
               {**self.GOOD, "dwell_s": 61}, {**self.GOOD, "settle_window_s": 0.1}, {**self.GOOD, "settle_span_cm": 1001},
               {**self.GOOD, "zone_max_cm": "5"}, {**self.GOOD, "extra": 1}, {"mode": "off"}]
        for body in bad:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_telemetry_reports_the_rules(self):
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertEqual(values["radar_rules"]["source"], "menuconfig")
        self.assertEqual(values["radar_rules"]["quiet_gap_seconds"], 1.5)


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
