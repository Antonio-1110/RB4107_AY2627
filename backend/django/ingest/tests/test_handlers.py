import json
from pathlib import Path

from django.test import SimpleTestCase

from ingest import handlers, validation

SAMPLES = [json.loads(line) for line in (Path(__file__).parent / "firmware_samples.jsonl").read_text().splitlines()]


def parsed(topic_suffix: str, event: str | None = None) -> validation.Message:
    sample = next(s for s in SAMPLES if s["topic"].endswith(topic_suffix) and s["payload"].get("event") == event)
    return validation.parse(sample["topic"], json.dumps(sample["payload"]).encode())


class HandlerLoggingTest(SimpleTestCase):
    def test_telemetry_is_logged(self):
        with self.assertLogs("rb4107.telemetry", "INFO") as logs:
            handlers.handle(parsed("controller/state"))
        self.assertIn("state=", logs.output[0])

    def test_warning_and_shutdown_events_log_at_warning_level(self):
        for topic, event in (("events/warning", "warning"), ("events/shutdown", "shutdown")):
            with self.subTest(event=event), self.assertLogs("rb4107.event", "WARNING") as logs:
                handlers.handle(parsed(topic, event))
            self.assertIn(event.upper(), logs.output[0])

    def test_fault_events_are_logged(self):
        with self.assertLogs("rb4107.fault", "WARNING") as logs:
            handlers.handle(parsed("events/fault", "fault_raised"))
        self.assertIn("RAISED", logs.output[0])

    def test_every_type_has_a_handler(self):
        self.assertEqual(set(handlers.HANDLERS), set(validation.TYPE_TO_DEF))
