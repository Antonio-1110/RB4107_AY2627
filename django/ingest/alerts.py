"""Telegram alerts to stall staff when the controller escalates.

The controller already publishes an event on every escalation (docs/mqtt_schema.md);
this turns the ones listed in RB4107_ALERT_EVENTS into a Telegram message. Off
until RB4107_TELEGRAM_BOT_TOKEN and RB4107_TELEGRAM_CHAT_IDS are set
(docs/telegram_alerts.md). A convenience only: the buzzer and the shutdown happen
on the controller whether or not a message gets out.

Each alert carries an "Acknowledge" button. poll_acks(), run by the MQTT
subscriber in its own thread, asks Telegram for button presses (long polling, so
the Mac needs no public address), records who pressed it in TelegramAlert and
edits the message in every chat to say so.
"""
from __future__ import annotations

import json
import logging
import re
import threading
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone

from django.conf import settings
from django.db import close_old_connections, connections
from django.utils import timezone as dj_timezone

from .locations import location_for
from .models import TelegramAlert
from .validation import Message

log = logging.getLogger("rb4107.alerts")

API_URL = "https://api.telegram.org/bot{token}/{method}"
TIMEOUT_S = 10
POLL_S = 25    # long-poll wait for button presses; Telegram answers at once when one arrives
RETRY_S = 30   # wait after a failed poll (no internet, bad token, another poller)
ACK_DATA = re.compile(r"ack:(\d+)")

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


def call(method: str, params: dict | None = None, timeout: float = TIMEOUT_S) -> dict:
    """One Bot API call. Raises on a network error or a Telegram error."""
    url = API_URL.format(token=settings.RB4107_TELEGRAM_BOT_TOKEN, method=method)
    # The Bot API takes objects and lists (reply_markup, allowed_updates) as JSON text.
    fields = {key: json.dumps(value) if isinstance(value, (dict, list)) else value
              for key, value in (params or {}).items() if value is not None}
    body = urllib.parse.urlencode(fields).encode()
    try:
        with urllib.request.urlopen(urllib.request.Request(url, data=body), timeout=timeout) as response:
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


def ack_button(alert_id: int) -> dict:
    return {"inline_keyboard": [[{"text": "✅ Acknowledge alert", "callback_data": f"ack:{alert_id}"}]]}


def send(text: str, alert_id: int | None = None) -> int:
    """Send to every configured chat; returns how many got it. Never raises.
    With alert_id, adds the Acknowledge button and stores where each copy went."""
    sent, messages = 0, []
    for chat_id in settings.RB4107_TELEGRAM_CHAT_IDS:
        params = {"chat_id": chat_id, "text": text}
        if alert_id is not None:
            params["reply_markup"] = ack_button(alert_id)
        try:
            reply = call("sendMessage", params)
            sent += 1
            message_id = (reply.get("result") or {}).get("message_id")
            if isinstance(message_id, int):
                messages.append([chat_id, message_id])
        except (OSError, RuntimeError, ValueError) as error:
            # str(error) never holds the token: urllib errors name the host only.
            log.warning("Telegram alert to chat %s failed: %s", chat_id, error)
    if alert_id is not None and messages:
        try:
            TelegramAlert.objects.filter(pk=alert_id).update(messages=messages)
        except Exception:
            log.exception("could not store the sent Telegram messages")
        finally:
            connections.close_all()  # this thread's own connection
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
    alert = TelegramAlert.objects.create(controller_id=msg.data["controller_id"], kind=kind, text=text,
                                         sent_at=dj_timezone.now())
    log.info("%s %s alert %d -> Telegram", msg.data["controller_id"], kind, alert.pk)
    threading.Thread(target=send, args=(text, alert.pk), name="telegram-alert", daemon=True).start()


# --- Acknowledge button -------------------------------------------------------

def _person(user: dict) -> str:
    name = " ".join(filter(None, (user.get("first_name"), user.get("last_name")))) or "someone"
    return f"{name} (@{user['username']})" if user.get("username") else name


def acked_text(alert: TelegramAlert) -> str:
    at = dj_timezone.localtime(alert.acked_at).strftime("%H:%M")
    return f"{alert.text}\n\n✅ Acknowledged by {alert.acked_by} at {at}"


def handle_press(query: dict) -> None:
    """One press of an Acknowledge button (a Telegram callback_query)."""
    message = query.get("message") or {}
    chat_id = str((message.get("chat") or {}).get("id", ""))
    match = ACK_DATA.fullmatch(query.get("data") or "")
    alert = TelegramAlert.objects.filter(pk=int(match.group(1))).first() if match else None
    if chat_id not in settings.RB4107_TELEGRAM_CHAT_IDS or alert is None:
        # A chat no longer in RB4107_TELEGRAM_CHAT_IDS, or an alert from another database.
        _answer(query, "This alert is not known to the RB4107 system.")
        return
    who = _person(query.get("from") or {})
    # Only the first press counts, even if two people press at the same moment.
    first = TelegramAlert.objects.filter(pk=alert.pk, acked_at__isnull=True).update(
        acked_at=dj_timezone.now(), acked_by=who)
    alert.refresh_from_db()
    if not first:
        _answer(query, f"Already acknowledged by {alert.acked_by}.")
        return
    log.info("%s %s alert %d acknowledged by %s after %d s", alert.controller_id, alert.kind, alert.pk, who,
             (alert.acked_at - alert.sent_at).total_seconds())
    _answer(query, "Acknowledged. Thank you.")
    copies = {(str(c), m) for c, m in alert.messages}
    if message.get("message_id") is not None:
        copies.add((chat_id, message["message_id"]))
    text = acked_text(alert)
    for copy_chat, message_id in copies:
        try:  # no reply_markup: the button goes away
            call("editMessageText", {"chat_id": copy_chat, "message_id": message_id, "text": text})
        except (OSError, RuntimeError, ValueError) as error:
            log.warning("could not mark alert %d acknowledged in chat %s: %s", alert.pk, copy_chat, error)


def _answer(query: dict, text: str) -> None:
    """Stops the button's spinner and shows `text` briefly to whoever pressed it."""
    try:
        call("answerCallbackQuery", {"callback_query_id": query.get("id"), "text": text})
    except (OSError, RuntimeError, ValueError) as error:
        log.warning("answerCallbackQuery failed: %s", error)


def poll_acks(stop: threading.Event) -> None:
    """Fetch button presses until `stop` is set. Run in a daemon thread (a poll
    can take POLL_S seconds to return). Only one process may poll a bot."""
    offset = None
    while not stop.is_set():
        try:
            reply = call("getUpdates", {"offset": offset, "timeout": POLL_S,
                                        "allowed_updates": ["callback_query"]}, timeout=POLL_S + TIMEOUT_S)
        except (OSError, RuntimeError, ValueError) as error:
            log.warning("Telegram button poll failed, retrying in %d s: %s", RETRY_S, error)
            stop.wait(RETRY_S)
            continue
        for update in reply.get("result", []):
            offset = update["update_id"] + 1  # confirms it, so Telegram doesn't send it again
            if "callback_query" in update:
                try:
                    handle_press(update["callback_query"])
                except Exception:
                    log.exception("Telegram button press failed")
                finally:
                    close_old_connections()
