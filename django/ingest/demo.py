"""Synthetic schema-v2 messages for exercising the real validation/storage path."""
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
