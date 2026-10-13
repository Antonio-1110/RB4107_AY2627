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

    def test_older_firmware_reports_none(self):
        data = demo_telemetry("controller_01", boot_id="b00t")
        for key in ("hold", "cookware"):
            data["safety"].pop(key)
        for key in ("phase", "rate_c_per_min", "settle_c"):
            data["safety"]["cooking"].pop(key)
        fields = normalize(parse("rb4107/controller/state", json.dumps(data).encode()))
        self.assertIsNone(fields["cooking_phase"])
        self.assertIsNone(fields["hold"])
        self.assertIsNone(fields["cookware_profile"])
