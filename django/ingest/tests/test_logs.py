"""The controller's log lines (controller/log) and the log level command: docs/logging.md."""
import json
from datetime import timedelta
from pathlib import Path
from unittest import mock

from django.test import TestCase
from django.utils import timezone

from ingest import retention, validation
from ingest.handlers import handle
from ingest.models import InboundMessage, LogLine
from ingest.storage import worker_status
from ingest.validation import InvalidMessage, parse

SAMPLE = next(json.loads(line) for line in (Path(__file__).parent / "firmware_samples.jsonl").read_text().splitlines()
              if json.loads(line)["topic"].endswith("/controller/log"))
URL = "/api/devices/controller_01"


def log_message(lines, levels=None, dropped=0, boot_id="3f9a01c2"):
    payload = dict(SAMPLE["payload"], boot_id=boot_id, dropped=dropped, levels=levels or {"*": "info"},
                   lines=[{"uptime_ms": 1000 + i, "level": level, "tag": tag, "msg": msg}
                          for i, (level, tag, msg) in enumerate(lines)])
    return parse("rb4107/controller/log", json.dumps(payload).encode())


class ControllerLogStorageTest(TestCase):
    def setUp(self):
        worker_status(True)

    def test_lines_and_levels_are_stored(self):
        handle(parse(SAMPLE["topic"], json.dumps(SAMPLE["payload"]).encode()))
        body = self.client.get(f"{URL}/logs/").json()
        self.assertEqual([(l["level"], l["tag"]) for l in body["lines"]], [("I", "MQTT"), ("W", "SAFETY"), ("E", "THERMAL")])
        self.assertEqual(body["lines"][1]["msg"], "UNATTENDED -> WARNING (unattended timeout)")
        self.assertEqual(body["lines"][1]["boot_id"], "3f9a01c2")
        self.assertEqual(body["levels"], {"*": "info", "SAFETY": "debug", "TELEMETRY": "warn"})
        self.assertEqual(body["dropped"], 3)
        self.assertEqual(body["tags"], ["MQTT", "SAFETY", "THERMAL"])
        self.assertEqual(body["last_id"], body["lines"][-1]["id"])
        self.assertFalse(InboundMessage.objects.exists())  # log batches are not kept twice

    def test_levels_only_message_updates_levels(self):
        handle(log_message([], levels={"*": "warn", "NET": "debug"}))
        body = self.client.get(f"{URL}/logs/").json()
        self.assertEqual(body["lines"], [])
        self.assertEqual(body["levels"], {"*": "warn", "NET": "debug"})

    def test_filters_and_after(self):
        handle(log_message([("I", "NET", "link up"), ("W", "MQTT", "broker disconnected"),
                            ("D", "SAFETY", "tick"), ("E", "THERMAL", "frame read failed")]))
        get = lambda query: [l["msg"] for l in self.client.get(f"{URL}/logs/?{query}").json()["lines"]]
        self.assertEqual(get("level=W"), ["broker disconnected", "frame read failed"])
        self.assertEqual(get("level=i"), ["link up", "broker disconnected", "frame read failed"])
        self.assertEqual(get("tag=SAFETY"), ["tick"])
        self.assertEqual(get("q=BROKER"), ["broker disconnected"])
        self.assertEqual(get("q=therm"), ["frame read failed"])  # the tag matches too
        self.assertEqual(get("limit=2"), ["tick", "frame read failed"])  # the newest, oldest first
        first = LogLine.objects.order_by("id").first().id
        self.assertEqual(get(f"after={first}&limit=2"), ["broker disconnected", "tick"])
        for query in ("level=X", "limit=0", "limit=5000", "after=-1", "after=x"):
            self.assertEqual(self.client.get(f"{URL}/logs/?{query}").status_code, 400, query)

    def test_unknown_device(self):
        self.assertEqual(self.client.get("/api/devices/nobody/logs/").status_code, 404)

    def test_bad_messages_are_refused(self):
        payload = dict(SAMPLE["payload"])
        for change in ({"lines": [{"uptime_ms": 1, "level": "X", "tag": "A", "msg": "m"}]},
                       {"lines": [{"uptime_ms": 1, "level": "I", "tag": "", "msg": "m"}]},
                       {"lines": [{"uptime_ms": 1, "level": "I", "tag": "A", "msg": "m" * 200}]},
                       {"levels": {"SAFETY": "debug"}},  # no "*"
                       {"levels": {"*": "loud"}}):
            with self.subTest(change=change), self.assertRaises(InvalidMessage):
                parse("rb4107/controller/log", json.dumps(dict(payload, **change)).encode())
        with self.assertRaises(InvalidMessage):  # only on its own topic
            parse("rb4107/controller/state", json.dumps(payload).encode())

    def test_old_lines_are_pruned(self):
        handle(log_message([("I", "NET", "old"), ("I", "NET", "new")]))
        LogLine.objects.filter(msg="old").update(received_at=timezone.now() - timedelta(days=8))
        result = retention.prune(policy=retention.Policy(log_days=7))
        self.assertEqual(result["log_lines"], 1)
        self.assertEqual(list(LogLine.objects.values_list("msg", flat=True)), ["new"])


class LogLevelCommandTest(TestCase):
    def setUp(self):
        worker_status(True)
        handle(log_message([]))

    def post(self, body):
        return self.client.post(f"{URL}/log-level/", json.dumps(body), content_type="application/json")

    @mock.patch("ingest.commands.publish")
    def test_level_is_published(self, publish):
        for tag, level in (("SAFETY", "debug"), ("*", "warn"), ("esp-tls", "none"), ("wifi", "verbose")):
            with self.subTest(tag=tag):
                self.assertEqual(self.post({"tag": tag, "level": level}).status_code, 202)
                topic, command = publish.call_args.args
                self.assertEqual(topic, "rb4107/controller/command")
                self.assertEqual((command["action"], command["log_tag"], command["log_level"]), ("log_level", tag, level))
                validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_defaults(self, publish):
        self.assertEqual(self.post({"defaults": True}).status_code, 202)
        command = publish.call_args.args[1]
        self.assertEqual(command["action"], "log_level_defaults")
        self.assertNotIn("log_tag", command)
        validation.check_command(command)

    @mock.patch("ingest.commands.publish")
    def test_bad_values_are_refused(self, publish):
        for body in ({}, {"tag": "SAFETY"}, {"level": "info"}, {"tag": "SAFETY", "level": "loud"},
                     {"tag": "", "level": "info"}, {"tag": "A" * 16, "level": "info"}, {"tag": "a b", "level": "info"},
                     {"tag": "S*", "level": "info"}, {"tag": 3, "level": "info"},
                     {"tag": "SAFETY", "level": "info", "x": 1}, {"defaults": False}, ["SAFETY"]):
            with self.subTest(body=body):
                self.assertEqual(self.post(body).status_code, 400)
        publish.assert_not_called()

    def test_the_schema_refuses_log_keys_on_another_action(self):
        with self.assertRaises(validation.InvalidMessage):
            validation.check_command({"schema_version": 2, "type": "controller_command", "controller_id": "c",
                                      "request_id": 1, "action": "reset", "log_tag": "SAFETY"})
