"""Hot-pixel threshold from the dashboard: the command endpoint."""
import json
from unittest import mock

from django.conf import settings
from django.test import TestCase, override_settings

from ingest import commands, validation
from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.storage import worker_status
from ingest.validation import parse

URL = "/api/devices/controller_01/thermal_threshold/"


class ThermalThresholdCommandTest(TestCase):
    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state",
                     json.dumps(demo_telemetry("controller_01", boot_id="3f9a01c2")).encode()))

    def post(self, body):
        return self.client.post(URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_threshold_is_published_in_tenths_of_a_degree(self, publish):
        response = self.post({"hot_pixel_threshold_c": 62.55})
        self.assertEqual(response.status_code, 202, response.content)
        topic, command = publish.call_args.args
        self.assertEqual(topic, "rb4107/controller/command")
        self.assertEqual(command["action"], "thermal_threshold")
        self.assertEqual(command["hot_pixel_threshold_dc"], 626)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_whole_degrees_and_limits(self, publish):
        for value, dc in [(0, 0), (50, 500), (300, 3000)]:
            with self.subTest(value=value):
                self.assertEqual(self.post({"hot_pixel_threshold_c": value}).status_code, 202)
                self.assertEqual(publish.call_args.args[1]["hot_pixel_threshold_dc"], dc)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "thermal_threshold_defaults")
        self.assertNotIn("hot_pixel_threshold_dc", command)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        for body in [{}, {"hot_pixel_threshold_c": -1}, {"hot_pixel_threshold_c": 300.1},
                     {"hot_pixel_threshold_c": "50"}, {"hot_pixel_threshold_c": True},
                     {"hot_pixel_threshold_c": None}, {"hot_pixel_threshold_c": 50, "extra": 1},
                     {"hot_pixel_threshold_c": 50, "defaults": True}, {"defaults": False}, [1]]:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    @mock.patch("ingest.commands.publish")
    def test_topic_names_the_controller_with_a_wildcard_subscription(self, publish):
        with override_settings(RB4107_MQTT={**settings.RB4107_MQTT, "TOPIC": "rb4107/+/#"}):
            self.assertEqual(self.post({"hot_pixel_threshold_c": 50}).status_code, 202)
        self.assertEqual(publish.call_args.args[0], "rb4107/controller_01/controller/command")

    def test_schema_ties_the_value_to_its_action(self):
        command = commands.build_reset_command("controller_01")
        with self.assertRaises(validation.InvalidMessage):
            validation.check_command({**command, "hot_pixel_threshold_dc": 500})
        with self.assertRaises(validation.InvalidMessage):
            validation.check_command({**command, "action": "thermal_threshold"})
