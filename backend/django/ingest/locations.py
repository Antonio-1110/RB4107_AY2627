"""Config-driven device-to-location mapping and display-only aggregation."""
import json
import math
from collections import defaultdict
from django.conf import settings

SEVERITY_RANK = {"critical": 5, "warning": 4, "fault": 3, "normal": 2, "unknown": 1}
CONNECTION_RANK = {"monitor_unavailable": 5, "offline": 4, "stale": 3, "unknown": 2, "online": 1}


def load_location_catalog():
    try:
        with open(settings.LOCATION_CATALOG_FILE, encoding="utf-8") as stream:
            catalog = json.load(stream)
    except (OSError, ValueError) as error:
        return {"meta": {"status": "unavailable", "notice": str(error)}, "devices": {}}
    if not isinstance(catalog, dict):
        return {"meta": {"status": "invalid", "notice": "catalogue root must be an object"},
                "devices": {}}
    meta = catalog.get("meta", {})
    if not isinstance(meta, dict):
        return {"meta": {"status": "invalid", "notice": "meta must be an object"}, "devices": {}}
    devices = catalog.get("devices", {})
    if not isinstance(devices, dict):
        return {"meta": {"status": "invalid", "notice": "devices must be an object"}, "devices": {}}
    return {"meta": meta, "devices": devices}


def unknown_location(device_id):
    return {"stall_id": f"unassigned-{device_id}", "site_id": "unassigned",
            "site_name": "Unassigned site", "site_type": "Unknown",
            "building": "Location incomplete", "access_zone": "Unknown zone",
            "level": "Unknown level", "area": "Location incomplete", "venue": "Unassigned device",
            "stall_name": "Unassigned device", "unit": "Not configured",
            "station_name": device_id, "response_route": "Location metadata is incomplete",
            "demo_only": False, "location_source": "No catalogue entry", "incomplete": True}


def location_for(device_id, catalog=None):
    entry = (catalog or load_location_catalog())["devices"].get(device_id)
    if not isinstance(entry, dict):
        return unknown_location(device_id)
    required = ("stall_id", "site_name", "building", "access_zone", "level", "stall_name", "station_name")
    result = unknown_location(device_id)
    result.update({key: value for key, value in entry.items() if isinstance(value, (str, bool, int, float))})
    result["incomplete"] = any(not result.get(key) for key in required)
    return result


def classify_display_state(device):
    """Classify reported states for the UI; never performs cooking safety logic."""
    values = device["values"]
    safety = str(values.get("safety_state") or "").strip().lower()
    alarm = str(values.get("alarm_state") or "").strip().lower()
    relay = str(values.get("relay_state") or "").strip().lower()
    critical_states = {"shutdown", "shutoff", "shutoff_latched", "isolated", "emergency", "fire"}
    warning_states = {"warning", "unattended_warning", "alarm", "active"}
    fault_states = {"fault", "error", "sensor_fault", "relay_fault"}
    if safety in critical_states or relay in {"isolated", "shutoff", "off_latched"}:
        level, title = "critical", "SUPPLY ISOLATED"
        source = "relay_state" if relay in {"isolated", "shutoff", "off_latched"} else "safety_state"
    elif safety in warning_states or alarm in warning_states:
        level, title = "warning", "UNATTENDED COOKING WARNING"
        source = "alarm_state" if alarm in warning_states else "safety_state"
    elif safety in fault_states or alarm in fault_states or relay in fault_states:
        level, title = "fault", "SAFETY SYSTEM FAULT"
        source = "safety_state" if safety in fault_states else "alarm_state" if alarm in fault_states else "relay_state"
    elif safety == "unattended" and alarm in {"", "clear", "inactive", "normal"}:
        level, title, source = "warning", "UNATTENDED — TIMER RUNNING", "safety_state"
    elif safety in {"monitoring", "normal", "idle", "attended", "safe"} and alarm in {"", "clear", "inactive", "normal"}:
        level, title, source = "normal", "NORMAL", "safety_state"
    else:
        level, title, source = "unknown", "UNMAPPED / UNKNOWN STATE", "safety_state"
    source_age = device["field_age_seconds"].get(source)
    last_known = device["connection"] != "online" or source_age is None or source_age > device["stale_after_seconds"]
    if level == "normal" and last_known:
        level, title = "unknown", "LAST-KNOWN NORMAL — NOT CURRENT"
    return {"level": level, "title": title, "active": level in {"critical", "warning", "fault"},
            "source_field": source, "freshness_seconds": source_age, "last_known": last_known}


def _finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def _aggregate_value(devices, field, mode="values"):
    found = [device["values"].get(field) for device in devices if device["values"].get(field) is not None]
    if not found:
        return None
    if mode == "max":
        numbers = [value for value in found if _finite(value)]
        return max(numbers) if numbers else None
    normalized = {str(value) for value in found}
    return found[0] if len(normalized) == 1 else "mixed"


def aggregate_stalls(devices):
    groups = defaultdict(list)
    for device in devices:
        groups[device["location"]["stall_id"]].append(device)
    stalls = []
    for stall_id, members in groups.items():
        ordered = sorted(members, key=lambda item: (-SEVERITY_RANK[item["display_state"]["level"]], item["device_id"]))
        lead = ordered[0]
        connections = [item["connection"] for item in members]
        connection = max(connections, key=lambda value: CONNECTION_RANK.get(value, 2))
        temperature = _aggregate_value(members, "temperature_c", "max")
        temp_fresh = any(item["connection"] == "online" and
                         item["field_age_seconds"].get("temperature_c") is not None and
                         item["field_age_seconds"]["temperature_c"] <= item["stale_after_seconds"] and
                         item["values"].get("temperature_c") == temperature for item in members)
        latest_seen = max((item["last_seen_at"] for item in members if item["last_seen_at"]), default=None)
        alerts = [{"device_id": item["device_id"], "station_name": item["location"]["station_name"],
                   "state": item["display_state"], "values": item["values"],
                   "connection": item["connection"], "last_seen_at": item["last_seen_at"]}
                  for item in ordered if item["display_state"]["active"]]
        stalls.append({"stall_id": stall_id, "stall_name": lead["location"]["stall_name"],
                       "location": lead["location"], "severity": lead["display_state"]["level"],
                       "state_title": lead["display_state"]["title"],
                       "state_last_known": lead["display_state"]["last_known"], "connection": connection,
                       "temperature_c": temperature, "temperature_fresh": temp_fresh,
                       "occupied": _aggregate_value(members, "occupied"),
                       "unattended_seconds": _aggregate_value(members, "unattended_seconds", "max"),
                       "relay_state": _aggregate_value(members, "relay_state"),
                       "last_seen_at": latest_seen, "station_count": len(members),
                       "device_ids": [item["device_id"] for item in ordered], "alerts": alerts,
                       "simulation": all(item["values"].get("simulation") is True for item in members)})
    return sorted(stalls, key=lambda item: (-SEVERITY_RANK[item["severity"]],
                                            item["location"]["site_name"], item["location"]["building"],
                                            item["location"]["level"], item["stall_name"]))


def fleet_summary(stalls):
    summary = {"total": len(stalls), "critical": 0, "warning": 0, "fault": 0,
               "normal": 0, "unknown": 0, "connectivity_issues": 0}
    for stall in stalls:
        summary[stall["severity"]] = summary.get(stall["severity"], 0) + 1
        if stall["connection"] != "online":
            summary["connectivity_issues"] += 1
    return summary


def active_alerts(stalls):
    result = []
    for stall in stalls:
        for alert in stall["alerts"]:
            result.append({**alert, "stall_id": stall["stall_id"], "stall_name": stall["stall_name"],
                           "location": stall["location"], "severity": alert["state"]["level"],
                           "title": alert["state"]["title"]})
    return sorted(result, key=lambda item: (-SEVERITY_RANK[item["severity"]], item["stall_name"], item["device_id"]))
