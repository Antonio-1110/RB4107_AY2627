"""MQTT subscriber: connects to Mosquitto, subscribes to rb4107/#, validates
and dispatches every message (TODO sections 23-24).

It is not tied to HTTP requests in any way. The persistent process is the
`mqtt_subscriber` management command.
"""

from __future__ import annotations

import logging
import threading
from dataclasses import replace
from typing import Callable

import paho.mqtt.client as mqtt

from . import handlers, validation

log = logging.getLogger("rb4107.mqtt")


class Subscriber:
    def __init__(self, config: dict, handle: Callable[[validation.Message], None] = handlers.handle, on_rejected=None):
        self.config = config
        self.handle = handle
        self.on_rejected = on_rejected
        self.prefix = validation.topic_prefix(config["TOPIC"])
        self.received = 0
        self.rejected = 0
        self.connected = threading.Event()
        self.subscribed = threading.Event()
        self.last_error = ""
        self._stopping = False
        self.client = mqtt.Client(
            callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
            client_id=config["CLIENT_ID"],
            clean_session=not config["PERSISTENT_SESSION"],
        )
        if config.get("USERNAME"):
            self.client.username_pw_set(config["USERNAME"], config.get("PASSWORD"))
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_subscribe = self._on_subscribe
        self.client.on_message = self._on_message

    # --- callbacks (run in paho's network thread) ---

    def _on_connect(self, client, userdata, flags, reason_code, properties):
        self.subscribed.clear()
        if reason_code.is_failure:
            self.last_error = str(reason_code)
            log.warning("broker refused connection: %s", reason_code)
            return
        log.info("broker connected (%s:%d, session %s)", self.config["HOST"], self.config["PORT"],
                 "resumed" if flags.session_present else "new")
        # (Re)subscribe on every connect so a reconnect never loses the subscription.
        client.subscribe(self.config["TOPIC"], qos=self.config["QOS"])
        self.connected.set()
        self.last_error = ""

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        self.connected.clear()
        self.subscribed.clear()
        if self._stopping:
            log.info("disconnected from broker")
        else:
            log.warning("broker disconnected (%s); reconnecting", reason_code)

    def _on_subscribe(self, client, userdata, mid, reason_codes, properties):
        if any(rc.is_failure for rc in reason_codes):
            self.last_error = "Broker rejected subscription"
            self.subscribed.clear()
        else:
            self.subscribed.set()
        log.info("subscribed to %s (%s)", self.config["TOPIC"], ", ".join(str(rc) for rc in reason_codes))

    def _on_message(self, client, userdata, message):
        if validation.is_command_topic(message.topic, self.prefix):
            return  # our own dashboard commands to the controller
        self.received += 1
        try:
            msg = validation.parse(message.topic, message.payload, self.prefix)
        except validation.InvalidMessage as exc:
            self.rejected += 1
            log.warning("rejected message on %s: %s", message.topic, exc)
            if self.on_rejected:
                try:
                    self.on_rejected(message.topic, message.payload, exc,
                                     bool(getattr(message, "retain", False)), getattr(message, "qos", 0))
                except Exception:
                    self.last_error = "Could not persist rejected MQTT message"
                    log.exception(self.last_error)
            return
        try:
            self.handle(replace(msg, retained=bool(getattr(message, "retain", False)),
                                qos=getattr(message, "qos", 0), duplicate=bool(getattr(message, "dup", False))))
            self.last_error = ""
        except Exception:  # a handler bug must not take the subscriber down
            self.last_error = "Message persistence/handler failed; check subscriber logs"
            log.exception("handler failed for %s", message.topic)

    # --- lifecycle ---

    def connect(self) -> None:
        """Start connecting. The actual connect happens in the network loop."""
        self.client.reconnect_delay_set(self.config["RECONNECT_MIN_S"], self.config["RECONNECT_MAX_S"])
        log.info("connecting to %s:%d as %s", self.config["HOST"], self.config["PORT"], self.config["CLIENT_ID"])
        self.client.connect_async(self.config["HOST"], self.config["PORT"], keepalive=self.config["KEEPALIVE_S"])

    def run_forever(self) -> None:
        """Block until stop() is called. paho reconnects on its own, with back-off
        between RECONNECT_MIN_S and RECONNECT_MAX_S, including when the broker
        is not up yet at start."""
        self.connect()
        self.client.loop_forever(retry_first_connection=True)
        log.info("subscriber stopped (received %d, rejected %d)", self.received, self.rejected)

    def stop(self) -> None:
        """Graceful shutdown: disconnect cleanly and let run_forever() return."""
        if not self._stopping:
            self._stopping = True
            log.info("shutting down")
            self.client.disconnect()
