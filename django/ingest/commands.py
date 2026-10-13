"""Dashboard -> controller commands: remote C4002 tuning (docs/c4002_tuning.md),
the operator reset of a latched shutdown, the presence filter and the cooking
temperature (docs/remote_reset.md). Nothing here can trigger a shutdown or drive
an output directly.
"""

from __future__ import annotations

import json
import secrets

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


TIER_TEMP_MAX_C = 300.0
TIER_RATE_MAX_C_PER_MIN = 1000.0
TIER_TIME_MIN_S, TIER_TIME_MAX_S = 1, 3600
TIER_FIELDS = {"elevated": ("temp_c", "warning_s", "shutdown_s"),
               "critical": ("temp_c", "rate_c_per_min", "rate_min_temp_c", "warning_s", "shutdown_s")}


def _number(group, key, low, high, label):
    value = group.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not low <= value <= high:
        raise CommandError(f"{label} must be a number from {low:g} to {high:g}")
    return value


def build_tiers_command(controller_id: str, body: dict) -> dict:
    """{"elevated": {"temp_c", "warning_s", "shutdown_s"}, "critical": {"temp_c", "rate_c_per_min",
    "rate_min_temp_c", "warning_s", "shutdown_s"}} sets the controller's temperature tiers (the hotter the
    pan, the shorter the buzzer and shutdown times), which it saves; a temperature of 0 turns a tier off.
    {"defaults": true} goes back to the menuconfig values. The controller also checks that a tier's
    shutdown comes after its warning and that CRITICAL is not below ELEVATED."""
    if not isinstance(body, dict):
        raise CommandError("body must be a JSON object")
    if body == {"defaults": True}:
        values, action = {}, "tiers_defaults"
    else:
        if set(body) != set(TIER_FIELDS) or any(not isinstance(body[k], dict) or set(body[k]) != set(f)
                                                for k, f in TIER_FIELDS.items()):
            raise CommandError("send {elevated: {temp_c, warning_s, shutdown_s}, critical: {temp_c, rate_c_per_min, "
                               "rate_min_temp_c, warning_s, shutdown_s}}, or {defaults: true}")
        e, c = body["elevated"], body["critical"]
        values = {
            "tier1_temp_dc": round(_number(e, "temp_c", 0, TIER_TEMP_MAX_C, "elevated temp_c") * 10),
            "tier1_warning_ms": round(_number(e, "warning_s", TIER_TIME_MIN_S, TIER_TIME_MAX_S, "elevated warning_s") * 1000),
            "tier1_shutdown_ms": round(_number(e, "shutdown_s", TIER_TIME_MIN_S, TIER_TIME_MAX_S, "elevated shutdown_s") * 1000),
            "tier2_temp_dc": round(_number(c, "temp_c", 0, TIER_TEMP_MAX_C, "critical temp_c") * 10),
            "tier2_rate_dc_per_min": round(_number(c, "rate_c_per_min", 0, TIER_RATE_MAX_C_PER_MIN, "critical rate_c_per_min") * 10),
            "tier2_rate_min_temp_dc": round(_number(c, "rate_min_temp_c", 0, TIER_TEMP_MAX_C, "critical rate_min_temp_c") * 10),
            "tier2_warning_ms": round(_number(c, "warning_s", TIER_TIME_MIN_S, TIER_TIME_MAX_S, "critical warning_s") * 1000),
            "tier2_shutdown_ms": round(_number(c, "shutdown_s", TIER_TIME_MIN_S, TIER_TIME_MAX_S, "critical shutdown_s") * 1000),
        }
        action = "tiers"
        if values["tier1_temp_dc"] > 0 and values["tier2_temp_dc"] > 0 and values["tier2_temp_dc"] < values["tier1_temp_dc"]:
            raise CommandError("critical must not start below elevated")
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
