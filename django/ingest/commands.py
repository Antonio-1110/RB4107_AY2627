"""Dashboard -> controller commands: remote C4002 tuning (docs/c4002_tuning.md),
the operator reset of a latched shutdown, the presence filter and the cooking
temperature (docs/remote_reset.md), and the log levels (docs/logging.md).
Nothing here can trigger a shutdown or drive an output directly.
"""

from __future__ import annotations

import json
import re
import secrets
from functools import lru_cache
from pathlib import Path

import paho.mqtt.publish as mqtt_publish
from django.conf import settings

from . import validation

SETTING_KEYS = {
    "report_period_ds", "range_min_cm", "range_max_cm", "resolution_cm", "motion_sensitivity",
    "presence_sensitivity", "disappear_delay_s", "lock_time_ds", "motion_gates", "presence_gates",
    "motion_thresholds", "presence_thresholds",
}
CALIBRATION_KEYS = {"calibration_delay_s", "calibration_duration_s"}


class CommandError(Exception):
    """The command was refused before sending. str(exc) is shown to the user."""


class BrokerUnavailable(Exception):
    """The MQTT broker could not be reached."""


def build_c4002_command(controller_id: str, body: dict) -> dict:
    """Turn the dashboard's request body into a schema-valid c4002_command."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    action = body.get("action")
    allowed = {"action"} | (SETTING_KEYS if action == "apply" else set()) | (
        CALIBRATION_KEYS if action == "calibrate" else set())
    extra = sorted(set(body) - allowed)
    if extra:
        raise CommandError(f"not allowed with action {action!r}: {', '.join(extra)}")
    if action == "apply" and not set(body) & SETTING_KEYS:
        raise CommandError("apply needs at least one setting")
    command = {"schema_version": 2, "type": "c4002_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, **body}
    if "range_min_cm" in command and "range_max_cm" in command and command["range_min_cm"] > command["range_max_cm"]:
        raise CommandError("range_min_cm must not be above range_max_cm")
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


def build_reset_command(controller_id: str) -> dict:
    """The operator reset: the same as the controller console's `reset` command."""
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": "reset"}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


PRESENCE_FILTER_KEYS = ("absence_ms", "return_ms", "return_gap_ms", "warning_ms", "shutdown_ms")


def build_presence_filter_command(controller_id: str, body: dict) -> dict:
    """{"absence_ms", "return_ms", "return_gap_ms", "warning_ms", "shutdown_ms"} sets the
    presence filter and the buzzer/shutdown times until the controller reboots;
    {"defaults": true} goes back to the menuconfig values. Whether shutdown_ms
    must be above warning_ms depends on shutdown_counts_from, so the controller
    checks that (and the dashboard before sending)."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "presence_filter_defaults"
    else:
        extra = sorted(set(body) - set(PRESENCE_FILTER_KEYS))
        missing = [key for key in PRESENCE_FILTER_KEYS if key not in body]
        if extra or missing:
            raise CommandError(f"send {', '.join(PRESENCE_FILTER_KEYS)} (milliseconds), or {{\"defaults\": true}}")
        values, action = {key: body[key] for key in PRESENCE_FILTER_KEYS}, "presence_filter"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


COOKING_TEMP_MAX_C = 300.0


def build_cooking_temp_command(controller_id: str, body: dict) -> dict:
    """{"cooking_temp_c": 55.5} sets the controller's cooking temperature (cooking
    when the hottest pixel reaches it), which the controller saves and also sends
    to the thermal node as its hot-pixel threshold; {"defaults": true} goes back
    to the menuconfig value."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "cooking_temp_defaults"
    else:
        value = body.get("cooking_temp_c")
        if set(body) != {"cooking_temp_c"} or isinstance(value, bool) or not isinstance(value, (int, float)):
            raise CommandError('send {"cooking_temp_c": <degC>}, or {"defaults": true}')
        if not 0 <= value <= COOKING_TEMP_MAX_C:
            raise CommandError(f"cooking_temp_c must be 0 to {COOKING_TEMP_MAX_C:g} degC")
        values, action = {"cooking_temp_dc": round(value * 10)}, "cooking_temp"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


PAUSE_BUDGET_MAX_S = 600


def build_attendance_command(controller_id: str, body: dict) -> dict:
    """{"pause_budget_s": 30, "pause_budget_pct": 25, "fusion": "all"} sets how long a person passing may stop the
    unattended clock (per timeline, in seconds and as a share of the shutdown time in force; 0 = never pause)
    and whether someone only counts as at the stove when every working radar sees them ("all") or either
    does ("any"); the controller saves it. {"defaults": true} goes back to the menuconfig values."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "attendance_defaults"
    else:
        if set(body) != {"pause_budget_s", "pause_budget_pct", "fusion"}:
            raise CommandError('send {"pause_budget_s", "pause_budget_pct", "fusion"}, or {"defaults": true}')
        budget = _number(body, "pause_budget_s", 0, PAUSE_BUDGET_MAX_S, "pause_budget_s")
        pct = _number(body, "pause_budget_pct", 0, 100, "pause_budget_pct")
        if body["fusion"] not in ("all", "any"):
            raise CommandError('fusion must be "all" or "any"')
        values = {"pause_budget_ms": round(budget * 1000), "pause_budget_pct": round(pct), "presence_fusion": body["fusion"]}
        action = "attendance"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


RADAR_FIELDS = {"quiet_gap_s": ("radar_quiet_gap_ms", 0, 10), "dwell_s": ("radar_dwell_ms", 0, 60),
                "stationary_min_s": ("radar_stationary_min_ms", 0, 60), "settle_window_s": ("radar_settle_window_ms", 0.5, 60)}
RADAR_CM = {"settle_span_cm": ("radar_settle_span_cm", 1000), "zone_max_cm": ("radar_zone_max_cm", 1200)}


def build_radar_rules_command(controller_id: str, body: dict) -> dict:
    """{"mode": "off"|"observe"|"enforce", "quiet_gap_s", "dwell_s", "stationary_min_s", "settle_window_s",
    "settle_span_cm", "zone_max_cm"} sets the radar state and distance rules (a placeholder; docs/safety_state_machine.md),
    which the controller saves. {"defaults": true} goes back to the menuconfig values."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "radar_rules_defaults"
    else:
        if set(body) != {"mode", *RADAR_FIELDS, *RADAR_CM}:
            raise CommandError('send {"mode", ' + ", ".join(f'"{k}"' for k in (*RADAR_FIELDS, *RADAR_CM)) + '}, or {"defaults": true}')
        if body["mode"] not in ("off", "observe", "enforce"):
            raise CommandError('mode must be "off", "observe" or "enforce"')
        values = {"radar_mode": body["mode"]}
        for key, (name, low, high) in RADAR_FIELDS.items():
            values[name] = round(_number(body, key, low, high, key) * 1000)
        for key, (name, high) in RADAR_CM.items():
            values[name] = round(_number(body, key, 0, high, key))
        action = "radar_rules"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


TIER_TEMP_MAX_C = 300.0
TIER_TIME_MIN_S, TIER_TIME_MAX_S = 1, 3600
TIER_NUMBERS = (2, 3, 4, 5)  # tier 1 is the base times (presence filter and timers)
TIER_FIELDS = ("temp_c", "warning_s", "shutdown_s")


def _number(group, key, low, high, label):
    value = group.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
        raise CommandError(f"{label} must be a number from {low:g} to {high:g}")
    return value


def build_tiers_command(controller_id: str, body: dict) -> dict:
    """{"levels": [{"temp_c", "warning_s", "shutdown_s"} x 4]} sets the controller's temperature tiers 2 to 5
    (the hotter the pan, the shorter the buzzer and shutdown times; tier 1 is the base times), which it saves;
    a temperature of 0 turns a tier off. {"defaults": true} goes back to the menuconfig values. The controller
    also checks that a tier's shutdown comes after its warning and that no tier starts below the one before."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "tiers_defaults"
    else:
        levels = body.get("levels")
        if set(body) != {"levels"} or not isinstance(levels, list) or len(levels) != len(TIER_NUMBERS) or any(
                not isinstance(level, dict) or set(level) != set(TIER_FIELDS) for level in levels):
            raise CommandError("send {levels: [{temp_c, warning_s, shutdown_s} for tiers 2, 3, 4 and 5]}, or {defaults: true}")
        values, below = {}, 0
        for n, level in zip(TIER_NUMBERS, levels):
            temp_dc = round(_number(level, "temp_c", 0, TIER_TEMP_MAX_C, f"tier {n} temp_c") * 10)
            values[f"tier{n}_temp_dc"] = temp_dc
            values[f"tier{n}_warning_ms"] = round(_number(level, "warning_s", TIER_TIME_MIN_S, TIER_TIME_MAX_S, f"tier {n} warning_s") * 1000)
            values[f"tier{n}_shutdown_ms"] = round(_number(level, "shutdown_s", TIER_TIME_MIN_S, TIER_TIME_MAX_S, f"tier {n} shutdown_s") * 1000)
            if temp_dc > 0:
                if temp_dc < below:
                    raise CommandError(f"tier {n} must not start below the tier before it")
                below = temp_dc
        action = "tiers"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


@lru_cache(maxsize=1)
def safety_params() -> tuple[dict, ...]:
    """The controller's other safety settings, generated from firmware/components/rb_params/include/rb_params.def
    by tools/safety_rules.py: key, kind, min, max, default (wire units), label, options."""
    return tuple(json.loads((Path(__file__).with_name("safety_params.json")).read_text())["params"])


def build_params_command(controller_id: str, body: dict) -> dict:
    """{"<key>": integer, ...} with every key of safety_params.json sets the controller's other safety settings
    (policies, fault/node/valve timings, the tier, trend and hold tuning), which it saves; {"defaults": true}
    goes back to the menuconfig and built-in values. Values are in the units of rb_params.def (ms, 0.1 °C, ...).
    The controller also refuses combinations that don't fit together."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    params = safety_params()
    if body == {"defaults": True}:
        values, action = {}, "params_defaults"
    else:
        keys = [p["key"] for p in params]
        missing, extra = sorted(set(keys) - set(body)), sorted(set(body) - set(keys))
        if missing or extra:
            raise CommandError("send every setting, or {defaults: true}" + (f"; missing: {', '.join(missing)}" if missing else "")
                               + (f"; unknown: {', '.join(extra)}" if extra else ""))
        values = {}
        for p in params:
            value = body[p["key"]]
            if isinstance(value, bool) or not isinstance(value, int) or not p["min"] <= value <= p["max"]:
                raise CommandError(f"{p['label']} ({p['key']}) must be a whole number from {p['min']} to {p['max']}")
            values[p["key"]] = value
        if values["node_stale_ms"] >= values["node_offline_ms"]:
            raise CommandError("a sensor node must go STALE before it goes OFFLINE")
        action = "params"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


COOKWARE_PROFILES = ("mixed", "light", "heavy", "liquid")


def build_cookware_command(controller_id: str, body: dict) -> dict:
    """{"cookware": "light"} picks the controller's cookware profile (how fast the pans lose heat),
    which it saves; {"defaults": true} goes back to the menuconfig profile."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "cookware_defaults"
    else:
        value = body.get("cookware")
        if set(body) != {"cookware"} or value not in COOKWARE_PROFILES:
            raise CommandError(f'send {{"cookware": one of {", ".join(COOKWARE_PROFILES)}}}, or {{"defaults": true}}')
        values, action = {"cookware": value}, "cookware"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


LOG_LEVELS = ("none", "error", "warn", "info", "debug", "verbose")
LOG_TAG = re.compile(r"^(\*|[A-Za-z0-9_.-]{1,15})$")


def build_log_level_command(controller_id: str, body: dict) -> dict:
    """{"tag": "SAFETY", "level": "debug"} sets one tag's log level ("*" = the default for every tag) until the
    controller reboots, as the console's `log` command; {"defaults": true} goes back to the menuconfig levels.
    The answer is the levels in the next controller/log message (docs/logging.md)."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "log_level_defaults"
    else:
        tag, level = body.get("tag"), body.get("level")
        if set(body) != {"tag", "level"}:
            raise CommandError('send {"tag", "level"}, or {"defaults": true}')
        if not isinstance(tag, str) or not LOG_TAG.match(tag):
            raise CommandError('tag must be "*" or 1 to 15 letters, digits, _ - .')
        if level not in LOG_LEVELS:
            raise CommandError(f"level must be one of {', '.join(LOG_LEVELS)}")
        values, action = {"log_tag": tag, "log_level": level}, "log_level"
    command = {"schema_version": 2, "type": "controller_command", "controller_id": controller_id,
               "request_id": secrets.randbelow(65535) + 1, "action": action, **values}
    try:
        validation.check_command(command)
    except validation.InvalidMessage as exc:
        raise CommandError(str(exc)) from None
    return command


def controller_prefix(controller_id: str) -> str:
    """The configured prefix with a '+' level filled in: with rb4107/+/# every
    controller has its own rb4107/<controller_id> tree, and a command must go
    to that one (a topic with a wildcard can't be published to)."""
    prefix = validation.topic_prefix(settings.RB4107_MQTT["TOPIC"])
    return "/".join(controller_id if part == "+" else part for part in prefix.split("/"))


def controller_command_topic(controller_id: str) -> str:
    return f"{controller_prefix(controller_id)}/controller/command"


def command_topic(controller_id: str, node: str) -> str:
    return f"{controller_prefix(controller_id)}/sensors/{node}/c4002_set"


def publish(topic: str, command: dict) -> None:
    """Publish once with QoS 1, not retained: an old command must never replay."""
    cfg = settings.RB4107_MQTT
    auth = {"username": cfg["USERNAME"], "password": cfg.get("PASSWORD")} if cfg.get("USERNAME") else None
    try:
        mqtt_publish.single(topic, json.dumps(command, separators=(",", ":")), qos=1, retain=False,
                            hostname=cfg["HOST"], port=cfg["PORT"], auth=auth, keepalive=10)
    except (OSError, ValueError) as exc:
        raise BrokerUnavailable(f"MQTT broker {cfg['HOST']}:{cfg['PORT']} unreachable ({exc})") from None
