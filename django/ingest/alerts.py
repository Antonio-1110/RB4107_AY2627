"""Telegram alerts to stall staff when the controller escalates.

The controller already publishes an event on every escalation (docs/mqtt_schema.md);
this turns the ones listed in RB4107_ALERT_EVENTS into a Telegram message. Off
until RB4107_TELEGRAM_BOT_TOKEN and RB4107_TELEGRAM_CHAT_IDS are set
(docs/telegram_alerts.md). A convenience only: the buzzer and the shutdown happen
on the controller whether or not a message gets out.
"""
from __future__ import annotations

import json
import logging
import threading
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone

from django.conf import settings

from .locations import location_for
from .validation import Message

log = logging.getLogger("rb4107.alerts")

API_URL = "https://api.telegram.org/bot{token}/{method}"
TIMEOUT_S = 10

# Alert kind -> (does this event match, headline). The kinds are the values
# RB4107_ALERT_EVENTS takes.
KINDS = {
    "unattended": (lambda kind, state: kind == "state_change" and state == "UNATTENDED",
                   "Unattended cooking: the timer has started"),
    "warning": (lambda kind, state: kind == "warning", "Buzzer sounding: nobody at the stove"),
    "shutdown": (lambda kind, state: kind == "shutdown", "Cooker SHUT DOWN"),
    "fault": (lambda kind, state: kind == "state_change" and state == "FAULT", "Controller FAULT"),
}


def enabled() -> bool:
    return bool(settings.RB4107_TELEGRAM_BOT_TOKEN and settings.RB4107_TELEGRAM_CHAT_IDS)


def alert_kind(msg: Message) -> str | None:
    """Which configured alert this event is, or None."""
    if msg.type != "event":
        return None
    state = msg.data["safety"]["state"]
    for name in settings.RB4107_ALERT_EVENTS:
        matches = KINDS.get(name, (None,))[0]
        if matches and matches(msg.data["event"], state):
            return name
    return None


def _too_old(msg: Message) -> bool:
    """A QoS 1 event the broker held while the subscriber was down. Only
    checkable when the controller had the time (timestamp is null before SNTP)."""
    stamp = msg.data.get("timestamp")
    if not stamp or settings.RB4107_ALERT_MAX_AGE_S <= 0:
        return False
    try:
        sent = datetime.fromisoformat(stamp.replace("Z", "+00:00"))
    except ValueError:
        return False
    if sent.tzinfo is None:
        sent = sent.replace(tzinfo=timezone.utc)
    return (datetime.now(timezone.utc) - sent).total_seconds() > settings.RB4107_ALERT_MAX_AGE_S


def alert_text(msg: Message, kind: str) -> str:
    d, safety = msg.data, msg.data["safety"]
    place = location_for(d["controller_id"])
    lines = [f"⚠️ {KINDS[kind][1]}",
             f"Stall: {place['stall_name']} ({d['controller_id']})"]
    if safety["unattended_ms"]:
        lines.append(f"Unattended for: {round(safety['unattended_ms'] / 1000)} s")
    if safety["reason"]:
        lines.append(f"Reason: {safety['reason']}")
    if kind == "shutdown":
        lines.append("Check the stall, then press Reset shutdown on the dashboard.")
    if d.get("timestamp"):
        lines.append(f"Time: {d['timestamp']}")
    return "\n".join(lines)


def call(method: str, params: dict | None = None) -> dict:
    """One Bot API call. Raises on a network error or a Telegram error."""
    url = API_URL.format(token=settings.RB4107_TELEGRAM_BOT_TOKEN, method=method)
    body = urllib.parse.urlencode(params or {}).encode()
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=body), timeout=TIMEOUT_S) as response:
            reply = json.load(response)
    except urllib.error.HTTPError as error:
        # Telegram explains itself in the body (bad token, chat not found, ...).
        try:
            reply = json.load(error)
        except ValueError:
            raise RuntimeError(f"HTTP {error.code}") from None
    if not reply.get("ok"):
        raise RuntimeError(reply.get("description", "Telegram refused the request"))
    return reply


def send(text: str) -> int:
    """Send to every configured chat; returns how many got it. Never raises."""
    sent = 0
    for chat_id in settings.RB4107_TELEGRAM_CHAT_IDS:
        try:
            call("sendMessage", {"chat_id": chat_id, "text": text})
            sent += 1
        except (OSError, RuntimeError, ValueError) as error:
            # str(error) never holds the token: urllib errors name the host only.
            log.warning("Telegram alert to chat %s failed: %s", chat_id, error)
    return sent


def notify(msg: Message) -> None:
    """Called for every event; sends in the background so MQTT never waits."""
    if not enabled() or msg.retained:
        return
    kind = alert_kind(msg)
    if kind is None:
        return
    if _too_old(msg):
        log.info("%s %s alert skipped: event is older than %d s", msg.data["controller_id"], kind,
                 settings.RB4107_ALERT_MAX_AGE_S)
        return
    text = alert_text(msg, kind)
    log.info("%s %s alert -> Telegram", msg.data["controller_id"], kind)
    threading.Thread(target=send, args=(text,), name="telegram-alert", daemon=True).start()
