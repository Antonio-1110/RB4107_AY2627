"""Translate validated firmware schema v2 into the dashboard's stable fields.

This module does not evaluate temperatures, fuse radar readings or run timers.
The controller publishes those decisions. See docs/dashboard.md.
"""
import math


def finite(value):
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) else None


def _seconds(ms):
    return ms / 1000 if isinstance(ms, int) and not isinstance(ms, bool) else None


def _presence_filter(value):
    """Firmware since 2026-10-10 publishes the presence filter; older firmware doesn't."""
    if not isinstance(value, dict):
        return None
    return {"absence_seconds": _seconds(value["absence_ms"]), "return_seconds": _seconds(value["return_ms"]),
            "return_gap_seconds": _seconds(value["return_gap_ms"]), "source": value["source"]}


def thermal_fields(thermal):
    valid = thermal.get("valid") is True
    mapping = {"temperature_c": "max_c", "temperature_avg_c": "mean_c",
               "temperature_min_c": "min_c", "hot_region_c": "hot_region_c",
               "rate_c_per_min": "rate_c_per_min", "pixels_above_threshold": "pixels_above_threshold"}
    return {key: finite(thermal.get(source)) if valid else None for key, source in mapping.items()}


def normalize(message):
    data, kind = message.data, message.type
    fields = {}
    if "uptime_ms" in data:
        fields["uptime_seconds"] = data["uptime_ms"] / 1000
    if "boot_id" in data:
        fields["boot_id"] = data["boot_id"]
    if kind == "telemetry":
        safety = data["safety"]
        shutdown = safety["shutdown"]
        # Firmware since 2026-09-30 publishes reset_required. Older firmware
        # does not, so fall back to its documented meaning: SHUTDOWN is latched
        # until an operator reset.
        if "reset_required" in safety:
            reset_required, reset_source = safety["reset_required"], "firmware"
        else:
            reset_required, reset_source = shutdown and safety["state"] == "SHUTDOWN", "derived_from_shutdown_state"
        fields.update(
            occupied={"PRESENT": True, "ABSENT": False, "UNKNOWN": None}[data["presence_state"]],
            safety_state=safety["state"].lower(),
            relay_state="isolated" if shutdown else "enabled",
            manual_reset_required=reset_required,
            reset_status_source=reset_source,
            warning_after_seconds=_seconds(safety.get("warning_after_ms")),
            shutdown_after_seconds=_seconds(safety.get("shutdown_after_ms")),
            shutdown_counts_from=safety.get("shutdown_counts_from"),
            alarm_state={"OFF": "clear", "WARNING": "warning", "SHUTDOWN": "shutdown", "FAULT": "fault"}[safety["buzzer"]],
            unattended_seconds=safety["unattended_ms"] / 1000,
            test_timers=safety["test_timers"],
            presence_filter=_presence_filter(safety.get("presence_filter")),
            faults=data["faults"],
            simulation=data.get("simulation") is True,
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
    elif kind == "c4002_config":
        tuning = {key: data[key] for key in ("request_id", "action", "result", "error",
                                              "calibration_remaining_s", "saved")}
        tuning["reported_at"] = data.get("timestamp")
        # A failed command carries no settings: keep showing the last known ones.
        if data["settings"] is not None:
            tuning["settings"] = data["settings"]
        fields["sensors"] = {data["sensor_node"]: {"c4002": tuning}}
    elif kind == "c4002_live":
        # Only the newest result; the live view reads the history from /c4002/live/.
        fields["sensors"] = {data["sensor_node"]: {"c4002_live": {
            key: data[key] for key in ("target", "gate_size_cm", "presence_gates", "presence", "motion",
                                       "calibration_remaining_s", "results")}}}
    elif kind == "faults":
        fields["faults"] = [fault["name"] for fault in data["faults"]]
        fields["fault_details"] = data["faults"]
    return fields
