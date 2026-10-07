"""Contract tests from validated firmware messages to the dashboard's HTTP API."""
import copy
import json
from dataclasses import replace
from datetime import timedelta
from django.conf import settings
from django.test import TestCase, override_settings
from django.utils import timezone

from ingest.demo import demo_telemetry, demo_thermal_frame
from ingest.handlers import handle
from ingest.models import Device, InboundMessage, Reading, ThermalFrame, WorkerStatus
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

    def test_cooking_follows_the_firmware_state(self):
        hot = {"valid": True, "max_c": 90.0, "min_c": 24.0, "mean_c": 40.0, "hot_region_c": 85.0,
               "rate_c_per_min": 0.0, "pixels_above_threshold": 30}
        cases = {"IDLE": "not_cooking", "MONITORING": "cooking", "UNATTENDED": "cooking",
                 "WARNING": "cooking", "SHUTDOWN": "supply_cut"}
        for state, status in cases.items():
            with self.subTest(state=state):
                self.send(state, thermal=hot)
                cooking = self.latest()["cooking"]
                self.assertEqual(cooking["status"], status)
                self.assertEqual(cooking["active"], status == "cooking")
                self.assertFalse(cooking["last_known"])
        # The state can't say in FAULT: guessed from the hot region, and labelled as a guess.
        self.send("FAULT", thermal=hot)
        self.assertEqual(self.latest()["cooking"]["status"], "likely_cooking")
        self.send("FAULT", thermal={**hot, "max_c": 30.0, "hot_region_c": 28.0})
        self.assertEqual(self.latest()["cooking"]["status"], "likely_not_cooking")
        self.send("FAULT", thermal={**hot, "valid": False})
        self.assertEqual(self.latest()["cooking"]["status"], "unknown")

    def test_stall_and_summary_count_cooking(self):
        self.send("UNATTENDED")
        fleet = self.client.get("/api/devices/").json()
        self.assertEqual(fleet["summary"]["cooking"], 1)
        self.assertEqual(fleet["stalls"][0]["cooking"]["status"], "cooking")
        self.assertEqual(fleet["stalls"][0]["cooking"]["stations_cooking"], 1)

    def test_firmware_payload_reaches_dashboard_and_history(self):
        msg = self.send()
        response = self.latest()
        self.assertEqual(response["values"]["temperature_c"], msg.data["thermal"]["max_c"])
        self.assertIs(response["values"]["occupied"], True)
        self.assertEqual(response["connection"], "online")
        self.assertEqual(response["location"]["stall_id"], "BENCH-01")
        self.assertEqual(len(response["values"]["sensors"]), 3)
        self.assertEqual(len(self.client.get("/api/devices/controller_01/history/").json()["points"]), 1)
        self.assertContains(self.client.get("/"), "js/dashboard.js")

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

    def send_boot(self, boot_id, sequence, state="MONITORING"):
        data = demo_telemetry("controller_01", boot_id=boot_id)
        data["sequence"] = sequence
        data["safety"].update(state=state, shutdown=state == "SHUTDOWN", reset_required=state == "SHUTDOWN")
        handle(parse("rb4107/controller/state", json.dumps(data).encode()))

    def test_firmware_reset_flag_and_timer_settings_are_used(self):
        self.send_boot("aaaa0001", 1, "SHUTDOWN")
        values = self.latest()["values"]
        self.assertTrue(values["manual_reset_required"])
        self.assertEqual(values["reset_status_source"], "firmware")
        self.assertEqual(values["warning_after_seconds"], 60)
        self.assertEqual(values["shutdown_after_seconds"], 90)
        self.assertEqual(values["shutdown_counts_from"], "UNATTENDED")
        self.assertEqual(values["boot_id"], "aaaa0001")

    def test_older_message_in_same_boot_does_not_overwrite_state(self):
        self.send_boot("aaaa0001", 10, "SHUTDOWN")
        self.send_boot("aaaa0001", 5, "MONITORING")   # late, no timestamps needed to spot it
        self.send_boot("aaaa0001", 10, "MONITORING")  # same sequence again
        self.assertEqual(self.latest()["values"]["safety_state"], "shutdown")
        outcomes = list(InboundMessage.objects.order_by("id").values_list("outcome", flat=True))
        self.assertEqual(outcomes, ["accepted", "historical", "duplicate"])
        self.assertEqual(Reading.objects.count(), 2)  # the duplicate is not plotted twice

    def test_new_boot_id_is_a_restart_even_with_lower_sequence(self):
        self.send_boot("aaaa0001", 500, "SHUTDOWN")
        self.send_boot("bbbb0002", 3, "IDLE")
        self.assertEqual(self.latest()["values"]["safety_state"], "idle")
        events = self.client.get("/api/devices/controller_01/events/").json()["events"]
        self.assertEqual([event["event_type"] for event in events], ["controller_restarted"])
        self.assertEqual(events[0]["detail"]["previous_boot_id"], "aaaa0001")

    @override_settings(LOCATION_CATALOG_FILE=settings.BASE_DIR / "locations.demo.json")
    def test_namespaced_controllers_and_same_stall_aggregate(self):
        for ident in ["demo-t3-foodcourt-a", "demo-t3-foodcourt-b"]:
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


class HeatMapTests(TestCase):
    TOPIC = "rb4107/sensors/node_03/thermal_frame"

    def send_frame(self, tick=0):
        msg = parse(self.TOPIC, json.dumps(demo_thermal_frame("controller_01", 0, tick)).encode())
        handle(msg)
        return msg

    def test_latest_picture_reaches_the_api(self):
        handle(parse("rb4107/controller/state", json.dumps(demo_telemetry("controller_01")).encode()))
        self.send_frame(tick=1)
        msg = self.send_frame(tick=2)
        response = self.client.get("/api/devices/controller_01/thermal_frame/").json()
        self.assertEqual(len(response["frames"]), 1)  # only the newest picture is kept
        frame = response["frames"][0]
        self.assertEqual(frame["sensor_node"], "node_03")
        self.assertEqual(frame["number"], msg.data["frame"]["number"])
        self.assertEqual(frame["pixels"], msg.data["frame"]["pixels"])
        self.assertEqual((frame["width"], frame["height"]), (32, 24))
        self.assertLess(frame["age_seconds"], 5)

    def test_pictures_are_not_logged_or_mixed_into_telemetry(self):
        handle(parse("rb4107/controller/state", json.dumps(demo_telemetry("controller_01")).encode()))
        before = (InboundMessage.objects.count(), Reading.objects.count())
        latest = self.client.get("/api/devices/controller_01/latest/").json()["values"]
        for tick in range(5):
            self.send_frame(tick)
        self.assertEqual((InboundMessage.objects.count(), Reading.objects.count()), before)
        self.assertEqual(ThermalFrame.objects.count(), 1)
        self.assertEqual(self.client.get("/api/devices/controller_01/latest/").json()["values"], latest)

    def test_endpoint_is_read_only_and_404s_for_unknown_devices(self):
        self.send_frame()
        self.assertEqual(self.client.post("/api/devices/controller_01/thermal_frame/", {}).status_code, 405)
        self.assertEqual(self.client.get("/api/devices/missing/thermal_frame/").status_code, 404)
        handle(parse("rb4107/controller/state", json.dumps(demo_telemetry("controller_02")).encode()))
        self.assertEqual(self.client.get("/api/devices/controller_02/thermal_frame/").json()["frames"], [])


class FrontendServingTests(TestCase):
    def test_serves_dashboard_page_and_assets(self):
        self.assertContains(self.client.get("/"), "js/dashboard.js")
        for asset in ["/css/dashboard.css", "/js/dashboard.js", "/js/api.js"]:
            self.assertEqual(self.client.get(asset).status_code, 200, asset)

    def test_does_not_serve_files_outside_assets(self):
        self.assertEqual(self.client.get("/js/../index.html").status_code, 404)
        self.assertEqual(self.client.get("/README.md").status_code, 404)
