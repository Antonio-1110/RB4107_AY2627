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

from .commands import safety_params

SCENARIOS = ("monitoring", "unattended", "warning", "shutdown", "idle", "fault_radars",
             "fault_node_offline", "network_loss", "cooling", "hot_pan", "passing")
# Without a scenario (tests, older catalogues) the index picks one, as before.
BY_INDEX = ("monitoring", "shutdown", "unattended", "warning", "monitoring", "idle", "fault_radars",
            "warning", "monitoring")
STATE = {"monitoring": "MONITORING", "unattended": "UNATTENDED", "warning": "WARNING",
         "shutdown": "SHUTDOWN", "idle": "IDLE", "fault_radars": "UNATTENDED", "fault_node_offline": "MONITORING",
         "network_loss": "MONITORING", "cooling": "UNATTENDED", "hot_pan": "UNATTENDED", "passing": "UNATTENDED"}
TICK_S = 2  # simulate_fleet's default interval
COOK_C = 28.0  # a cold stove reads about room temperature
SHUTDOWN_CYCLE_S = 600
COOLING_CYCLE_S = 420
# The firmware defaults: buzzer 2.5 min and shutdown 5 min after the person left (tier 1).
WARNING_S, SHUTDOWN_AFTER_S = 150, 300
# The temperature tiers 2..5 (firmware defaults): hotter pans get shorter times.
TIERS = {"source": "menuconfig", "rate": True,
         "levels": [{"tier": 2, "temp_c": 100.0, "warning_ms": 120000, "shutdown_ms": 240000},
                    {"tier": 3, "temp_c": 150.0, "warning_ms": 90000, "shutdown_ms": 180000},
                    {"tier": 4, "temp_c": 200.0, "warning_ms": 60000, "shutdown_ms": 120000},
                    {"tier": 5, "temp_c": 250.0, "warning_ms": 30000, "shutdown_ms": 60000}]}
HOT_TIER = TIERS["levels"][2]  # the hot_pan scenario's 200-225 °C: tier 4
HOT_WARNING_S = HOT_TIER["warning_ms"] // 1000

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
    if scenario == "cooling":  # burner off, nobody there: the pan cools by itself (tau 3 min)
        t = seconds % COOLING_CYCLE_S
        excess = (110 - COOK_C) * math.exp(-t / 180)
        return COOK_C + excess, -excess / 180 * 60
    if scenario == "shutdown":  # gas cut: cools towards room temperature
        since_cut = seconds % SHUTDOWN_CYCLE_S
        excess = (110 - COOK_C) * math.exp(-since_cut / 240)
        return COOK_C + excess, -excess / 240 * 60
    if scenario == "hot_pan":  # oil in a pan, burner on, nobody there: 200-225 °C
        return 212 + math.sin(tick / 5 + index) * 8, math.cos(tick / 5 + index) * 2
    base = 78 + (index * 7) % 18  # different dishes, simmering: 74-99 °C, tier 1 (the base times)
    phase = tick / 5 + index
    return base + math.sin(phase) * 4, math.cos(phase) * 4 / (5 * TICK_S) * 60


def _timers(scenario, tick):
    """(unattended_ms, state_ms)."""
    seconds = tick * TICK_S
    # fault_radars: presence unknown while cooking counts as absent, so the same timeline runs
    if scenario in {"unattended", "fault_radars", "passing"}:  # left the stove 10 s to just before the buzzer
        unattended = 10 + seconds % (WARNING_S - 14)
        return unattended * 1000, unattended * 1000
    if scenario == "hot_pan":  # hot oil: the buzzer comes after 60 s (tier 4), not 2.5 min
        unattended = 5 + seconds % (HOT_WARNING_S - 8)
        return unattended * 1000, unattended * 1000
    if scenario == "cooling":  # past the warning time, but held back while the pan only cools
        unattended = WARNING_S + 15 + seconds % COOLING_CYCLE_S
        return unattended * 1000, unattended * 1000
    if scenario == "warning":  # buzzer sounding, until just before the supply is cut
        unattended = WARNING_S + seconds % (SHUTDOWN_AFTER_S - WARNING_S - 2)
        return unattended * 1000, (unattended - WARNING_S) * 1000
    if scenario == "shutdown":
        since_cut = seconds % SHUTDOWN_CYCLE_S
        return (SHUTDOWN_AFTER_S + since_cut) * 1000, since_cut * 1000
    return 0, 5000 + seconds * 1000


def demo_telemetry(controller_id, index=0, tick=0, boot_id=None, scenario=None):
    """One telemetry snapshot. With boot_id it has the current firmware's shape
    (boot_id, reset_required, timer settings); without, the older shape."""
    scenario = scenario_for(index, scenario)
    state = STATE[scenario]
    occupied = scenario in {"monitoring", "network_loss", "fault_node_offline"}
    temp, rate = _pan_temperature(scenario, index, tick)
    unattended_ms, state_ms = _timers(scenario, tick)
    radar = {"link": "ONLINE", "valid": True, "detected": occupied or scenario == "passing"}
    if boot_id:  # the radar rules (placeholder): a verdict per radar and the episode behind it
        verdict = "dwelling" if occupied else "passing" if scenario == "passing" else "none"
        radar.update(verdict=verdict, episode_ms=7000 if occupied else 2500 if verdict == "passing" else 0,
                     stationary_ms=4500 if occupied else 500 if verdict == "passing" else 0,
                     span_cm=30 if occupied else 190 if verdict == "passing" else 0)
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
        "sequence": tick * 2 + 1, "protocol_version": 3, "simulation": True,
        "presence_state": presence, "nearby": "PRESENT" if scenario == "passing" else presence,
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
    phase = "off" if scenario in {"idle", "shutdown"} else "cooling" if scenario == "cooling" else "holding"
    if boot_id:
        data["boot_id"] = boot_id
        data["safety"].update(reset_required=state == "SHUTDOWN", warning_after_ms=WARNING_S * 1000,
                              shutdown_after_ms=SHUTDOWN_AFTER_S * 1000, shutdown_counts_from="UNATTENDED",
                              presence_filter={"absence_ms": 2000, "return_ms": 5000, "return_gap_ms": 1000, "fusion": "all",
                                               "pause_budget_cfg_ms": 30000, "pause_budget_pct": 25,
                                               "attendance_source": "menuconfig", "source": "menuconfig"},
                              cooking={"active": temp >= 40.0, "phase": phase, "rate_c_per_min": round(rate, 2),
                                       "settle_c": COOK_C if phase == "cooling" else round(temp, 1),
                                       "rate_err_c_per_min": 0.1,
                                       "cool_rate_c_per_min": round((temp - 37.0) / 300 * 60, 2) if temp > 37.0 else None,
                                       "room_c": 30.0, "on_c": 50.0, "off_c": 40.0, "source": "menuconfig"},
                              cookware={"profile": "light", "tau_s": 300, "source": "menuconfig"},
                              hold="cooling" if scenario == "cooling" else "none",
                              tier="tier4" if scenario == "hot_pan" else "tier1",
                              effective_warning_ms=HOT_WARNING_S * 1000 if scenario == "hot_pan" else WARNING_S * 1000,
                              effective_shutdown_ms=HOT_TIER["shutdown_ms"] if scenario == "hot_pan" else SHUTDOWN_AFTER_S * 1000,
                              tiers=TIERS,
                              radar_rules={"mode": "observe", "quiet_gap_ms": 1500, "settle_window_ms": 4000, "source": "menuconfig", "dwell_ms": 4000, "stationary_min_ms": 1500,
                                           "settle_span_cm": 60, "zone_max_cm": 0},
                              paused=scenario == "passing", pause_used_ms=4000 if scenario == "passing" else 0,
                              pause_budget_ms=30000,
                              presence_unknown=presence == "UNKNOWN", thermal_lost=False,
                              absent_ms=0, returning_ms=0,
                              params={p["key"]: p["default"] for p in safety_params()}, params_source="menuconfig")
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


# ---- controller log lines (controller/log), for the dashboard's Logs tab ----

LOG_LEVELS = {"*": "info", "SAFETY": "debug"}  # as a controller with RB_LOG_DEBUG_TAGS="SAFETY" reports them
HEALTH_EVERY = 15  # ticks: the health summaries come every 30 s (RB_S3_HEALTH_LOG_PERIOD_MS)

# Each scenario's timeline restarts when its timer wraps; these are the lines the firmware logs at that moment,
# in its own formats (rb_controller.c logs a transition at W for WARNING, SHUTDOWN and FAULT, else at I;
# docs/logging.md has the other lines).
_BACK_AND_GONE = [("I", "SAFETY", "UNATTENDED -> MONITORING (person returned)"),
                  ("I", "SAFETY", "MONITORING -> UNATTENDED (person absent)")]
_RESTART_LINES = {
    "unattended": _BACK_AND_GONE,
    "passing": _BACK_AND_GONE,
    "fault_radars": _BACK_AND_GONE,
    "hot_pan": _BACK_AND_GONE,
    "cooling": [("I", "SAFETY", "MONITORING -> UNATTENDED (person absent)")],
    "warning": [("W", "SAFETY", "UNATTENDED -> WARNING (unattended timeout)")],
    "shutdown": [("W", "SAFETY", "WARNING -> SHUTDOWN (shutdown timeout)"),
                 ("W", "OUTPUT", "shutdown relay 1 ACTIVATED")],
}


def _restarted(scenario, tick):
    """True on the tick a scenario's timeline starts over (and on tick 0)."""
    if tick == 0:
        return True
    before, now = _timers(scenario, tick - 1), _timers(scenario, tick)
    return now[1] < before[1]


def _boot_lines(scenario, index):
    """What a controller logs from power-on until it is safe-to-run, at uptimes before the first telemetry."""
    ip = f"192.168.1.{40 + index}"
    lines = [
        (310, "I", "CONTROLLER", "RB4107 controller, ESP32-S3 rev 0.2, 8 MB flash, protocol v3"),
        (420, "I", "BOARD", "TCA9554 at 0x20 ready, outputs released"),
        (900, "I", "RTC", "wall clock restored from PCF85063"),
        (1450, "I", "WIFI", f"connected, IP {ip}, channel 6, RSSI -58 dBm"),
        (1520, "I", "ESPNOW", "ESP-NOW started on channel 6"),
        (2210, "I", "MQTT", "broker connected (mqtt://rb4107-broker.local:1883)"),
        (2230, "I", "MQTT", "subscribed to rb4107/controller/command"),
        (2600, "I", "RTC", "SNTP synchronised"),
        (3100, "I", "SAFETY", "BOOT -> SELF_TEST (boot complete)"),
        (3420, "I", "SENSOR", "presence_a node_01 ONLINE"),
        (3480, "I", "SENSOR", "presence_b node_02 ONLINE"),
        (3510, "I", "SENSOR", "thermal node_03 ONLINE"),
        (4200, "I", "SAFETY", "SELF_TEST -> IDLE (self-test passed)"),
    ]
    if scenario == "fault_node_offline":
        lines += [(64200, "W", "SENSOR", "presence_b node_02 STALE"),
                  (72200, "W", "SENSOR", "presence_b node_02 OFFLINE"),
                  (72210, "W", "FAULT", "SAFETY fault RAISED: presence_b_node_offline (detail 2)")]
    elif scenario == "fault_radars":
        lines += [(58000, "E", "SENSOR", "presence_a node_01 sensor reading lost (invalid frame)"),
                  (58020, "E", "SENSOR", "presence_b node_02 sensor reading lost (invalid frame)"),
                  (58030, "W", "FAULT", "SAFETY fault RAISED: presence_a_unavailable (detail 1)"),
                  (58040, "W", "FAULT", "SAFETY fault RAISED: presence_b_unavailable (detail 1)")]
    if scenario != "idle":
        lines.append((90000, "I", "SAFETY", "IDLE -> MONITORING (heating detected)"))
    if scenario in {"warning", "shutdown"}:  # the steps before the one their timeline starts with
        lines.append((100000, "I", "SAFETY", "MONITORING -> UNATTENDED (person absent)"))
    if scenario == "shutdown":
        lines.append((110000, "W", "SAFETY", "UNATTENDED -> WARNING (unattended timeout)"))
    return [{"uptime_ms": ms, "level": level, "tag": tag, "msg": msg} for ms, level, tag, msg in lines]


def demo_controller_log(controller_id, index=0, tick=0, boot_id=None, scenario=None):
    """The controller/log batch one simulated controller sends this tick, or None when it has nothing to say:
    boot lines on the first tick, the lines of each timeline restart, network drops, and health summaries."""
    scenario = scenario_for(index, scenario)
    if scenario == "network_loss" and network_phase(tick) != "online":
        return None  # no broker to send to; the lines about the drop go out on reconnect
    uptime = 120000 + tick * TICK_S * 1000
    lines = _boot_lines(scenario, index) if tick == 0 else []

    def log(level, tag, msg, at=uptime):
        lines.append({"uptime_ms": at, "level": level, "tag": tag, "msg": msg[:119]})

    if scenario in _RESTART_LINES and _restarted(scenario, tick):
        for n, (level, tag, msg) in enumerate(_RESTART_LINES[scenario]):
            log(level, tag, msg, uptime - 900 + n * 300)
    if scenario == "network_loss" and tick and network_phase(tick) == "online" and network_phase(tick - 1) != "online":
        gap_s = NETWORK_SILENT_S + NETWORK_OFFLINE_S
        log("W", "WIFI", "disconnected (reason 8, beacon timeout); reconnecting", uptime - gap_s * 1000)
        log("W", "MQTT", "broker disconnected; retrying every 5000 ms", uptime - gap_s * 1000 + 50)
        log("I", "WIFI", f"connected, IP 192.168.1.{40 + index}, channel 6, RSSI -71 dBm", uptime - 1200)
        log("I", "MQTT", "broker connected (mqtt://rb4107-broker.local:1883)", uptime - 400)
    temp, rate = _pan_temperature(scenario, index, tick)
    if tick % HEALTH_EVERY == HEALTH_EVERY - 1:
        heap = 186000 - index % 5 * 1200
        log("I", "CONTROLLER", f"uptime {uptime // 1000} s, free heap {heap} B, min free {heap - 21000} B")
        log("D", "SAFETY", f"{STATE[scenario]}: hottest {temp:.1f} C, {rate:+.1f} C/min")
        if scenario == "fault_node_offline":
            log("W", "SENSOR", "presence_b node_02 still OFFLINE (no packet for 5+ min)")
    if not lines:
        return None
    data = {"schema_version": 2, "type": "controller_log", "controller_id": controller_id,
            "timestamp": timezone.now().isoformat(), "uptime_ms": uptime, "sequence": tick * 2 + 2,
            "dropped": 0, "levels": LOG_LEVELS, "lines": sorted(lines, key=lambda line: line["uptime_ms"])[:50]}
    if boot_id:
        data["boot_id"] = boot_id
    return data
