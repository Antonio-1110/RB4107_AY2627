"""Simulated stalls (simulate_fleet): valid firmware-shaped messages for each scenario."""
import json

from django.test import SimpleTestCase

from ingest.demo import SCENARIOS, demo_telemetry, demo_thermal_frame, network_phase
from ingest.management.commands.simulate_fleet import Command
from ingest.validation import parse

PREFIX = "rb4107/+"


def send(topic, data):
    return parse(topic, json.dumps(data).encode(), PREFIX)


class DemoScenarioTests(SimpleTestCase):
    def test_every_scenario_passes_the_schema(self):
        for scenario in SCENARIOS:
            for tick in (0, 17, 90):
                with self.subTest(scenario=scenario, tick=tick):
                    send("rb4107/demo/controller/state", demo_telemetry("demo", 0, tick, "b00t", scenario))
                    send("rb4107/demo/sensors/node_03/thermal_frame",
                         demo_thermal_frame("demo", 0, tick, "b00t", scenario))

    def test_states_follow_the_firmware_rules(self):
        state = lambda scenario, tick=0: demo_telemetry("demo", 0, tick, "b00t", scenario)
        # A radar node offline raises its fault but is not FAULT: the other radar still sees the cook.
        offline = state("fault_node_offline")
        self.assertEqual(offline["safety"]["state"], "MONITORING")
        self.assertEqual(offline["faults"], ["presence_b_node_offline"])
        self.assertEqual(offline["presence_state"], "PRESENT")
        # Both radars lost while cooking: presence is unknown, flagged, and counts as absent (the timeline runs).
        radars = state("fault_radars")
        self.assertEqual(radars["presence_state"], "UNKNOWN")
        self.assertEqual(radars["safety"]["state"], "UNATTENDED")
        self.assertTrue(radars["safety"]["presence_unknown"])
        self.assertFalse(radars["safety"]["thermal_lost"])
        self.assertEqual(state("idle")["presence_state"], "ABSENT")
        # The timers stay inside their state's window while counting.
        for tick in range(0, 120, 7):
            self.assertLess(state("unattended", tick)["safety"]["unattended_ms"], 150000)
            self.assertTrue(150000 <= state("warning", tick)["safety"]["unattended_ms"] < 300000)
        # Hot oil: the temperature tier gives much shorter limits than the simmering pots.
        hot = state("hot_pan")
        self.assertEqual(hot["safety"]["tier"], "tier4")
        self.assertEqual(hot["safety"]["effective_warning_ms"], 60000)
        self.assertGreaterEqual(hot["thermal"]["max_c"], 200.0)
        for tick in range(0, 60, 3):
            self.assertLess(state("hot_pan", tick)["safety"]["unattended_ms"], 60000)
        # Switched off and left: no countdown yet while the pan falls (rule D6), and the time away runs.
        for tick in range(0, 60, 4):
            check = state("checking", tick)
            self.assertEqual((check["safety"]["state"], check["safety"]["hold"]), ("MONITORING", "checking"))
            self.assertEqual(check["presence_state"], "ABSENT")
            self.assertLess(check["thermal"]["rate_c_per_min"], 0)
            self.assertTrue(2000 <= check["safety"]["absent_ms"] < 90000)
        for scenario in ("monitoring", "unattended", "warning", "cooling", "idle"):
            self.assertEqual(state(scenario)["safety"]["tier"], "tier1")
            self.assertEqual(state(scenario)["safety"]["effective_warning_ms"], 150000)
        # After the supply is cut the stove cools down.
        hot, later = state("shutdown", 0)["thermal"], state("shutdown", 150)["thermal"]
        self.assertGreater(hot["max_c"], later["max_c"])
        self.assertLess(later["rate_c_per_min"], 0)

    def test_network_loss_goes_stale_then_offline_then_back(self):
        kinds = []
        for tick in range(0, 61):
            messages = Command.messages("rb4107/demo", "demo", 0, tick, "b00t", "network_loss")
            for topic, data in messages:
                send(topic, data)
            kinds.append((network_phase(tick), [data["type"] for _, data in messages]))
        # The first tick also carries the boot log lines.
        self.assertEqual(kinds[0], ("online", ["controller_status", "telemetry", "thermal_frame", "controller_log"]))
        self.assertEqual(kinds[1], ("online", ["telemetry", "thermal_frame"]))
        self.assertEqual(kinds[25], ("silent", []))
        self.assertEqual(kinds[40], ("offline", ["controller_status"]))  # the Last Will, once
        self.assertEqual(kinds[41], ("offline", []))
        # Back online: the log says what happened to the link.
        self.assertEqual(kinds[60], ("online", ["controller_status", "telemetry", "thermal_frame", "controller_log"]))


class DemoLogTests(SimpleTestCase):
    """The simulated controllers' log lines (controller/log) for the Logs tab."""

    def batches(self, scenario, ticks=400):
        from ingest.demo import demo_controller_log
        return [(tick, demo_controller_log("demo", 0, tick, "b00t", scenario)) for tick in range(ticks)]

    def test_every_batch_passes_the_schema(self):
        for scenario in SCENARIOS:
            for tick, batch in self.batches(scenario):
                if batch:
                    with self.subTest(scenario=scenario, tick=tick):
                        send("rb4107/demo/controller/log", batch)

    def test_boot_lines_first_then_only_changes_and_summaries(self):
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario):
                batches = self.batches(scenario)
                first = batches[0][1]["lines"]
                self.assertIn("BOOT -> SELF_TEST (boot complete)", [line["msg"] for line in first])
                # Quiet most of the time, as the firmware: changes and the 30 s summaries, not every step.
                self.assertLess(sum(1 for _, b in batches if b) / len(batches), 0.35)
                uptimes = [line["uptime_ms"] for _, b in batches if b for line in b["lines"]]
                self.assertEqual(uptimes, sorted(uptimes), "lines in the order they were logged")

    def test_lines_say_what_the_scenario_does(self):
        def msgs(scenario):
            return [(line["level"], line["tag"], line["msg"]) for _, b in self.batches(scenario) if b
                    for line in b["lines"]]
        self.assertIn(("W", "SAFETY", "WARNING -> SHUTDOWN (shutdown timeout)"), msgs("shutdown"))
        self.assertIn(("W", "OUTPUT", "shutdown relay 1 ACTIVATED"), msgs("shutdown"))
        self.assertIn(("W", "SAFETY", "UNATTENDED -> WARNING (unattended timeout)"), msgs("warning"))
        self.assertIn(("W", "FAULT", "SAFETY fault RAISED: presence_b_node_offline (detail 2)"),
                      msgs("fault_node_offline"))
        self.assertIn(("W", "MQTT", "broker disconnected; retrying every 5000 ms"), msgs("network_loss"))
        # A transition is logged at W only into WARNING, SHUTDOWN or FAULT (rb_controller.c).
        for scenario in SCENARIOS:
            for level, tag, msg in msgs(scenario):
                if tag == "SAFETY" and " -> " in msg and "(" in msg:
                    to = msg.split(" -> ")[1].split(" ")[0]
                    self.assertEqual(level, "W" if to in {"WARNING", "SHUTDOWN", "FAULT"} else "I", msg)

    def test_the_simulator_sends_them(self):
        topics = [topic for tick in range(20)
                  for topic, _ in Command.messages("rb4107/demo", "demo", 0, tick, "b00t", "shutdown")]
        self.assertIn("rb4107/demo/controller/log", topics)
