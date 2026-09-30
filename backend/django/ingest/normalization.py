"""Translate validated firmware schema v2 into the dashboard's stable fields.

This module does not evaluate temperatures, fuse radar readings or run timers.
The controller publishes those decisions. See docs/dashboard_integration.md.
"""
import math


def finite(value):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) else None


def thermal_fields(thermal):
    valid = thermal.get("valid") is True
    mapping = {"temperature_c": "max_c", "temperature_avg_c": "mean_c",
               "temperature_min_c": "min_c", "hot_region_c": "hot_region_c",
               "rate_c_per_min": "rate_c_per_min"}
    return {key: finite(thermal.get(source)) if valid else None for key, source in mapping.items()}


def normalize(message):
    data, kind = message.data, message.type
    fields = {}
    if "uptime_ms" in data:
        fields["uptime_seconds"] = data["uptime_ms"] / 1000
    if kind == "telemetry":
        safety = data["safety"]
        shutdown = safety["shutdown"]
        # In this repository SHUTDOWN is explicitly latched until operator reset.
        # No explicit reset flag is sent in v2, so expose that documented meaning.
        fields.update(
            occupied={"PRESENT": True, "ABSENT": False, "UNKNOWN": None}[data["presence_state"]],
            safety_state=safety["state"].lower(),
            relay_state="isolated" if shutdown else "enabled",
            manual_reset_required=shutdown and safety["state"] == "SHUTDOWN",
            reset_status_source="schema_v2_latched_shutdown",
            alarm_state={"OFF": "clear", "WARNING": "warning", "SHUTDOWN": "shutdown", "FAULT": "fault"}[safety["buzzer"]],
            unattended_seconds=safety["unattended_ms"] / 1000,
            test_timers=safety["test_timers"],
            faults=data["faults"],
            simulation=data.get("simulation") is True,
            # Not present in firmware v2: do not invent cooking flags/settings.
            cooking_state=None, warning_after_seconds=None, shutoff_after_seconds=None,
            protocol_version=data["protocol_version"],
        )
        fields.update(thermal_fields(data["thermal"]))
        fields["sensors"] = {}
        for node in data["nodes"]:
            valid = node["valid"] and node["link"] == "ONLINE"
            sensor = {"type": "C4002" if node["role"] == "presence" else "MLX90640",
                      "role": node["role"], "link": node["link"], "valid": valid}
            if node["role"] == "presence":
                sensor["occupied"] = node["detected"] if valid else None
                if not valid:
                    sensor.update(moving=None, stationary=None, distance_m=None)
            else:
                sensor.update(thermal_fields(data["thermal"] if valid else {}))
            fields["sensors"][node["sensor_node"]] = sensor
    elif kind == "heartbeat":
        fields.update(heartbeat_safety_state=data["safety_state"].lower(),
                      safety_loop_count=data["safety_loop_count"])
    elif kind == "presence":
        presence = data["presence"]
        fields["sensors"] = {data["sensor_node"]: {
            "type": "C4002", "role": "presence", "valid": presence["valid"],
            "occupied": presence["detected"] if presence["valid"] else None,
            "moving": presence["moving"] if presence["valid"] else None,
            "stationary": presence["stationary"] if presence["valid"] else None,
            "distance_m": finite(presence["distance_m"]) if presence["valid"] else None,
        }}
    elif kind == "thermal":
        fields["sensors"] = {data["sensor_node"]: {
            "type": "MLX90640", "role": "thermal", "valid": data["thermal"]["valid"],
            **thermal_fields(data["thermal"]),
        }}
    elif kind == "node_status":
        valid = data["valid"] and data["link"] == "ONLINE"
        sensor = {key: data[key] for key in ("role", "link", "node_fault_flags", "missed_packets", "restarts")}
        sensor["valid"] = valid
        sensor["type"] = "C4002" if data["role"] == "presence" else "MLX90640"
        if not valid:
            sensor.update({"occupied": None, "moving": None, "stationary": None, "distance_m": None}
                          if data["role"] == "presence" else thermal_fields({}))
        fields["sensors"] = {data["sensor_node"]: sensor}
    elif kind == "faults":
        fields["faults"] = [fault["name"] for fault in data["faults"]]
        fields["fault_details"] = data["faults"]
    return fields
