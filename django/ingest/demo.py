"""Synthetic schema-v2 messages for exercising the real validation/storage path."""
import base64
import math

from django.utils import timezone


def demo_telemetry(controller_id, index=0, tick=0, boot_id=None):
    """One telemetry snapshot. With boot_id it has the current firmware's shape
    (boot_id, reset_required, timer settings); without, the older shape."""
    states = ["MONITORING", "SHUTDOWN", "UNATTENDED", "WARNING", "MONITORING",
              "IDLE", "FAULT", "WARNING", "MONITORING"]
    state = states[index % len(states)]
    occupied = state in {"MONITORING", "IDLE"}
    temp = round((32 if state == "IDLE" else 105 + index * 2) + math.sin(tick / 5 + index) * 3, 1)
    data = {
        "schema_version": 2, "type": "telemetry", "controller_id": controller_id,
        "timestamp": timezone.now().isoformat(), "uptime_ms": 120000 + tick * 2000,
        "sequence": tick * 2 + 1, "protocol_version": 2, "simulation": True,
        "presence_state": "UNKNOWN" if state == "FAULT" else "PRESENT" if occupied else "ABSENT",
        "nodes": [
            {"sensor_node": "node_01", "role": "presence", "link": "ONLINE",
             "valid": state != "FAULT", "detected": None if state == "FAULT" else occupied},
            {"sensor_node": "node_02", "role": "presence", "link": "ONLINE",
             "valid": state != "FAULT", "detected": None if state == "FAULT" else occupied},
            {"sensor_node": "node_03", "role": "thermal", "link": "ONLINE", "valid": True, "detected": None},
        ],
        "thermal": {"valid": True, "max_c": temp, "min_c": 24.0, "mean_c": 46.0,
                    "hot_region_c": temp - 5, "rate_c_per_min": 1.2, "pixels_above_threshold": 30},
        "safety": {"state": state, "state_ms": 5000,
                   "unattended_ms": {"UNATTENDED": 25000, "WARNING": 70000, "SHUTDOWN": 95000}.get(state, 0),
                   "buzzer": state if state in {"WARNING", "SHUTDOWN", "FAULT"} else "OFF",
                   "shutdown": state == "SHUTDOWN", "test_timers": False},
        "faults": ["presence_unavailable"] if state == "FAULT" else [],
    }
    if boot_id:
        data["boot_id"] = boot_id
        data["safety"].update(reset_required=state == "SHUTDOWN", warning_after_ms=60000,
                              shutdown_after_ms=90000, shutdown_counts_from="UNATTENDED")
    return data


def demo_thermal_frame(controller_id, index=0, tick=0, boot_id=None):
    """One heat-map picture: a warm kitchen with a hot pan, encoded the way the
    firmware does it (thermal_frame_encode: 1 byte per pixel, range fitted to the frame)."""
    width, height = 32, 24
    pan = (32 if index % 9 == 5 else 105 + index * 2) + math.sin(tick / 5 + index) * 3
    cx, cy = 12 + 3 * math.sin(tick / 7 + index), 13 + 2 * math.cos(tick / 9 + index)
    temps = [24 + 0.08 * y + (pan - 24) * math.exp(-((x - cx) ** 2 + (y - cy) ** 2) / 10)
             for y in range(height) for x in range(width)]
    base = math.floor(min(temps) * 100)
    step = max(10, math.ceil((math.ceil(max(temps) * 100) - base) / 254))
    pixels = bytes(min(254, max(0, round((t * 100 - base) / step))) for t in temps)
    data = {
        "schema_version": 2, "type": "thermal_frame", "controller_id": controller_id,
        "timestamp": timezone.now().isoformat(), "uptime_ms": 120000 + tick * 2000,
        "sequence": tick * 2 + 2, "simulation": True, "sensor_node": "node_03",
        "frame": {"number": tick + 1, "width": width, "height": height, "base_c": base / 100,
                  "step_c": step / 100, "invalid": 255, "encoding": "u8_base64",
                  "pixels": base64.b64encode(pixels).decode()},
    }
    if boot_id:
        data["boot_id"] = boot_id
    return data
