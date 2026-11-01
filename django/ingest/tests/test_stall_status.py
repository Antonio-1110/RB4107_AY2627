"""The stall status rule (docs/dashboard.md, "Stall status";
classify_display_state() in ingest/locations.py).

One test per rule, plus the order in which they apply, then the same rule
end to end through the HTTP API with real firmware-shaped telemetry."""
import json
from datetime import timedelta

from django.test import SimpleTestCase, TestCase
from django.utils import timezone

from ingest.demo import demo_telemetry
from ingest.handlers import handle
from ingest.locations import SEVERITY_RANK, aggregate_stalls, classify_display_state, fleet_summary
from ingest.models import Device
from ingest.storage import worker_status
from ingest.validation import parse

STALE = 15
NODES = {"node_01": ("presence", "ONLINE", True), "node_02": ("presence", "ONLINE", True),
         "node_03": ("thermal", "ONLINE", True)}


def device(safety="idle", alarm="clear", relay="enabled", connection="online", age=1.0,
           nodes=None, node_age=None):
    """A serialized controller as views.serialize() builds it."""
    nodes = NODES if nodes is None else nodes
    sensors = {name: {"role": role, "link": link, "valid": valid} for name, (role, link, valid) in nodes.items()}
    ages = {"safety_state": age, "alarm_state": age, "relay_state": age}
    for name in nodes:
        ages[f"sensors.{name}.link"] = age if node_age is None else node_age.get(name, age)
    return {"device_id": "controller_01", "connection": connection, "stale_after_seconds": STALE,
            "values": {"safety_state": safety, "alarm_state": alarm, "relay_state": relay, "sensors": sensors},
            "field_age_seconds": ages}


def level(**kwargs):
    return classify_display_state(device(**kwargs))["level"]


def all_down(link="OFFLINE"):
    return {name: (role, link, False) for name, (role, _, _) in NODES.items()}


class StallStatusRuleTest(SimpleTestCase):
    # 5. normal
    def test_idle_or_monitoring_with_every_node_working_is_normal(self):
        for safety in ("idle", "monitoring"):
            with self.subTest(safety=safety):
                state = classify_display_state(device(safety=safety))
                self.assertEqual((state["level"], state["title"], state["reasons"]), ("normal", "NORMAL", []))
                self.assertFalse(state["active"])

    # 1. critical
    def test_supply_cut_is_critical_even_last_known(self):
        self.assertEqual(level(safety="shutdown", alarm="shutdown", relay="isolated"), "critical")
        self.assertEqual(level(safety="idle", relay="isolated"), "critical")
        state = classify_display_state(device(safety="shutdown", relay="isolated", connection="offline",
                                              nodes=all_down()))
        self.assertEqual(state["level"], "critical")
        self.assertTrue(state["last_known"])

    # 2. warning
    def test_warning_and_unattended_are_warnings_even_last_known(self):
        self.assertEqual(level(safety="warning", alarm="warning"), "warning")
        self.assertEqual(level(safety="unattended"), "warning")
        self.assertEqual(level(safety="monitoring", alarm="warning"), "warning")
        self.assertEqual(level(safety="unattended", connection="stale", nodes=all_down()), "warning")

    # 3. fault, F1
    def test_controller_fault_is_a_fault(self):
        state = classify_display_state(device(safety="fault", alarm="fault"))
        self.assertEqual((state["level"], state["title"]), ("fault", "SAFETY SYSTEM FAULT"))
        self.assertTrue(state["active"])
        self.assertEqual(level(safety="idle", alarm="fault"), "fault")

    # F2: the bug that started this, plus the old "connectivity issue" bucket
    def test_controller_not_online_is_a_fault(self):
        titles = {"offline": "CONTROLLER OFFLINE", "stale": "CONTROLLER NOT REPORTING",
                  "unknown": "CONTROLLER NEVER HEARD", "monitor_unavailable": "MONITOR UNAVAILABLE"}
        for connection, title in titles.items():
            with self.subTest(connection=connection):
                state = classify_display_state(device(connection=connection))
                self.assertEqual((state["level"], state["title"]), ("fault", title))
                self.assertTrue(state["last_known"])
                self.assertIn(f"controller {connection.replace('_', ' ')}", state["reasons"])

    # F3
    def test_old_safety_state_from_an_online_controller_is_a_fault(self):
        state = classify_display_state(device(age=STALE + 1))
        self.assertEqual((state["level"], state["title"]), ("fault", "SAFETY STATE NOT CURRENT"))
        state = classify_display_state(device(age=None))
        self.assertEqual(state["level"], "fault")

    # F4: the reported bug: controller online and idle, every node offline
    def test_all_nodes_offline_is_a_fault_not_normal(self):
        state = classify_display_state(device(nodes=all_down()))
        self.assertEqual((state["level"], state["title"]), ("fault", "SENSOR NODES DOWN"))
        self.assertEqual(state["reasons"], ["sensor node_01 offline", "sensor node_02 offline",
                                            "sensor node_03 offline"])

    def test_any_single_node_down_is_a_fault(self):
        for link, text in (("OFFLINE", "offline"), ("STALE", "stale"), ("NEVER_SEEN", "never seen")):
            with self.subTest(link=link):
                state = classify_display_state(device(nodes={**NODES, "node_02": ("presence", link, False)}))
                self.assertEqual((state["level"], state["title"]), ("fault", "SENSOR NODE DOWN"))
                self.assertEqual(state["reasons"], [f"sensor node_02 {text}"])

    def test_online_node_with_an_invalid_reading_is_a_fault(self):
        state = classify_display_state(device(nodes={**NODES, "node_03": ("thermal", "ONLINE", False)}))
        self.assertEqual(state["level"], "fault")
        self.assertEqual(state["reasons"], ["sensor node_03 invalid reading"])

    def test_node_dropped_from_the_controllers_list_is_ignored(self):
        # node_04 was configured once; the controller no longer lists it, so its
        # link was not refreshed by the latest telemetry.
        nodes = {**NODES, "node_04": ("presence", "OFFLINE", False)}
        self.assertEqual(level(nodes=nodes, node_age={"node_04": STALE + 60}), "normal")

    # F5
    def test_no_sensor_nodes_is_a_fault(self):
        state = classify_display_state(device(nodes={}))
        self.assertEqual((state["level"], state["title"]), ("fault", "NO SENSOR NODES"))

    # 4. unknown
    def test_starting_or_unrecognised_state_with_nodes_working_is_unknown(self):
        self.assertEqual(classify_display_state(device(safety="boot"))["title"], "CONTROLLER STARTING")
        self.assertEqual(level(safety="self_test"), "unknown")
        self.assertEqual(level(safety=None), "unknown")
        self.assertFalse(classify_display_state(device(safety="boot"))["active"])

    # Order: the first matching rule wins
    def test_rules_apply_in_order(self):
        # A cut supply outranks a fault; a fault outranks starting up.
        self.assertEqual(level(safety="shutdown", relay="isolated", nodes=all_down()), "critical")
        self.assertEqual(level(safety="boot", nodes=all_down("NEVER_SEEN")), "fault")
        # Several fault reasons: the title names the first, every reason is listed.
        state = classify_display_state(device(safety="fault", alarm="fault", connection="offline"))
        self.assertEqual(state["title"], "SAFETY SYSTEM FAULT")
        self.assertEqual(state["reasons"], ["controller reports a safety fault", "controller offline"])
        # Offline: node links are last known too, so only the controller is blamed.
        state = classify_display_state(device(connection="offline", nodes=all_down()))
        self.assertEqual(state["reasons"], ["controller offline"])

    def test_every_level_is_ranked_and_unknown_outranks_normal(self):
        self.assertEqual(sorted(SEVERITY_RANK, key=SEVERITY_RANK.get, reverse=True),
                         ["critical", "warning", "fault", "unknown", "normal"])


class StallAggregationTest(SimpleTestCase):
    def serialized(self, device_id, stall_id, **kwargs):
        result = device(**kwargs)
        result.update(device_id=device_id, last_seen_at=None,
                      location={"stall_id": stall_id, "stall_name": stall_id, "station_name": device_id,
                                "site_name": "s", "building": "b", "level": "l"},
                      cooking={"status": "not_cooking", "active": False})
        result["display_state"] = classify_display_state(result)
        return result

    def test_stall_shows_its_worst_controller_and_counts_once(self):
        stalls = aggregate_stalls([
            self.serialized("c1", "A"), self.serialized("c2", "A", nodes=all_down()),
            self.serialized("c3", "B"), self.serialized("c4", "B", safety="boot"),
            self.serialized("c5", "C", connection="stale"),
            self.serialized("c6", "D"),
        ])
        by_id = {stall["stall_id"]: stall["severity"] for stall in stalls}
        self.assertEqual(by_id, {"A": "fault", "B": "unknown", "C": "fault", "D": "normal"})
        summary = fleet_summary(stalls)
        self.assertEqual({key: summary[key] for key in ("critical", "warning", "fault", "unknown", "normal")},
                         {"critical": 0, "warning": 0, "fault": 2, "unknown": 1, "normal": 1})
        self.assertNotIn("connectivity_issues", summary)


class StallStatusApiTest(TestCase):
    """The same rule through /api/devices/ with firmware-shaped telemetry."""

    def setUp(self):
        worker_status(True)

    def send(self, link="ONLINE", valid=True):
        data = demo_telemetry("controller_01", scenario="idle")
        for node in data["nodes"]:
            node.update(link=link, valid=valid and link == "ONLINE")
            if link != "ONLINE":
                node["detected"] = None
        handle(parse("rb4107/controller/state", json.dumps(data).encode()))

    def stall(self):
        fleet = self.client.get("/api/devices/").json()
        return fleet["stalls"][0], fleet["summary"]

    def test_idle_controller_with_every_node_offline_shows_fault(self):
        self.send()
        stall, summary = self.stall()
        self.assertEqual((stall["severity"], summary["normal"]), ("normal", 1))
        self.send(link="OFFLINE")
        stall, summary = self.stall()
        self.assertEqual((stall["severity"], stall["state_title"]), ("fault", "SENSOR NODES DOWN"))
        self.assertEqual((summary["fault"], summary["normal"]), (1, 0))
        # The stall now also appears in the active alerts list.
        alerts = self.client.get("/api/devices/").json()["alerts"]
        self.assertEqual(alerts[0]["state"]["reasons"][0], "sensor node_01 offline")
        # Nodes back: normal again.
        self.send()
        self.assertEqual(self.stall()[0]["severity"], "normal")

    def test_silent_controller_shows_fault(self):
        self.send()
        Device.objects.update(last_seen_at=timezone.now() - timedelta(minutes=5))
        stall, summary = self.stall()
        self.assertEqual((stall["severity"], stall["state_title"]), ("fault", "CONTROLLER NOT REPORTING"))
        self.assertEqual(summary["fault"], 1)
