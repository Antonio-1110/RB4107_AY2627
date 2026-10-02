"""Parsing and validation of RB4107 MQTT payloads (TODO section 23).

Every payload is checked against docs/schema/rb4107_mqtt.schema.json, the
same file the firmware's output is tested against. Anything malformed raises
InvalidMessage with a short reason. It never raises anything else, so one bad
message can't kill the subscriber.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
from typing import Any

import jsonschema
from django.conf import settings

# Message "type" -> definition in the schema (validating against the matching
# definition gives precise errors, instead of the top-level oneOf's
# "not valid under any of the given schemas").
TYPE_TO_DEF = {
    "telemetry": "telemetry",
    "heartbeat": "heartbeat",
    "faults": "faults",
    "presence": "presence_msg",
    "thermal": "thermal_msg",
    "node_status": "node_status",
    "event": "event",
    "controller_status": "controller_status",
    "c4002_config": "c4002_config",
    "c4002_live": "c4002_live",
}

# Which message types may appear on which topic (the part after the prefix).
TOPIC_TYPES = {
    "controller/status": {"controller_status"},
    "controller/heartbeat": {"heartbeat"},
    "controller/state": {"telemetry"},
    "controller/faults": {"faults"},
    "sensors/*/presence": {"presence"},
    "sensors/*/thermal": {"thermal"},
    "sensors/*/status": {"node_status"},
    "events/warning": {"event"},
    "events/shutdown": {"event"},
    "events/fault": {"event"},
    "sensors/*/c4002_config": {"c4002_config"},
    "sensors/*/c4002_live": {"c4002_live"},
}

# Topics the dashboard publishes on (commands to the controller). The
# subscriber sees them under rb4107/# too, and ignores them.
COMMAND_TOPICS = {"sensors/*/c4002_set"}

MAX_PAYLOAD_BYTES = 8192


class InvalidMessage(Exception):
    """The payload could not be accepted. str(exc) is a short reason."""


@dataclass(frozen=True)
class Message:
    topic: str
    kind: str  # topic pattern, e.g. "sensors/*/presence"
    type: str
    data: dict[str, Any]
    retained: bool = False
    qos: int = 0
    duplicate: bool = False


@lru_cache(maxsize=1)
def _validators(schema_file: Path) -> dict[str, jsonschema.Draft202012Validator]:
    schema = json.loads(schema_file.read_text())
    return {
        msg_type: jsonschema.Draft202012Validator({"$defs": schema["$defs"], "$ref": f"#/$defs/{name}"})
        for msg_type, name in TYPE_TO_DEF.items()
    }


def topic_kind(topic: str, prefix: str | None = None) -> str:
    """Map a concrete topic onto its pattern: rb4107/sensors/node_01/thermal -> sensors/*/thermal."""
    prefix = prefix or topic_prefix(settings.RB4107_MQTT["TOPIC"])
    prefix_parts, topic_parts = prefix.split("/"), topic.split("/")
    if len(topic_parts) <= len(prefix_parts) or any(
        expected != "+" and expected != actual
        for expected, actual in zip(prefix_parts, topic_parts)
    ):
        raise InvalidMessage(f"topic outside prefix '{prefix}'")
    parts = topic_parts[len(prefix_parts):]
    if len(parts) == 3 and parts[0] == "sensors":
        parts[1] = "*"
    kind = "/".join(parts)
    if kind not in TOPIC_TYPES and kind not in COMMAND_TOPICS:
        raise InvalidMessage(f"unknown topic '{topic}'")
    return kind


def topic_prefix(subscription: str) -> str:
    """'rb4107/#' -> 'rb4107'."""
    return subscription.split("/#")[0].rstrip("/")


def is_command_topic(topic: str, prefix: str | None = None) -> bool:
    """True for rb4107/sensors/<node>/c4002_set and other dashboard -> controller topics."""
    try:
        return topic_kind(topic, prefix) in COMMAND_TOPICS
    except InvalidMessage:
        return False


@lru_cache(maxsize=1)
def _command_validator(schema_file: Path) -> jsonschema.Draft202012Validator:
    schema = json.loads(schema_file.read_text())
    return jsonschema.Draft202012Validator({"$defs": schema["$defs"], "$ref": "#/$defs/c4002_command"})


def check_command(command: dict) -> None:
    """Validate an outgoing c4002_command against the shared schema. Raises InvalidMessage."""
    validator = _command_validator(Path(settings.RB4107_SCHEMA_FILE))
    error = jsonschema.exceptions.best_match(validator.iter_errors(command))
    if error is not None:
        where = "/".join(str(p) for p in error.absolute_path) or "(root)"
        raise InvalidMessage(f"{error.message} at {where}")


def parse(topic: str, payload: bytes, prefix: str | None = None) -> Message:
    """Decode and validate one MQTT message. prefix defaults to the configured subscription's."""
    kind = topic_kind(topic, prefix)
    if kind in COMMAND_TOPICS:
        raise InvalidMessage(f"'{topic}' is a command topic, not telemetry")
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise InvalidMessage(f"payload too large ({len(payload)} bytes)")
    try:
        data = json.loads(payload.decode("utf-8"), parse_constant=_reject_constant, parse_float=_finite_float)
    except UnicodeDecodeError:
        raise InvalidMessage("payload is not UTF-8") from None
    except json.JSONDecodeError as exc:
        raise InvalidMessage(f"malformed JSON: {exc.msg} at char {exc.pos}") from None
    except (ValueError, RecursionError):
        raise InvalidMessage("invalid or excessively nested JSON") from None
    if not isinstance(data, dict):
        raise InvalidMessage("payload is not a JSON object")

    version = data.get("schema_version")
    if not isinstance(version, int) or isinstance(version, bool) or version not in settings.RB4107_SUPPORTED_SCHEMA_VERSIONS:
        raise InvalidMessage(f"unsupported schema_version {version!r}")
    msg_type = data.get("type")
    if not isinstance(msg_type, str) or msg_type not in TOPIC_TYPES[kind]:
        raise InvalidMessage(f"type {msg_type!r} not allowed on {kind}")

    validator = _validators(Path(settings.RB4107_SCHEMA_FILE))[msg_type]
    error = jsonschema.exceptions.best_match(validator.iter_errors(data))
    if error is not None:
        where = "/".join(str(p) for p in error.absolute_path) or "(root)"
        raise InvalidMessage(f"schema: {error.message} at {where}")
    return Message(topic=topic, kind=kind, type=msg_type, data=data)


def _reject_constant(value):
    raise ValueError(f"Non-finite number: {value}")


def _finite_float(value):
    result = float(value)
    if not math.isfinite(result):
        raise ValueError("Non-finite number")
    return result
