"""Publish synthetic firmware-shaped reports, or feed the same handlers directly."""
import json
import secrets
import time

import paho.mqtt.client as mqtt
from django.conf import settings
from django.core.management.base import BaseCommand, CommandError

from ingest.demo import demo_telemetry, demo_thermal_frame
from ingest.handlers import handle
from ingest.locations import load_location_catalog
from ingest.storage import worker_status
from ingest.validation import parse, topic_prefix


class Command(BaseCommand):
    help = "Generate labelled synthetic schema-v2 fleet data (no actuator commands)."

    def add_arguments(self, parser):
        parser.add_argument("--direct", action="store_true", help="Use the validator/database without MQTT")
        parser.add_argument("--once", action="store_true", help="Generate one snapshot and exit")
        parser.add_argument("--interval", type=float, default=2)

    def handle(self, *args, **options):
        if options["interval"] <= 0:
            raise CommandError("--interval must be positive")
        ids = [key for key, value in load_location_catalog()["devices"].items() if value.get("demo_only")]
        if not ids:
            raise CommandError("The location catalogue has no demo_only controllers. "
                               "Set RB4107_LOCATION_CATALOG_FILE=locations.demo.json first.")
        prefix = topic_prefix(settings.RB4107_MQTT["TOPIC"])
        client = None
        try:
            if options["direct"]:
                worker_status(True, mode="demo-direct")
            else:
                cfg = settings.RB4107_MQTT
                client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="rb4107-fleet-demo")
                if cfg.get("USERNAME"):
                    client.username_pw_set(cfg["USERNAME"], cfg.get("PASSWORD"))
                client.connect(cfg["HOST"], cfg["PORT"], cfg["KEEPALIVE_S"])
                client.loop_start()
            self.stdout.write(f"Simulating {len(ids)} stations; {'direct database path' if options['direct'] else 'MQTT'}")
            tick = 0
            boot_id = secrets.token_hex(4)  # one "boot" per simulator run
            while True:
                if options["direct"]:
                    worker_status(True, mode="demo-direct")
                for index, controller_id in enumerate(ids):
                    concrete = "/".join(controller_id if part == "+" else part for part in prefix.split("/"))
                    for topic, data in (
                        (f"{concrete}/controller/state", demo_telemetry(controller_id, index, tick, boot_id)),
                        (f"{concrete}/sensors/node_03/thermal_frame",
                         demo_thermal_frame(controller_id, index, tick, boot_id)),
                    ):
                        payload = json.dumps(data).encode()
                        msg = parse(topic, payload, prefix)
                        if options["direct"]:
                            handle(msg)
                        else:
                            # Never replace a real controller's retained broker snapshot.
                            client.publish(topic, payload, qos=1, retain=False).wait_for_publish(5)
                tick += 1
                if options["once"]:
                    break
                time.sleep(options["interval"])
        except KeyboardInterrupt:
            pass
        finally:
            if client:
                client.disconnect()
                client.loop_stop()
            if options["direct"]:
                worker_status(False, mode="demo-direct", error="Demo stopped")
