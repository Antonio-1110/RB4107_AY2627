#!/usr/bin/env python3
"""Validate RB4107 MQTT JSON payloads against docs/schema/rb4107_mqtt.schema.json.

Accepts any text: every line holding a JSON object is checked, so it works on
    - `mosquitto_sub -t 'rb4107/#' -v` output (topic, space, JSON),
    - plain files with one JSON document per line.

Usage:
    mosquitto_sub -h HOST -t 'rb4107/#' -v | python3 validate_json.py
    python3 validate_json.py capture.log

Requires: pip install jsonschema
"""
import fileinput
import json
import pathlib
import sys

import jsonschema

SCHEMA = pathlib.Path(__file__).resolve().parents[2] / "docs" / "schema" / "rb4107_mqtt.schema.json"

# Message "type" -> definition in the schema. Validating against the matching
# definition gives precise error messages (the top-level oneOf only says
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


def validators(schema: dict) -> dict:
    return {
        msg_type: jsonschema.Draft202012Validator({"$defs": schema["$defs"], "$ref": f"#/$defs/{name}"})
        for msg_type, name in TYPE_TO_DEF.items()
    }


def main() -> int:
    by_type = validators(json.loads(SCHEMA.read_text()))
    checked = failed = 0
    for line in fileinput.input():
        start = line.find("{")
        if start < 0:
            continue
        try:
            doc = json.loads(line[start:])
        except json.JSONDecodeError as exc:
            failed += 1
            print(f"MALFORMED JSON: {exc}: {line.strip()[:120]}")
            continue
        checked += 1
        msg_type = doc.get("type") if isinstance(doc, dict) else None
        label = f"{str(msg_type):<17}"
        if msg_type not in by_type or doc.get("schema_version") != 1:
            failed += 1
            print(f"INVALID {label} unknown type or schema_version")
            continue
        errors = sorted(by_type[msg_type].iter_errors(doc), key=lambda e: list(e.path))
        if errors:
            failed += 1
            print(f"INVALID {label} {errors[0].message} (at {'/'.join(map(str, errors[0].path))})")
        else:
            print(f"ok      {label}")
    print(f"{checked} documents checked, {failed} problems")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
