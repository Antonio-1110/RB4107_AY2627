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
    """Classify reported states for the UI; never performs cooking safety logic.

    The values come from normalization.py, i.e. the firmware's schema-v2 enums
    in lower case: safety_state is boot, self_test, idle, monitoring,
    unattended, warning, shutdown or fault; alarm_state is clear, warning,
    shutdown or fault; relay_state is enabled or isolated.
    """
    values = device["values"]
    safety = values.get("safety_state")
    alarm = values.get("alarm_state")
    relay = values.get("relay_state")
    if relay == "isolated" or safety == "shutdown":
        level, title = "critical", "SUPPLY ISOLATED"
        source = "relay_state" if relay == "isolated" else "safety_state"
    elif safety == "warning" or alarm == "warning":
        level, title = "warning", "UNATTENDED COOKING WARNING"
        source = "safety_state" if safety == "warning" else "alarm_state"
    elif safety == "fault" or alarm == "fault":
        level, title = "fault", "SAFETY SYSTEM FAULT"
        source = "safety_state" if safety == "fault" else "alarm_state"
    elif safety == "unattended":
        level, title, source = "warning", "UNATTENDED — TIMER RUNNING", "safety_state"
    elif safety in {"idle", "monitoring"} and alarm in {None, "clear"}:
        level, title, source = "normal", "NORMAL", "safety_state"
    elif safety in {"boot", "self_test"}:
        level, title, source = "unknown", "CONTROLLER STARTING", "safety_state"
    else:
        level, title, source = "unknown", "UNKNOWN STATE", "safety_state"
    source_age = device["field_age_seconds"].get(source)
    last_known = device["connection"] != "online" or source_age is None or source_age > device["stale_after_seconds"]
    if level == "normal" and last_known:
        level, title = "unknown", "LAST-KNOWN NORMAL — NOT CURRENT"
    return {"level": level, "title": title, "active": level in {"critical", "warning", "fault"},
            "source_field": source, "freshness_seconds": source_age, "last_known": last_known}


# Active cooking. The firmware decides it: cooking starts when the thermal
# camera's hot region reaches RB_SAFETY_HEAT_ON_DC (placeholder 50 degC, or a
# fast rise) and ends below RB_SAFETY_HEAT_OFF_DC (40 degC); issue #21 tunes
# them. Its safety state says the result: IDLE is not cooking, MONITORING /
# UNATTENDED / WARNING are cooking. Only when the state can't say (FAULT,
# starting up) is it guessed from the hot region with the same default
# thresholds, and labelled "likely".
HEAT_ON_C, HEAT_OFF_C = 50.0, 40.0
COOKING_STATES = {"monitoring": "Cook at the stove", "unattended": "Nobody at the stove",
                  "warning": "Nobody at the stove · warning"}
COOKING_RANK = {"cooking": 5, "likely_cooking": 4, "supply_cut": 3, "unknown": 2,
                "likely_not_cooking": 1, "not_cooking": 0}
ACTIVE_COOKING = {"cooking", "likely_cooking"}


def classify_cooking(device):
    """Whether a controller's stove is in use, for display; never used for safety."""
    values = device["values"]
    safety = values.get("safety_state")
    hot_field = "hot_region_c" if _finite(values.get("hot_region_c")) else "temperature_c"
    hot = values.get(hot_field) if _finite(values.get(hot_field)) else None
    hot_text = f"hot region {hot:.0f} °C" if hot is not None else "no temperature"
    if safety in COOKING_STATES:
        status, label, detail, source = "cooking", "COOKING", f"{COOKING_STATES[safety]} · {hot_text}", "safety_state"
    elif safety == "idle":
        status, label, detail, source = ("not_cooking", "NOT COOKING",
                                         f"Stove off or cold · {hot_text} (cooking from {HEAT_ON_C:.0f} °C)",
                                         "safety_state")
    elif safety == "shutdown":
        status, label, detail, source = ("supply_cut", "SUPPLY CUT",
                                         f"Gas cut by the controller · stove cooling, {hot_text}", "safety_state")
    elif hot is not None and hot >= HEAT_ON_C:
        status, label, source = "likely_cooking", "LIKELY COOKING", hot_field
        detail = f"Controller {safety or 'state unknown'} · {hot_text} (≥ {HEAT_ON_C:.0f} °C)"
    elif hot is not None and hot < HEAT_OFF_C:
        status, label, source = "likely_not_cooking", "LIKELY NOT COOKING", hot_field
        detail = f"Controller {safety or 'state unknown'} · {hot_text} (< {HEAT_OFF_C:.0f} °C)"
    else:
        status, label, detail, source = "unknown", "COOKING UNKNOWN", f"Controller {safety or 'state unknown'} · {hot_text}", None
    age = device["field_age_seconds"].get(source) if source else None
    last_known = device["connection"] != "online" or age is None or age > device["stale_after_seconds"]
    return {"status": status, "label": label, "detail": detail, "active": status in ACTIVE_COOKING,
            "source_field": source, "hot_region_c": hot, "last_known": last_known}


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
        by_cooking = max(members, key=lambda item: (COOKING_RANK[item["cooking"]["status"]],
                                                    SEVERITY_RANK[item["display_state"]["level"]]))
        cooking = {**by_cooking["cooking"],
                   "stations_cooking": sum(item["cooking"]["active"] for item in members)}
        stalls.append({"stall_id": stall_id, "stall_name": lead["location"]["stall_name"],
                       "cooking": cooking,
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
               "normal": 0, "unknown": 0, "connectivity_issues": 0, "cooking": 0}
    for stall in stalls:
        summary["cooking"] += stall["cooking"]["active"]
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
