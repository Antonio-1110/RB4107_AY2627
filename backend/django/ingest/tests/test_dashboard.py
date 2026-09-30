"""Contract tests from validated firmware messages to the dashboard's HTTP API."""
import copy
import json
from dataclasses import replace
from datetime import timedelta
from django.test import TestCase
from django.utils import timezone

from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.models import Device, InboundMessage, Reading, WorkerStatus
from ingest.storage import worker_status
from ingest.validation import InvalidMessage, parse


class DashboardIntegrationTest(TestCase):
    def setUp(self):
        worker_status(True)

    def send(self, state="MONITORING", **changes):
        data = demo_telemetry("controller_01")
        data["safety"].update(state=state, shutdown=state == "SHUTDOWN",
                              buzzer=state if state in {"SHUTDOWN", "WARNING", "FAULT"} else "OFF")
        data.update(changes)
        msg = parse("rb4107/controller/state", json.dumps(data).encode())
        handle(msg)
        return msg

    def latest(self):
        return self.client.get("/api/devices/controller_01/latest/").json()

    def test_firmware_payload_reaches_dashboard_and_history(self):
        msg = self.send()
        response = self.latest()
        self.assertEqual(response["values"]["temperature_c"], msg.data["thermal"]["max_c"])
        self.assertIs(response["values"]["occupied"], True)
        self.assertEqual(response["connection"], "online")
        self.assertEqual(response["location"]["stall_id"], "AES-BENCH-01")
        self.assertEqual(len(response["values"]["sensors"]), 3)
        self.assertEqual(len(self.client.get("/api/devices/controller_01/history/").json()["points"]), 1)
        self.assertContains(self.client.get("/"), "v2.2")

    def test_shutdown_and_warning_have_distinct_reset_requirements(self):
        self.send("WARNING")
        values = self.latest()["values"]
        self.assertEqual(values["relay_state"], "enabled")
        self.assertFalse(values["manual_reset_required"])
        self.send("SHUTDOWN")
        values = self.latest()["values"]
        self.assertEqual(values["relay_state"], "isolated")
        self.assertTrue(values["manual_reset_required"])
        self.send("IDLE")
        self.assertFalse(self.latest()["values"]["manual_reset_required"])
        self.assertEqual(self.client.get("/api/devices/").json()["alerts"], [])

    def test_shutdown_word_without_shutdown_output_does_not_require_reset(self):
        msg = self.send()
        data = copy.deepcopy(msg.data)
        data["safety"].update(state="SHUTDOWN", shutdown=False)
        handle(parse(msg.topic, json.dumps(data).encode()))
        self.assertFalse(self.latest()["values"]["manual_reset_required"])

    def test_unknown_presence_and_invalid_thermal_do_not_become_safe_readings(self):
        msg = self.send(presence_state="UNKNOWN")
        data = copy.deepcopy(msg.data)
        data["thermal"]["valid"] = False
        handle(parse(msg.topic, json.dumps(data).encode()))
        values = self.latest()["values"]
        self.assertIsNone(values["occupied"])
        self.assertIsNone(values["temperature_c"])
        self.assertIsNone(values["cooking_state"])
        self.assertIsNone(values["warning_after_seconds"])

    def test_retained_shutdown_bootstraps_last_known_without_online_or_graph_point(self):
        data = demo_telemetry("controller_01", index=1)
        msg = replace(parse("rb4107/controller/state", json.dumps(data).encode()), retained=True)
        handle(msg)
        response = self.latest()
        self.assertEqual(response["connection"], "unknown")
        self.assertTrue(response["display_state"]["last_known"])
        self.assertTrue(response["values"]["manual_reset_required"])
        self.assertIsNone(response["field_age_seconds"]["relay_state"])
        self.assertFalse(Reading.objects.exists())

    def test_retained_normal_does_not_clear_known_isolation(self):
        self.send("SHUTDOWN")
        msg = parse("rb4107/controller/state", json.dumps(demo_telemetry("controller_01")).encode())
        handle(replace(msg, retained=True))
        self.assertTrue(self.latest()["values"]["manual_reset_required"])

    def test_retained_online_never_proves_liveness_and_lwt_marks_offline(self):
        self.send("SHUTDOWN")
        for online in [False, True]:
            data = {"schema_version": 2, "type": "controller_status", "controller_id": "controller_01", "online": online}
            handle(replace(parse("rb4107/controller/status", json.dumps(data).encode()), retained=True))
        response = self.latest()
        self.assertEqual(response["connection"], "offline")
        self.assertTrue(response["values"]["manual_reset_required"])

    def test_queued_event_is_logged_without_overwriting_current_snapshot(self):
        self.send("SHUTDOWN")
        data = {"schema_version": 2, "type": "event", "controller_id": "controller_01", "timestamp": None,
                "uptime_ms": 20, "sequence": 2, "event": "warning", "fault": None,
                "safety": {"state": "WARNING", "from_state": "UNATTENDED", "reason": "queued earlier warning", "unattended_ms": 60000}}
        handle(parse("rb4107/events/warning", json.dumps(data).encode()))
        self.assertEqual(self.latest()["values"]["safety_state"], "shutdown")
        event = self.client.get("/api/devices/controller_01/events/").json()["events"][0]
        self.assertEqual(event["detail"]["note"], "queued earlier warning")

    def test_old_timestamp_does_not_clear_current_shutdown(self):
        self.send("SHUTDOWN")
        old = demo_telemetry("controller_01")
        old["timestamp"] = (timezone.now() - timedelta(hours=1)).isoformat()
        handle(parse("rb4107/controller/state", json.dumps(old).encode()))
        self.assertTrue(self.latest()["values"]["manual_reset_required"])
        self.assertTrue(InboundMessage.objects.filter(outcome="historical").exists())

    def test_duplicate_transport_is_deduplicated_but_identical_new_message_is_not(self):
        msg = self.send()
        handle(replace(msg, duplicate=True))
        self.assertEqual(Reading.objects.count(), 1)
        handle(msg)
        self.assertEqual(Reading.objects.count(), 2)

    def test_future_controller_clock_does_not_block_later_reset_report(self):
        self.send("SHUTDOWN", timestamp=(timezone.now() + timedelta(hours=1)).isoformat())
        self.send("IDLE")
        self.assertFalse(self.latest()["values"]["manual_reset_required"])

    def test_dead_subscriber_marks_all_values_last_known(self):
        self.send("SHUTDOWN")
        WorkerStatus.objects.update(heartbeat_at=timezone.now() - timedelta(minutes=1))
        response = self.latest()
        self.assertEqual(response["connection"], "monitor_unavailable")
        self.assertTrue(response["display_state"]["last_known"])
        self.assertTrue(response["values"]["manual_reset_required"])

    def test_heartbeat_does_not_refresh_old_sensor_or_relay_fields(self):
        self.send("SHUTDOWN")
        device = Device.objects.get(device_id="controller_01")
        old = (timezone.now() - timedelta(minutes=1)).isoformat()
        device.field_updated_at = {key: old for key in device.field_updated_at}
        device.save()
        data = {"schema_version": 2, "type": "heartbeat", "controller_id": "controller_01", "timestamp": None,
                "uptime_ms": 130000, "sequence": 12, "safety_state": "SHUTDOWN", "safety_loop_count": 100}
        handle(parse("rb4107/controller/heartbeat", json.dumps(data).encode()))
        self.assertTrue(self.latest()["display_state"]["last_known"])
        self.assertGreater(self.latest()["field_age_seconds"]["temperature_c"], 59)

    def test_namespaced_controllers_and_same_stall_aggregate(self):
        for ident in ["t3-kopitiam-s01", "t3-kopitiam-s01-b"]:
            msg = parse(f"rb4107/{ident}/controller/state", json.dumps(demo_telemetry(ident)).encode(), "rb4107/+")
            handle(msg)
        data = self.client.get("/api/devices/").json()
        self.assertEqual(len(data["devices"]), 2)
        self.assertEqual(len(data["stalls"]), 1)
        self.assertEqual(data["stalls"][0]["station_count"], 2)

    def test_apis_are_read_only_and_validate_history_window(self):
        self.send()
        for endpoint in ["/api/devices/", "/api/health/", "/api/devices/controller_01/latest/",
                         "/api/devices/controller_01/history/", "/api/devices/controller_01/events/"]:
            self.assertEqual(self.client.post(endpoint, {}).status_code, 405)
        self.assertEqual(self.client.get("/api/devices/controller_01/history/?hours=nan").status_code, 400)
        self.assertEqual(self.client.get("/api/devices/missing/latest/").status_code, 404)

    def test_malformed_type_and_nonfinite_json_are_rejected(self):
        for payload in [b'{"schema_version":[],"type":"telemetry"}',
                        b'{"schema_version":2,"type":[]}', b'{"value":NaN}', b'{"value":1e999}']:
            with self.assertRaises(InvalidMessage):
                parse("rb4107/controller/state", payload)
