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
        # A radar node offline is a safety fault: FAULT, even though the other radar sees the cook.
        offline = state("fault_node_offline")
        self.assertEqual(offline["safety"]["state"], "FAULT")
        self.assertEqual(offline["faults"], ["presence_b_node_offline"])
        self.assertEqual(offline["presence_state"], "PRESENT")
        self.assertEqual(state("fault_radars")["presence_state"], "UNKNOWN")
        self.assertEqual(state("idle")["presence_state"], "ABSENT")
        # The timers stay inside their state's window while counting.
        for tick in range(0, 120, 7):
            self.assertLess(state("unattended", tick)["safety"]["unattended_ms"], 60000)
            self.assertTrue(60000 <= state("warning", tick)["safety"]["unattended_ms"] < 90000)
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
        self.assertEqual(kinds[0], ("online", ["controller_status", "telemetry", "thermal_frame"]))
        self.assertEqual(kinds[1], ("online", ["telemetry", "thermal_frame"]))
        self.assertEqual(kinds[25], ("silent", []))
        self.assertEqual(kinds[40], ("offline", ["controller_status"]))  # the Last Will, once
        self.assertEqual(kinds[41], ("offline", []))
        self.assertEqual(kinds[60], ("online", ["controller_status", "telemetry", "thermal_frame"]))
