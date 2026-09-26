import json
from pathlib import Path

from django.test import SimpleTestCase

from ingest import validation
from ingest.validation import InvalidMessage

SAMPLES = [json.loads(line) for line in (Path(__file__).parent / "firmware_samples.jsonl").read_text().splitlines()]


def payload(obj) -> bytes:
    return json.dumps(obj).encode()


class FirmwareSamplesTest(SimpleTestCase):
    """Messages captured from the real firmware (controller running in QEMU) must be accepted."""

    def test_all_firmware_samples_are_valid(self):
        self.assertGreaterEqual(len(SAMPLES), 10)
        for sample in SAMPLES:
            with self.subTest(topic=sample["topic"], event=sample["payload"].get("event")):
                msg = validation.parse(sample["topic"], payload(sample["payload"]))
                self.assertEqual(msg.type, sample["payload"]["type"])

    def test_every_message_type_is_covered(self):
        types = {s["payload"]["type"] for s in SAMPLES}
        self.assertEqual(types, set(validation.TYPE_TO_DEF))


class MalformedMessageTest(SimpleTestCase):
    def sample(self, topic_suffix: str) -> dict:
        return next(s for s in SAMPLES if s["topic"].endswith(topic_suffix))

    def test_malformed_json(self):
        with self.assertRaisesMessage(InvalidMessage, "malformed JSON"):
            validation.parse("rb4107/controller/state", b'{"schema_version": 1,')

    def test_not_utf8(self):
        with self.assertRaisesMessage(InvalidMessage, "not UTF-8"):
            validation.parse("rb4107/controller/state", b"\xff\xfe")

    def test_not_an_object(self):
        with self.assertRaisesMessage(InvalidMessage, "not a JSON object"):
            validation.parse("rb4107/controller/state", b"[1, 2]")

    def test_unsupported_schema_version(self):
        data = dict(self.sample("controller/state")["payload"], schema_version=1)  # old single-node format
        with self.assertRaisesMessage(InvalidMessage, "unsupported schema_version 1"):
            validation.parse("rb4107/controller/state", payload(data))

    def test_missing_required_field(self):
        data = dict(self.sample("controller/state")["payload"])
        del data["safety"]
        with self.assertRaisesMessage(InvalidMessage, "'safety' is a required property"):
            validation.parse("rb4107/controller/state", payload(data))

    def test_wrong_field_type(self):
        data = json.loads(json.dumps(self.sample("controller/state")["payload"]))
        data["safety"]["shutdown"] = "yes"
        with self.assertRaisesMessage(InvalidMessage, "at safety/shutdown"):
            validation.parse("rb4107/controller/state", payload(data))

    def test_unknown_presence_must_be_null_not_false(self):
        data = json.loads(json.dumps(self.sample("presence")["payload"]))
        data["presence"] = {"valid": False, "detected": False, "moving": None, "stationary": None, "distance_m": None}
        with self.assertRaisesMessage(InvalidMessage, "presence/detected"):
            validation.parse(self.sample("presence")["topic"], payload(data))

    def test_offline_node_must_not_report_a_person(self):
        data = json.loads(json.dumps(self.sample("controller/state")["payload"]))
        data["nodes"][0].update(link="OFFLINE", valid=False, detected=True)
        with self.assertRaisesMessage(InvalidMessage, "nodes/0/detected"):
            validation.parse("rb4107/controller/state", payload(data))

    def test_type_must_match_topic(self):
        data = self.sample("controller/heartbeat")["payload"]
        with self.assertRaisesMessage(InvalidMessage, "not allowed on events/warning"):
            validation.parse("rb4107/events/warning", payload(data))

    def test_unknown_topic(self):
        with self.assertRaisesMessage(InvalidMessage, "unknown topic"):
            validation.parse("rb4107/controller/unknown", b"{}")

    def test_topic_outside_prefix(self):
        with self.assertRaisesMessage(InvalidMessage, "outside prefix"):
            validation.parse("other/controller/state", b"{}")

    def test_oversized_payload(self):
        with self.assertRaisesMessage(InvalidMessage, "too large"):
            validation.parse("rb4107/controller/state", b" " * (validation.MAX_PAYLOAD_BYTES + 1))

    def test_extra_fields_are_allowed(self):
        data = dict(self.sample("controller/state")["payload"], future_sensor={"co_ppm": 3})
        validation.parse("rb4107/controller/state", payload(data))
