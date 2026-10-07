"""Dashboard -> controller commands: remote C4002 tuning (docs/c4002_tuning.md)
and the operator reset of a latched shutdown (docs/remote_reset.md). Nothing
here can trigger a shutdown or drive an output directly.
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
