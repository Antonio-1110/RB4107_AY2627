"""Operator reset from the dashboard: the command endpoint."""
import json
from unittest import mock

from django.conf import settings
from django.test import TestCase, override_settings

from ingest import commands, validation
from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.storage import worker_status
from ingest.validation import parse

URL = "/api/devices/controller_01/reset/"


class ResetCommandTest(TestCase):
    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state", json.dumps(demo_telemetry("controller_01")).encode()))

    @mock.patch("ingest.commands.publish")
    def test_reset_is_published_on_the_controller_topic(self, publish):
        response = self.client.post(URL)
        self.assertEqual(response.status_code, 202, response.content)
        topic, command = publish.call_args.args
        self.assertEqual(topic, "rb4107/controller/command")
        self.assertEqual(command["type"], "controller_command")
        self.assertEqual(command["action"], "reset")
        self.assertEqual(command["controller_id"], "controller_01")
        self.assertEqual(response.json()["request_id"], command["request_id"])
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_per_controller_prefix_gets_the_controller_id(self, publish):
        # With rb4107/+/# each controller has its own tree; a wildcard topic can't be published to.
        with override_settings(RB4107_MQTT={**settings.RB4107_MQTT, "TOPIC": "rb4107/+/#"}):
            self.assertEqual(self.client.post(URL).status_code, 202)
            self.assertEqual(commands.command_topic("controller_02", "node_01"),
                             "rb4107/controller_02/sensors/node_01/c4002_set")
        self.assertEqual(publish.call_args.args[0], "rb4107/controller_01/controller/command")

    @mock.patch("ingest.commands.publish")
    def test_unknown_device_and_get_are_refused(self, publish):
        self.assertEqual(self.client.post("/api/devices/nobody/reset/").status_code, 404)
        self.assertEqual(self.client.get(URL).status_code, 405)
        publish.assert_not_called()

    @mock.patch("ingest.commands.publish", side_effect=commands.BrokerUnavailable("down"))
    def test_broker_down_is_reported(self, publish):
        self.assertEqual(self.client.post(URL).status_code, 503)

    def test_schema_refuses_other_actions(self):
        command = commands.build_reset_command("controller_01")
        for change in [{"action": "shutdown"}, {"controller_id": ""}, {"extra": 1}]:
            with self.subTest(change=change), self.assertRaises(validation.InvalidMessage):
                validation.check_command({**command, **change})

    def test_subscriber_ignores_its_own_commands(self):
        self.assertTrue(validation.is_command_topic("rb4107/controller/command"))
        with self.assertRaises(validation.InvalidMessage):
            parse("rb4107/controller/command", json.dumps(commands.build_reset_command("controller_01")).encode())
