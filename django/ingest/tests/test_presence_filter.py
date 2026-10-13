"""Presence filter from the dashboard: the command endpoint and the telemetry field."""
import json
from unittest import mock

from django.test import TestCase

from ingest import commands, validation
from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.storage import worker_status
from ingest.validation import parse

URL = "/api/devices/controller_01/presence_filter/"
FILTER = {"absence_ms": 2000, "return_ms": 3000, "return_gap_ms": 1000}


class PresenceFilterCommandTest(TestCase):
    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="3f9a01c2")).encode()))

    def post(self, body):
        return self.client.post(URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_filter_is_published_on_the_controller_topic(self, publish):
        response = self.post(FILTER)
        self.assertEqual(response.status_code, 202, response.content)
        topic, command = publish.call_args.args
        self.assertEqual(topic, "rb4107/controller/command")
        self.assertEqual(command["action"], "presence_filter")
        self.assertEqual({key: command[key] for key in FILTER}, FILTER)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "presence_filter_defaults")
        self.assertFalse(set(FILTER) & set(command))

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        for body in [{}, {"return_ms": 3000}, {**FILTER, "return_ms": 10001}, {**FILTER, "return_gap_ms": 5001},
                     {**FILTER, "absence_ms": -1}, {**FILTER, "return_ms": 2.5}, {**FILTER, "extra": 1},
                     {**FILTER, "defaults": True}, [1]]:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_schema_refuses_filter_values_with_reset(self):
        command = commands.build_reset_command("controller_01")
        with self.assertRaises(validation.InvalidMessage):
            validation.check_command({**command, **FILTER})

    def test_filter_in_use_reaches_the_dashboard(self):
        values = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        self.assertEqual(values["presence_filter"], {"absence_seconds": 2.0, "return_seconds": 3.0,
                                                     "return_gap_seconds": 1.0, "source": "menuconfig"})
