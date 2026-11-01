"""python manage.py simulate_alert [--controller ID] [--warning-after S] [--shutdown-after S]

Plays one unattended-cooking incident for one stall, sped up, to check the
dashboard and the Telegram alerts (docs/telegram_alerts.md) without hardware:

  cooking, person at the stove -> person leaves (UNATTENDED) -> buzzer
  (WARNING, "events/warning") -> cooker shut down (SHUTDOWN, "events/shutdown")
  -> reset, back to IDLE.

It publishes what the controller would, through the broker, so the running
MQTT subscriber stores it, the dashboard shows it, the alerts go out and the
Acknowledge button works. Run it next to tools/run_dev.sh. With --direct it
needs no broker: it feeds the same handlers itself and also polls for button
presses (stop the MQTT subscriber first: only one program may poll a bot).

Every message is marked "simulation": true. Never point it at a stall whose
real controller is online: its messages would mix with the real ones.
"""
import json
import secrets
import threading
import time

import paho.mqtt.client as mqtt
from django.conf import settings
from django.core.management.base import BaseCommand, CommandError
from django.utils import timezone

from ingest import alerts
from ingest.demo import demo_controller_status, demo_telemetry
from ingest.handlers import handle
from ingest.locations import load_location_catalog, location_for
from ingest.storage import worker_status
from ingest.validation import parse, topic_prefix


class Command(BaseCommand):
    help = "Play one unattended-cooking incident (leave, buzzer, shutdown, reset) for one stall."

    def add_arguments(self, parser):
        parser.add_argument("--controller", default="controller_01",
                            help="controller_id to play it on (default controller_01)")
        parser.add_argument("--attended", type=float, default=10, help="seconds of cooking with the person there")
        parser.add_argument("--warning-after", type=float, default=20,
                            help="seconds unattended before the buzzer (the real default is 180)")
        parser.add_argument("--shutdown-after", type=float, default=40,
                            help="seconds unattended before the shutdown (the real default is 300)")
        parser.add_argument("--reset-after", type=float, default=60,
                            help="seconds after the shutdown before it is reset; 0 = stay shut down until Ctrl-C")
        parser.add_argument("--interval", type=float, default=2, help="seconds between telemetry messages")
        parser.add_argument("--direct", action="store_true",
                            help="no broker: feed the handlers directly and poll Telegram buttons here")

    def handle(self, *args, **o):
        if not 0 < o["warning_after"] < o["shutdown_after"]:
            raise CommandError("Need 0 < --warning-after < --shutdown-after.")
        if o["interval"] <= 0:
            raise CommandError("--interval must be positive")
        cid = o["controller"]
        entry = load_location_catalog()["devices"].get(cid)
        if entry is None:
            raise CommandError(f"{cid} is not in the location catalogue ({settings.LOCATION_CATALOG_FILE}).")
        if entry.get("demo_only"):
            self.stdout.write(self.style.WARNING(
                f"{cid} is also played by simulate_fleet (run_dev.sh --demo): the dashboard will flick "
                "between the two. Telegram alerts are not affected."))
        self.stdout.write(f"Stall: {location_for(cid)['stall_name']} ({cid})")
        self.stdout.write("Telegram alerts: " + (
            f"on, events {', '.join(settings.RB4107_ALERT_EVENTS)}" if alerts.enabled()
            else "OFF (set RB4107_TELEGRAM_BOT_TOKEN and RB4107_TELEGRAM_CHAT_IDS in django/.env)"))

        self.prefix = topic_prefix(settings.RB4107_MQTT["TOPIC"])
        self.base = "/".join(cid if part == "+" else part for part in self.prefix.split("/"))
        self.cid, self.boot_id, self.sequence, self.started = cid, secrets.token_hex(4), 0, time.monotonic()
        self.direct, self.client = o["direct"], None
        stop = threading.Event()
        try:
            if self.direct:
                worker_status(True, mode="demo-direct")
                if alerts.enabled():
                    threading.Thread(target=alerts.poll_acks, args=(stop,), daemon=True).start()
            else:
                cfg = settings.RB4107_MQTT
                self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"rb4107-alert-sim-{self.boot_id}")
                if cfg.get("USERNAME"):
                    self.client.username_pw_set(cfg["USERNAME"], cfg.get("PASSWORD"))
                try:
                    self.client.connect(cfg["HOST"], cfg["PORT"], cfg["KEEPALIVE_S"])
                except OSError as error:
                    raise CommandError(f"No MQTT broker at {cfg['HOST']}:{cfg['PORT']} ({error}). "
                                       "Start tools/run_dev.sh first, or use --direct.") from None
                self.client.loop_start()
            self.play(o)
            if self.direct and alerts.enabled():
                self.stdout.write("Done. Still listening for Acknowledge presses; Ctrl-C to stop.")
                while True:
                    time.sleep(1)
        except KeyboardInterrupt:
            self.stdout.write("\nStopped.")
        finally:
            stop.set()
            if self.client:
                self.client.disconnect()
                self.client.loop_stop()
            if self.direct:
                worker_status(False, mode="demo-direct", error="Demo stopped")

    def play(self, o):
        self.publish("controller/status", demo_controller_status(self.cid, True, self.boot_id))
        left_at = o["attended"]
        steps = [  # (seconds from start, state, event to publish on entering it)
            (0, "monitoring", None),
            (left_at, "unattended", None),
            (left_at + o["warning_after"], "warning", "warning"),
            (left_at + o["shutdown_after"], "shutdown", "shutdown"),
        ]
        if o["reset_after"] > 0:
            steps.append((left_at + o["shutdown_after"] + o["reset_after"], "idle", None))
        state, entered, t0 = None, 0.0, time.monotonic()
        while True:
            now = time.monotonic() - t0
            due = [s for s in steps if s[0] <= now][-1]
            if due[1] != state:
                previous, (entered, state, event) = state, due
                self.stdout.write(f"[{now:5.0f} s] {state.upper()}" + (f"  -> events/{event}" if event else ""))
                if event:
                    self.publish(f"events/{event}", self.event(event, state, previous, now - left_at))
            unattended = now - left_at if state in {"unattended", "warning", "shutdown"} else 0
            self.publish("controller/state", self.telemetry(state, now - entered, unattended, o))
            if state == "idle":
                return
            time.sleep(o["interval"])

    def telemetry(self, state, in_state_s, unattended_s, o):
        data = demo_telemetry(self.cid, 0, 0, self.boot_id, state)
        data["safety"].update(state_ms=int(in_state_s * 1000), unattended_ms=int(unattended_s * 1000),
                              warning_after_ms=int(o["warning_after"] * 1000),
                              shutdown_after_ms=int(o["shutdown_after"] * 1000),
                              effective_warning_ms=int(o["warning_after"] * 1000),
                              effective_shutdown_ms=int(o["shutdown_after"] * 1000))
        return self.stamp(data)

    def event(self, kind, state, previous, unattended_s):
        reason = {"warning": "unattended timeout", "shutdown": "shutdown timeout"}[kind]
        return self.stamp({
            "schema_version": 2, "type": "event", "controller_id": self.cid, "event": kind,
            "safety": {"state": state.upper(), "from_state": (previous or "unattended").upper(),
                       "reason": reason, "unattended_ms": int(unattended_s * 1000)},
            "fault": None, "simulation": True,
        })

    def stamp(self, data):
        self.sequence += 1
        data.update(timestamp=timezone.now().isoformat(), sequence=self.sequence, boot_id=self.boot_id,
                    uptime_ms=int((time.monotonic() - self.started) * 1000) + 60000)
        return data

    def publish(self, suffix, data):
        topic, payload = f"{self.base}/{suffix}", json.dumps(data).encode()
        msg = parse(topic, payload, self.prefix)  # never publish something Django would reject
        if self.direct:
            handle(msg)
        else:
            self.client.publish(topic, payload, qos=1, retain=False).wait_for_publish(5)
