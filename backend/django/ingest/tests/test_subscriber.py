"""Subscriber tests.

Unit tests drive the paho callbacks directly. The integration test talks to
a real Mosquitto on RB4107_MQTT_HOST:RB4107_MQTT_PORT and is skipped when no
broker is running (start one with tools/mqtt/start_broker.sh).
"""

import json
import socket
import threading
import time
import uuid
from pathlib import Path
from types import SimpleNamespace

import paho.mqtt.client as mqtt
from django.conf import settings
from django.test import SimpleTestCase

from ingest.subscriber import Subscriber

SAMPLES = [json.loads(line) for line in (Path(__file__).parent / "firmware_samples.jsonl").read_text().splitlines()]


def config(**overrides) -> dict:
    cfg = dict(settings.RB4107_MQTT)
    cfg.update(CLIENT_ID=f"rb4107-test-{uuid.uuid4().hex[:8]}", PERSISTENT_SESSION=False, **overrides)
    return cfg


def fake_message(topic: str, payload: bytes):
    return SimpleNamespace(topic=topic, payload=payload)


class SubscriberCallbackTest(SimpleTestCase):
    def test_valid_message_is_dispatched(self):
        handled = []
        sub = Subscriber(config(), handle=handled.append)
        sample = SAMPLES[0]
        sub._on_message(None, None, fake_message(sample["topic"], json.dumps(sample["payload"]).encode()))
        self.assertEqual(len(handled), 1)
        self.assertEqual(sub.rejected, 0)

    def test_malformed_message_is_rejected_not_raised(self):
        sub = Subscriber(config(), handle=lambda m: self.fail("must not be handled"))
        with self.assertLogs("rb4107.mqtt", "WARNING") as logs:
            sub._on_message(None, None, fake_message("rb4107/controller/state", b"not json"))
        self.assertEqual(sub.rejected, 1)
        self.assertIn("rejected message", logs.output[0])

    def test_handler_exception_does_not_escape(self):
        def broken(msg):
            raise RuntimeError("bug in handler")

        sub = Subscriber(config(), handle=broken)
        sample = SAMPLES[0]
        with self.assertLogs("rb4107.mqtt", "ERROR"):
            sub._on_message(None, None, fake_message(sample["topic"], json.dumps(sample["payload"]).encode()))


def broker_available() -> bool:
    try:
        with socket.create_connection((settings.RB4107_MQTT["HOST"], settings.RB4107_MQTT["PORT"]), timeout=0.5):
            return True
    except OSError:
        return False


class SubscriberBrokerIntegrationTest(SimpleTestCase):
    def setUp(self):
        if not broker_available():
            self.skipTest("no MQTT broker running")

    def test_receives_validates_and_stops_gracefully(self):
        prefix = f"rb4107test{uuid.uuid4().hex[:6]}"
        handled = []
        sub = Subscriber(config(TOPIC=f"{prefix}/#"), handle=handled.append)
        thread = threading.Thread(target=sub.run_forever, daemon=True)
        thread.start()
        self.assertTrue(sub.connected.wait(5), "subscriber did not connect")
        time.sleep(0.3)  # let the SUBSCRIBE complete

        pub = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        pub.connect(settings.RB4107_MQTT["HOST"], settings.RB4107_MQTT["PORT"])
        pub.loop_start()
        for sample in SAMPLES:
            topic = sample["topic"].replace("rb4107/", f"{prefix}/", 1)
            pub.publish(topic, json.dumps(sample["payload"]), qos=1).wait_for_publish(2)
        pub.publish(f"{prefix}/controller/state", "garbage", qos=1).wait_for_publish(2)
        pub.loop_stop()
        pub.disconnect()

        deadline = time.time() + 5
        while (len(handled) < len(SAMPLES) or sub.rejected < 1) and time.time() < deadline:
            time.sleep(0.05)
        self.assertEqual(len(handled), len(SAMPLES))
        self.assertEqual(sub.rejected, 1)

        sub.stop()
        thread.join(5)
        self.assertFalse(thread.is_alive(), "run_forever() did not return after stop()")
