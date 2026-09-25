"""MQTT subscriber: connects to Mosquitto, subscribes to rb4107/#, validates
and dispatches every message (TODO sections 23-24).

It is not tied to HTTP requests in any way. The persistent process is the
`mqtt_subscriber` management command.
"""

from __future__ import annotations

import logging
import threading
from typing import Callable

import paho.mqtt.client as mqtt

from . import handlers, validation

log = logging.getLogger("rb4107.mqtt")


class Subscriber:
    def __init__(self, config: dict, handle: Callable[[validation.Message], None] = handlers.handle):
        self.config = config
        self.handle = handle
        self.received = 0
        self.rejected = 0
        self.connected = threading.Event()
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
        if reason_code.is_failure:
            log.warning("broker refused connection: %s", reason_code)
            return
        log.info("broker connected (%s:%d, session %s)", self.config["HOST"], self.config["PORT"],
                 "resumed" if flags.session_present else "new")
        # (Re)subscribe on every connect so a reconnect never loses the subscription.
        client.subscribe(self.config["TOPIC"], qos=self.config["QOS"])
        self.connected.set()

    def _on_disconnect(self, client, userdata, flags, reason_code, properties):
        self.connected.clear()
        if self._stopping:
            log.info("disconnected from broker")
        else:
            log.warning("broker disconnected (%s); reconnecting", reason_code)

    def _on_subscribe(self, client, userdata, mid, reason_codes, properties):
        log.info("subscribed to %s (%s)", self.config["TOPIC"], ", ".join(str(rc) for rc in reason_codes))

    def _on_message(self, client, userdata, message):
        self.received += 1
        try:
            msg = validation.parse(message.topic, message.payload)
        except validation.InvalidMessage as exc:
            self.rejected += 1
            log.warning("rejected message on %s: %s", message.topic, exc)
            return
        try:
            self.handle(msg)
        except Exception:  # a handler bug must not take the subscriber down
            log.exception("handler failed for %s", message.topic)

    # --- lifecycle ---

    def connect(self) -> None:
        """Start connecting. The actual connect happens in the network loop."""
        self.client.reconnect_delay_set(self.config["RECONNECT_MIN_S"], self.config["RECONNECT_MAX_S"])
        log.info("connecting to %s:%d as %s", self.config["HOST"], self.config["PORT"], self.config["CLIENT_ID"])
        self.client.connect_async(self.config["HOST"], self.config["PORT"], keepalive=self.config["KEEPALIVE_S"])
