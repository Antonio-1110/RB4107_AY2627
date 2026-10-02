"""Remote C4002 tuning: the command endpoint and the node's answer on the dashboard."""
import json
from pathlib import Path
from unittest import mock

from django.test import TestCase

from ingest import commands, validation
from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.storage import worker_status
from ingest.validation import parse

SAMPLES = [json.loads(line) for line in (Path(__file__).parent / "firmware_samples.jsonl").read_text().splitlines()]
CONFIG_SAMPLES = [s for s in SAMPLES if s["payload"]["type"] == "c4002_config"]
URL = "/api/devices/controller_01/nodes/node_01/c4002/"


class C4002CommandTest(TestCase):
    def setUp(self):
        worker_status(True)
        handle(parse("rb4107/controller/state", json.dumps(demo_telemetry("controller_01")).encode()))

    def post(self, body):
        return self.client.post(URL, json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_apply_is_published_on_the_node_topic(self, publish):
        response = self.post({"action": "apply", "range_max_cm": 300, "presence_sensitivity": "low"})
        self.assertEqual(response.status_code, 202, response.content)
        topic, command = publish.call_args.args
        self.assertEqual(topic, "rb4107/sensors/node_01/c4002_set")
        self.assertEqual(command["type"], "c4002_command")
        self.assertEqual(command["controller_id"], "controller_01")
        self.assertEqual(command["range_max_cm"], 300)
        self.assertEqual(response.json()["request_id"], command["request_id"])
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_commands_are_refused_before_sending(self, publish):
        for body in [{"action": "apply"}, {"action": "apply", "range_max_cm": 5000},
                     {"action": "apply", "range_min_cm": 500, "range_max_cm": 100},
                     {"action": "read", "range_max_cm": 300}, {"action": "explode"},
                     {"action": "apply", "rang_max_cm": 300},
                     {"action": "apply", "motion_gates": [1, 0]},
                     {"action": "calibrate", "calibration_duration_s": 0}]:
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    @mock.patch("ingest.commands.publish")
    def test_only_presence_nodes_can_be_tuned(self, publish):
        response = self.client.post("/api/devices/controller_01/nodes/node_03/c4002/", '{"action":"read"}',
                                    content_type="application/json")
        self.assertEqual(response.status_code, 404)
        self.assertEqual(self.client.get(URL).status_code, 405)
        publish.assert_not_called()

    @mock.patch("ingest.commands.publish", side_effect=commands.BrokerUnavailable("down"))
    def test_broker_down_is_reported(self, publish):
        self.assertEqual(self.post({"action": "read"}).status_code, 503)

    def test_node_answer_shows_in_latest_and_failures_keep_last_settings(self):
        ok, no_reply = CONFIG_SAMPLES
        handle(parse(ok["topic"], json.dumps(ok["payload"]).encode()))
        tuning = self.client.get("/api/devices/controller_01/latest/").json()["values"]["sensors"]["node_01"]["c4002"]
        self.assertEqual(tuning["result"], "ok")
        self.assertEqual(tuning["settings"]["range_max_cm"], 350)
        self.assertEqual(tuning["settings"]["presence_gates"][7], 0)

        handle(parse(no_reply["topic"], json.dumps(no_reply["payload"]).encode()))
        tuning = self.client.get("/api/devices/controller_01/latest/").json()["values"]["sensors"]["node_01"]["c4002"]
        self.assertEqual(tuning["result"], "no_reply")
        self.assertEqual(tuning["request_id"], 4712)
        self.assertEqual(tuning["settings"]["range_max_cm"], 350)

    def test_subscriber_ignores_its_own_commands(self):
        self.assertTrue(validation.is_command_topic("rb4107/sensors/node_01/c4002_set"))
        self.assertFalse(validation.is_command_topic("rb4107/sensors/node_01/c4002_config"))
        with self.assertRaises(validation.InvalidMessage):
            parse("rb4107/sensors/node_01/c4002_set", b'{"schema_version":2,"type":"c4002_command"}')
