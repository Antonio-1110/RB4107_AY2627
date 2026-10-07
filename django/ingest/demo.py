"""Synthetic schema-v2 messages for exercising the real validation/storage path.

Each simulated controller plays one scenario (its "demo_scenario" in
locations.demo.json): situations a single bench stall can't show at once.
Values follow the firmware's rules (docs/safety_state_machine.md): any
safety-relevant fault, such as one radar node going offline, means FAULT.
Timers count up and start over, so a stall stays in its state.
"""
import base64
import math

from django.utils import timezone

SCENARIOS = ("monitoring", "unattended", "warning", "shutdown", "idle", "fault_radars",
             "fault_node_offline", "network_loss")
# Without a scenario (tests, older catalogues) the index picks one, as before.
BY_INDEX = ("monitoring", "shutdown", "unattended", "warning", "monitoring", "idle", "fault_radars",
            "warning", "monitoring")
STATE = {"monitoring": "MONITORING", "unattended": "UNATTENDED", "warning": "WARNING",
         "shutdown": "SHUTDOWN", "idle": "IDLE", "fault_radars": "FAULT", "fault_node_offline": "FAULT",
         "network_loss": "MONITORING"}
TICK_S = 2  # simulate_fleet's default interval
COOK_C = 28.0  # a cold stove reads about room temperature
SHUTDOWN_CYCLE_S = 600

# network_loss: online, then silent (the dashboard marks it stale), then the
# broker reports it offline (the controller's MQTT Last Will), then back.
NETWORK_ONLINE_S, NETWORK_SILENT_S, NETWORK_OFFLINE_S = 40, 40, 40
NETWORK_CYCLE_S = NETWORK_ONLINE_S + NETWORK_SILENT_S + NETWORK_OFFLINE_S


def scenario_for(index, scenario=None):
    return scenario if scenario in SCENARIOS else BY_INDEX[index % len(BY_INDEX)]


def network_phase(tick):
    """'online', 'silent' or 'offline' for the network_loss scenario."""
    t = (tick * TICK_S) % NETWORK_CYCLE_S
    if t < NETWORK_ONLINE_S:
        return "online"
    return "silent" if t < NETWORK_ONLINE_S + NETWORK_SILENT_S else "offline"


def _pan_temperature(scenario, index, tick):
    """(hottest reading °C, rate °C/min) of the stove."""
    seconds = tick * TICK_S
    if scenario == "idle":
        return COOK_C + math.sin(tick / 9 + index) * 0.5, 0.0
    if scenario == "shutdown":  # gas cut: cools towards room temperature
        since_cut = seconds % SHUTDOWN_CYCLE_S
        excess = (110 - COOK_C) * math.exp(-since_cut / 240)
        return COOK_C + excess, -excess / 240 * 60
    base = 95 + (index * 7) % 30  # different dishes, 95-125 °C
    phase = tick / 5 + index
    return base + math.sin(phase) * 4, math.cos(phase) * 4 / (5 * TICK_S) * 60


def _timers(scenario, tick):
    """(unattended_ms, state_ms)."""
    seconds = tick * TICK_S
    if scenario == "unattended":  # left the stove 10-55 s ago (warning at 60 s)
        unattended = 10 + seconds % 46
        return unattended * 1000, unattended * 1000
    if scenario == "warning":  # 60-88 s (supply cut at 90 s)
        unattended = 60 + seconds % 29
        return unattended * 1000, (unattended - 60) * 1000
    if scenario == "shutdown":
        since_cut = seconds % SHUTDOWN_CYCLE_S
        return (90 + since_cut) * 1000, since_cut * 1000
    return 0, 5000 + seconds * 1000


def demo_telemetry(controller_id, index=0, tick=0, boot_id=None, scenario=None):
    """One telemetry snapshot. With boot_id it has the current firmware's shape
    (boot_id, reset_required, timer settings); without, the older shape."""
    scenario = scenario_for(index, scenario)
    state = STATE[scenario]
    occupied = scenario in {"monitoring", "network_loss", "fault_node_offline"}
    temp, rate = _pan_temperature(scenario, index, tick)
    unattended_ms, state_ms = _timers(scenario, tick)
    radar = {"link": "ONLINE", "valid": True, "detected": occupied}
    radar_b = dict(radar)
    faults = []
    presence = "PRESENT" if occupied else "ABSENT"
    if scenario == "fault_radars":  # both radars report garbage: nobody can say who is there
        radar = radar_b = {"link": "ONLINE", "valid": False, "detected": None}
        faults = ["presence_a_unavailable", "presence_b_unavailable"]
        presence = "UNKNOWN"
    elif scenario == "fault_node_offline":  # radar B's node unplugged; A still sees the cook
        radar_b = {"link": "OFFLINE", "valid": False, "detected": None}
        faults = ["presence_b_node_offline"]
    data = {
        "schema_version": 2, "type": "telemetry", "controller_id": controller_id,
        "timestamp": timezone.now().isoformat(), "uptime_ms": 120000 + tick * TICK_S * 1000,
        "sequence": tick * 2 + 1, "protocol_version": 2, "simulation": True,
        "presence_state": presence,
        "nodes": [
            {"sensor_node": "node_01", "role": "presence", **radar},
            {"sensor_node": "node_02", "role": "presence", **radar_b},
            {"sensor_node": "node_03", "role": "thermal", "link": "ONLINE", "valid": True, "detected": None},
        ],
        "thermal": {"valid": True, "max_c": round(temp, 1), "min_c": 24.0,
                    "mean_c": round(24 + (temp - 24) * 0.25, 1), "hot_region_c": round(temp - 5, 1),
                    "rate_c_per_min": round(rate, 1),
                    "pixels_above_threshold": max(0, int((temp - 50) * 0.6))},  # above 50 °C
        "safety": {"state": state, "state_ms": state_ms, "unattended_ms": unattended_ms,
                   "buzzer": state if state in {"WARNING", "SHUTDOWN", "FAULT"} else "OFF",
                   "shutdown": state == "SHUTDOWN", "test_timers": False},
        "faults": faults,
    }
    if boot_id:
        data["boot_id"] = boot_id
        data["safety"].update(reset_required=state == "SHUTDOWN", warning_after_ms=60000,
                              shutdown_after_ms=90000, shutdown_counts_from="UNATTENDED")
    return data


def demo_controller_status(controller_id, online, boot_id=None):
    """What the broker publishes for the controller: retained online, or the Last Will."""
    data = {"schema_version": 2, "type": "controller_status", "controller_id": controller_id,
            "online": online}
    if boot_id:
        data["boot_id"] = boot_id
    return data


def demo_thermal_frame(controller_id, index=0, tick=0, boot_id=None, scenario=None):
    """One heat-map picture: a warm kitchen with a pan as hot as the telemetry says, encoded
    the way the firmware does it (thermal_frame_encode: 1 byte per pixel, range fitted to the frame)."""
    width, height = 32, 24
    pan, _ = _pan_temperature(scenario_for(index, scenario), index, tick)
    cx, cy = 12 + 3 * math.sin(tick / 7 + index), 13 + 2 * math.cos(tick / 9 + index)
    temps = [24 + 0.08 * y + (pan - 24) * math.exp(-((x - cx) ** 2 + (y - cy) ** 2) / 10)
             for y in range(height) for x in range(width)]
    base = math.floor(min(temps) * 100)
    step = max(10, math.ceil((math.ceil(max(temps) * 100) - base) / 254))
    pixels = bytes(min(254, max(0, round((t * 100 - base) / step))) for t in temps)
    data = {
        "schema_version": 2, "type": "thermal_frame", "controller_id": controller_id,
        "timestamp": timezone.now().isoformat(), "uptime_ms": 120000 + tick * TICK_S * 1000,
        "sequence": tick * 2 + 2, "simulation": True, "sensor_node": "node_03",
        "frame": {"number": tick + 1, "width": width, "height": height, "base_c": base / 100,
                  "step_c": step / 100, "invalid": 255, "hot_threshold_c": 50.0,
                  "hot_region_radius": 1, "encoding": "u8_base64",
                  "pixels": base64.b64encode(pixels).decode()},
    }
    if boot_id:
        data["boot_id"] = boot_id
    return data
