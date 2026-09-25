"""Parsing and validation of RB4107 MQTT payloads (TODO section 23).

Every payload is checked against docs/schema/rb4107_mqtt.schema.json, the
same file the firmware's output is tested against. Anything malformed raises
InvalidMessage with a short reason. It never raises anything else, so one bad
message can't kill the subscriber.
"""

from __future__ import annotations

import json
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
}

MAX_PAYLOAD_BYTES = 8192


class InvalidMessage(Exception):
    """The payload could not be accepted. str(exc) is a short reason."""


@dataclass(frozen=True)
class Message:
    topic: str
    kind: str  # topic pattern, e.g. "sensors/*/presence"
    type: str
    data: dict[str, Any]


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
    if not topic.startswith(prefix + "/"):
        raise InvalidMessage(f"topic outside prefix '{prefix}'")
    parts = topic[len(prefix) + 1:].split("/")
    if len(parts) == 3 and parts[0] == "sensors":
        parts[1] = "*"
    kind = "/".join(parts)
    if kind not in TOPIC_TYPES:
        raise InvalidMessage(f"unknown topic '{topic}'")
    return kind


def topic_prefix(subscription: str) -> str:
    """'rb4107/#' -> 'rb4107'."""
    return subscription.split("/#")[0].rstrip("/")


def parse(topic: str, payload: bytes, prefix: str | None = None) -> Message:
    """Decode and validate one MQTT message. prefix defaults to the configured subscription's."""
    kind = topic_kind(topic, prefix)
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise InvalidMessage(f"payload too large ({len(payload)} bytes)")
    try:
        data = json.loads(payload.decode("utf-8"))
    except UnicodeDecodeError:
        raise InvalidMessage("payload is not UTF-8") from None
    except json.JSONDecodeError as exc:
        raise InvalidMessage(f"malformed JSON: {exc.msg} at char {exc.pos}") from None
    if not isinstance(data, dict):
        raise InvalidMessage("payload is not a JSON object")

    version = data.get("schema_version")
    if version not in settings.RB4107_SUPPORTED_SCHEMA_VERSIONS:
        raise InvalidMessage(f"unsupported schema_version {version!r}")
    msg_type = data.get("type")
    if msg_type not in TOPIC_TYPES[kind]:
        raise InvalidMessage(f"type {msg_type!r} not allowed on {kind}")

    validator = _validators(Path(settings.RB4107_SCHEMA_FILE))[msg_type]
    error = jsonschema.exceptions.best_match(validator.iter_errors(data))
    if error is not None:
        where = "/".join(str(p) for p in error.absolute_path) or "(root)"
        raise InvalidMessage(f"schema: {error.message} at {where}")
    return Message(topic=topic, kind=kind, type=msg_type, data=data)
