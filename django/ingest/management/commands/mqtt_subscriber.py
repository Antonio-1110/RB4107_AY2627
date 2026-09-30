"""python manage.py mqtt_subscriber

Persistent MQTT subscriber for the RB4107 system (TODO section 24). It runs
as its own process and is independent of any HTTP request. It connects to
Mosquitto, subscribes to rb4107/#, validates and handles every message,
reconnects on its own after broker failures, and shuts down cleanly on
Ctrl-C / SIGTERM.
"""

import logging
import signal
import threading
import time

from django.conf import settings
from django.core.management.base import BaseCommand
from django.db import close_old_connections

from ingest import handlers
from ingest.subscriber import Subscriber
from ingest.storage import record_rejection, worker_status

log = logging.getLogger("rb4107.mqtt")


class Command(BaseCommand):
    help = "Run the persistent RB4107 MQTT subscriber."

    def add_arguments(self, parser):
        parser.add_argument("--host", help="broker host (default: RB4107_MQTT_HOST)")
        parser.add_argument("--port", type=int, help="broker port (default: RB4107_MQTT_PORT)")
        parser.add_argument("--topic", help="subscription (default: RB4107_MQTT_TOPIC)")
        parser.add_argument("--stats-interval", type=int, default=60,
                            help="seconds between status lines (0 = off)")

    def handle(self, *args, **options):
        config = dict(settings.RB4107_MQTT)
        for key in ("host", "port", "topic"):
            if options[key] is not None:
                config[key.upper()] = options[key]

        subscriber = Subscriber(config, on_rejected=record_rejection)
        stop = threading.Event()

        def request_stop(signum, frame):
            stop.set()
            subscriber.stop()

        signal.signal(signal.SIGINT, request_stop)
        signal.signal(signal.SIGTERM, request_stop)

        worker_status(False)
        reporter = threading.Thread(target=self._report, args=(subscriber, stop, options["stats_interval"]), daemon=True)
        reporter.start()
        try:
            subscriber.run_forever()
        finally:
            stop.set()
            subscriber.stop()
            reporter.join(timeout=25)
            worker_status(False, error="Subscriber stopped")

    @staticmethod
    def _report(subscriber: Subscriber, stop: threading.Event, interval: int) -> None:
        last_log = time.monotonic()
        while not stop.wait(2):
            close_old_connections()
            try:
                worker_status(subscriber.subscribed.is_set() and not subscriber.last_error,
                              error=subscriber.last_error)
            except Exception:
                log.exception("could not update dashboard worker heartbeat")
            finally:
                close_old_connections()
            if interval > 0 and time.monotonic() - last_log >= interval:
                with handlers._stats_lock:
                    by_type = ", ".join(f"{k}={v}" for k, v in sorted(handlers.stats.items())) or "none"
                log.info("status: %s, received %d, rejected %d | %s",
                         "connected" if subscriber.connected.is_set() else "DISCONNECTED",
                         subscriber.received, subscriber.rejected, by_type)
                last_log = time.monotonic()
