#!/usr/bin/env python3
"""Critical failure test, MacBook side (TODO section 30).

    python3 critical_failure_test.py --host localhost --outage 45

Flow:
  1. Watch rb4107/# until telemetry and heartbeats flow (baseline).
  2. Stop Mosquitto (--stop-cmd). While it is down, do the safety actions by
     hand (e.g. walk away from the hot pan): the S3 must still warn and shut down.
  3. Restart Mosquitto (--start-cmd).
  4. Check from the broker side that:
       - telemetry resumes on its own (the S3 reconnects),
       - the controller did not reboot (uptime continued),
       - the safety loop kept running the whole time (safety_loop_count kept up with uptime),
       - the S3 reported the telemetry fault (mqtt_disconnected events),
       - QoS 1 events from the outage (warning/shutdown) arrive after reconnect.
  The S3's own verdict ([CRITICAL] ... PASS) is in its serial log.

For the second variant (MacBook disconnected entirely), unplug the MacBook's
network or power it off instead. This script can't observe that, so use
the S3 serial log (project 30) as the record.

Needs: pip install paho-mqtt
"""
import argparse
import json
import pathlib
import shlex
import subprocess
import sys
import threading
import time

import paho.mqtt.client as mqtt

ROOT = pathlib.Path(__file__).resolve().parents[2]


class Watcher:
    def __init__(self, host, port, prefix):
        self.prefix = prefix
        self.lock = threading.Lock()
        self.heartbeats = []   # (arrival, doc)
        self.telemetry = []
        self.events = []
        self.client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
        self.client.on_connect = lambda c, u, f, rc, p: c.subscribe(f"{prefix}/#", qos=1)
        self.client.on_message = self.on_message
        self.client.reconnect_delay_set(1, 2)
        self.client.connect(host, port)
        self.client.loop_start()

    def on_message(self, client, userdata, msg):
        try:
            doc = json.loads(msg.payload)
        except ValueError:
            return
        now = time.time()
        with self.lock:
            kind = doc.get("type")
            if kind == "heartbeat":
                self.heartbeats.append((now, doc))
            elif kind == "telemetry":
                self.telemetry.append((now, doc))
            elif kind == "event":
                self.events.append((now, doc))

    def last(self, attr, after=0.0):
        with self.lock:
            items = [d for t, d in getattr(self, attr) if t >= after]
        return items[-1] if items else None

    def wait_for(self, attr, after, timeout):
        end = time.time() + timeout
        while time.time() < end:
            doc = self.last(attr, after)
            if doc is not None:
                return doc
            time.sleep(0.2)
        return None


def run(cmd: str, background: bool = False):
    print(f"   $ {cmd}")
    if background:
        return subprocess.Popen(cmd, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                start_new_session=True)
    return subprocess.run(cmd, shell=True, check=False)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="localhost")
    ap.add_argument("--port", type=int, default=1883)
    ap.add_argument("--prefix", default="rb4107")
    ap.add_argument("--outage", type=int, default=45, help="seconds the broker stays down")
    ap.add_argument("--stop-cmd", default="pkill -x mosquitto")
    ap.add_argument("--start-cmd", default=shlex.quote(str(ROOT / "tools" / "mqtt" / "start_broker.sh")))
    ap.add_argument("--resume-timeout", type=int, default=60)
    ap.add_argument("--tick-ms", type=int, default=100, help="RB_CTRL_SAFETY_TICK_MS")
    args = ap.parse_args()

    w = Watcher(args.host, args.port, args.prefix)
    print("1. waiting for the controller's telemetry...")
    before = w.wait_for("heartbeats", 0, 30)
    if before is None:
        print("   no heartbeat received: is the S3 running and connected?")
        return 2
    state_before = w.last("telemetry")
    print(f"   baseline: uptime {before['uptime_ms']} ms, safety loops {before['safety_loop_count']}, "
          f"state {before['safety_state']}")

    print(f"2. stopping the broker for {args.outage} s. Walk away from the hot pan now.")
    run(args.stop_cmd)
    time.sleep(args.outage)

    print("3. restarting the broker")
    restarted = time.time()
    run(args.start_cmd, background=True)

    print("4. waiting for telemetry to resume...")
    after = w.wait_for("heartbeats", restarted, args.resume_timeout)
    time.sleep(3)  # let queued QoS 1 events arrive
    state_after = w.last("telemetry", restarted)
    w.client.loop_stop()

    checks = []
    checks.append(("telemetry resumed automatically after the broker restart", after is not None))
    if after is not None:
        d_uptime = after["uptime_ms"] - before["uptime_ms"]
        d_loops = after["safety_loop_count"] - before["safety_loop_count"]
        expected_loops = d_uptime / args.tick_ms
        checks.append((f"controller did not reboot (uptime +{d_uptime / 1000:.0f} s)", d_uptime >= args.outage * 800))
        checks.append((f"safety loop kept running ({d_loops} loops in {d_uptime / 1000:.0f} s, "
                       f"~{expected_loops:.0f} expected)", d_loops >= 0.8 * expected_loops))
    events = [d for t, d in w.events if t >= restarted]
    mqtt_fault = [e for e in events if (e.get("fault") or {}).get("name") == "mqtt_disconnected"]
    checks.append(("S3 reported the telemetry fault (mqtt_disconnected events)", bool(mqtt_fault)))

    print("\nevents delivered after reconnect (QoS 1, queued on the S3 during the outage):")
    for e in events:
        s = e["safety"]
        what = e["fault"]["name"] if e.get("fault") else f"{s['from_state']} -> {s['state']}"
        print(f"   {e['event']:<14} {what}  (uptime {e['uptime_ms']} ms)")
    if state_before and state_after:
        print(f"\nsafety state before: {state_before['safety']['state']}, "
              f"after: {state_after['safety']['state']} (unattended {state_after['safety']['unattended_ms']} ms)")

    print()
    for text, ok in checks:
        print(f"  [{'PASS' if ok else 'FAIL'}] {text}")
    print("\nAlso check by hand: the buzzer and relay acted during the outage, and the S3 serial log shows\n"
          "'[CRITICAL] outage ... -> PASS'.")
    return 0 if all(ok for _, ok in checks) else 1


if __name__ == "__main__":
    sys.exit(main())
