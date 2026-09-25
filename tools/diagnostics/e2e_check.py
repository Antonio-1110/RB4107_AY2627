#!/usr/bin/env python3
"""End-to-end checklist from the MQTT side (TODO sections 29 and 30).

Subscribes to rb4107/# and ticks off everything visible from the broker:

    python3 e2e_check.py --host 192.168.1.127 --duration 300

It reports which checklist items have been seen so far. Walk through the
test (heat the pan, leave, return, reset) while it runs. Every payload is
also validated against docs/schema/rb4107_mqtt.schema.json.

Needs: pip install paho-mqtt jsonschema
"""
import argparse
import json
import pathlib
import sys
import time

import jsonschema
import paho.mqtt.client as mqtt

SCHEMA = pathlib.Path(__file__).resolve().parents[2] / "docs" / "schema" / "rb4107_mqtt.schema.json"
TYPE_TO_DEF = {"telemetry": "telemetry", "heartbeat": "heartbeat", "faults": "faults", "presence": "presence_msg",
               "thermal": "thermal_msg", "node_status": "node_status", "event": "event",
               "controller_status": "controller_status"}

CHECKS = [
    ("controller_online", "S3 connected to the broker (controller/status online)"),
    ("telemetry", "Telemetry appears in Mosquitto (controller/state)"),
    ("presence_valid", "Presence readings reach the S3 (valid presence in telemetry)"),
    ("presence_changes", "Presence changes are reported (seen both present and absent)"),
    ("thermal_valid", "Thermal readings reach the S3 (valid thermal in telemetry)"),
    ("monitoring", "Safety state responds: MONITORING reached (cooking detected)"),
    ("unattended", "Safety state responds: UNATTENDED reached"),
    ("warning_event", "Buzzer activates: WARNING event published"),
    ("shutdown_event", "Relay activates: SHUTDOWN event published"),
    ("schema_valid", "Every payload valid against the schema"),
]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--prefix", default="rb4107")
    ap.add_argument("--duration", type=int, default=300, help="seconds to watch")
    args = ap.parse_args()

    schema = json.loads(SCHEMA.read_text())
    validators = {t: jsonschema.Draft202012Validator({"$defs": schema["$defs"], "$ref": f"#/$defs/{d}"})
                  for t, d in TYPE_TO_DEF.items()}
    seen = {key: False for key, _ in CHECKS}
    seen["schema_valid"] = True
    presence_values = set()
    counts = {"messages": 0, "invalid": 0}

    def on_message(client, userdata, msg):
        counts["messages"] += 1
        try:
            doc = json.loads(msg.payload)
            validator = validators[doc["type"]]
        except (ValueError, KeyError, TypeError):
            counts["invalid"] += 1
            seen["schema_valid"] = False
            return
        if list(validator.iter_errors(doc)):
            counts["invalid"] += 1
            seen["schema_valid"] = False
        kind = doc["type"]
        if kind == "controller_status" and doc.get("online"):
            seen["controller_online"] = True
        if kind == "telemetry":
            seen["telemetry"] = True
            p, t, s = doc["presence"], doc["thermal"], doc["safety"]
            if p["valid"]:
                seen["presence_valid"] = True
                presence_values.add(p["detected"])
                seen["presence_changes"] = presence_values >= {True, False}
            seen["thermal_valid"] |= t["valid"]
            seen["monitoring"] |= s["state"] == "MONITORING"
            seen["unattended"] |= s["state"] == "UNATTENDED"
        if kind == "event":
            seen["warning_event"] |= doc["event"] == "warning"
            seen["shutdown_event"] |= doc["event"] == "shutdown"

    client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    client.on_message = on_message
    client.on_connect = lambda c, u, f, rc, p: c.subscribe(f"{args.prefix}/#", qos=1)
    client.connect(args.host, args.port)
    client.loop_start()
    end = time.time() + args.duration
    try:
        while time.time() < end and not all(seen.values()):
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    client.loop_stop()

    print(f"\n{counts['messages']} messages, {counts['invalid']} invalid\n")
    for key, text in CHECKS:
        print(f"  [{'x' if seen[key] else ' '}] {text}")
    print("\nNot visible over MQTT (check by hand): buzzer sound, relay click/contacts, Django log lines.")
    return 0 if all(seen.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
