"""Application layer for validated RB4107 messages (TODO section 23).

Stores validated messages and logs telemetry and events in a readable form.
The same persistent data powers the monitoring dashboard.
"""

from __future__ import annotations

import logging
import threading
from collections import Counter
from typing import Callable

from .validation import Message
from .storage import persist

telemetry_log = logging.getLogger("rb4107.telemetry")
event_log = logging.getLogger("rb4107.event")
sensor_log = logging.getLogger("rb4107.sensor")
fault_log = logging.getLogger("rb4107.fault")

_stats_lock = threading.Lock()
stats: Counter = Counter()


def _node_text(n: dict) -> str:
    """node_01:present, node_02:OFFLINE, node_03:ok"""
    if n["link"] != "ONLINE":
        return f"{n['sensor_node']}:{n['link']}"
    if not n["valid"]:
        return f"{n['sensor_node']}:INVALID"
    if n["role"] == "presence":
        return f"{n['sensor_node']}:{'present' if n['detected'] else 'absent'}"
    return f"{n['sensor_node']}:ok"


def _num(value, unit: str = "") -> str:
    return "n/a" if value is None else f"{value:.1f}{unit}"


def handle_telemetry(msg: Message) -> None:
    d = msg.data
    s, t = d["safety"], d["thermal"]
    telemetry_log.info(
        "%s seq=%d %s | state=%s unattended=%.1fs buzzer=%s shutdown=%s | presence %s | nodes %s | hot %s rate %s | faults=%s",
        d["controller_id"], d["sequence"], d["timestamp"] or f"uptime {d['uptime_ms']} ms",
        s["state"], s["unattended_ms"] / 1000, s["buzzer"], s["shutdown"],
        d["presence_state"], " ".join(_node_text(n) for n in d["nodes"]),
        _num(t["hot_region_c"], "C") if t["valid"] else "INVALID", _num(t["rate_c_per_min"], "C/min"),
        ",".join(d["faults"]) or "none",
    )


def handle_event(msg: Message) -> None:
    d = msg.data
    safety, fault = d["safety"], d["fault"]
    kind = d["event"]
    if kind in ("warning", "shutdown") or (kind == "state_change" and safety["state"] == "FAULT"):
        event_log.warning(
            "%s %s: %s -> %s (%s), unattended %.1fs",
            d["controller_id"], kind.upper(), safety["from_state"], safety["state"], safety["reason"],
            safety["unattended_ms"] / 1000,
        )
    elif fault is not None:
        level = logging.WARNING if kind == "fault_raised" else logging.INFO
        fault_log.log(level, "%s %s %s fault %s (state %s)", d["controller_id"], fault["class"], fault["name"],
                      "RAISED" if kind == "fault_raised" else "cleared", safety["state"])
    else:
        event_log.info("%s %s (state %s)", d["controller_id"], kind, safety["state"])


def handle_faults(msg: Message) -> None:
    faults = msg.data["faults"]
    names = ", ".join(f"{f['name']}[{f['class']}]" for f in faults) or "none"
    fault_log.info("%s active faults: %s", msg.data["controller_id"], names)


def handle_node_status(msg: Message) -> None:
    d = msg.data
    level = logging.INFO if d["link"] == "ONLINE" and d["valid"] else logging.WARNING
    sensor_log.log(level, "%s (%s node) %s valid=%s missed=%d restarts=%d",
                   d["sensor_node"], d["role"], d["link"], d["valid"], d["missed_packets"], d["restarts"])


def handle_controller_status(msg: Message) -> None:
    online = msg.data["online"]
    (telemetry_log.info if online else telemetry_log.warning)(
        "controller %s is %s", msg.data["controller_id"], "ONLINE" if online else "OFFLINE (last will)")


def handle_c4002_config(msg: Message) -> None:
    d = msg.data
    level = logging.INFO if d["result"] == "ok" else logging.WARNING
    settings = d["settings"]
    summary = (f"range {settings['range_min_cm']}-{settings['range_max_cm']} cm, "
               f"sensitivity motion={settings['motion_sensitivity']} presence={settings['presence_sensitivity']}"
               if settings else "no settings")
    sensor_log.log(level, "%s C4002 %s request %d: %s%s | %s%s", d["sensor_node"], d["action"] or "command",
                   d["request_id"], d["result"], f" ({d['error']})" if d["error"] else "", summary,
                   f" | calibrating, {d['calibration_remaining_s']} s left" if d["calibration_remaining_s"] else "")


def handle_quiet(msg: Message) -> None:
    """Routine per-period messages: counted, logged only at DEBUG."""
    telemetry_log.debug("%s %s", msg.topic, msg.data)


HANDLERS: dict[str, Callable[[Message], None]] = {
    "telemetry": handle_telemetry,
    "event": handle_event,
    "faults": handle_faults,
    "node_status": handle_node_status,
    "controller_status": handle_controller_status,
    "heartbeat": handle_quiet,
    "presence": handle_quiet,
    "thermal": handle_quiet,
    "c4002_config": handle_c4002_config,
    "c4002_live": handle_quiet,
}


def handle(msg: Message) -> None:
    persist(msg)
    with _stats_lock:
        stats[msg.type] += 1
    HANDLERS[msg.type](msg)
