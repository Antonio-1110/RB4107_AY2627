"""The other safety settings from the dashboard (rb_params.def): the command endpoint and the telemetry fields."""
import json
from unittest import mock

from django.test import TestCase

from ingest import validation
from ingest.commands import build_params_command, safety_params
from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.storage import worker_status
from ingest.validation import parse

URL = "/api/devices/controller_01/params/"


def defaults():
    return {p["key"]: p["default"] for p in safety_params()}


class ParamsCommandTest(TestCase):
    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="3f9a01c2")).encode()))

    def post(self, body):
        return self.client.post(URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_every_setting_is_published(self, publish):
        for end in ("min", "max"):
            with self.subTest(end=end):
                body = {p["key"]: p[end] for p in safety_params()}
                response = self.post(body)
                self.assertEqual(response.status_code, 202, response.content)
                topic, command = publish.call_args.args
                self.assertEqual(topic, "rb4107/controller/command")
                self.assertEqual(command["action"], "params")
                self.assertEqual({k: command[k] for k in body}, body)
                validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "params_defaults")
        self.assertFalse(set(command) & set(defaults()))
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        first = safety_params()[0]
        missing = defaults()
        del missing["falling_hold_max_ms"]
        bad = [{}, [], missing, {**defaults(), "x": 1}, {**defaults(), "defaults": True},
               {**defaults(), first["key"]: first["max"] + 1}, {**defaults(), "falling_hold_max_ms": 2.5},
               {**defaults(), "cooking_trend": True}, {**defaults(), "trend_window_ms": 120000},
               {**defaults(), "fault_shutdown_ms": 0}]
        for body in bad:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_the_longest_command_fits_the_controllers_mqtt_buffer(self):
        # firmware/components/rb_mqtt/src/rb_mqtt.c: RB_MQTT_BUFFER_BYTES; a bigger message arrives in pieces and is dropped.
        # The MQTT header and topic take under 100 bytes of it.
        longest = {p["key"]: p["max"] for p in safety_params()}
        command = build_params_command("c" * 39, longest)
        self.assertLess(len(json.dumps(command, separators=(",", ":"))) + 100, 2048)

    def test_the_schema_refuses_a_setting_on_another_action(self):
        command = {"schema_version": 2, "type": "controller_command", "controller_id": "c", "request_id": 1,
                   "action": "reset", "falling_hold_max_ms": 20000}
        self.assertIsNone(validation.check_command(command))  # the key is known ...
        command["falling_hold_max_ms"] = 0
        with self.assertRaises(validation.InvalidMessage):  # ... and range-checked
            validation.check_command(command)


class ParamsTelemetryTest(TestCase):
    def test_params_reach_latest(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="b00t")).encode()))
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertEqual(values["params"], defaults())
        self.assertEqual(values["params_source"], "menuconfig")
        self.assertEqual(values["params"]["fault_shutdown_ms"], 90000)
