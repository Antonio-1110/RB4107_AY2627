# Telegram alerts

When the buzzer starts (WARNING) or the cooker is shut down (SHUTDOWN), the
Django MQTT subscriber sends a Telegram message to the stall's staff, for example:

```text
⚠️ Cooker SHUT DOWN
Stall: Controller 01 — test bench (controller_01)
Unattended for: 300 s
Reason: shutdown timeout
Check the stall, then press Reset shutdown on the dashboard.
[ ✅ Acknowledge alert ]
```

Anyone in the chat can press **Acknowledge alert** to say they are dealing with
it. The message then changes, in every alert chat, to end with
`✅ Acknowledged by Antonio (@antonio) at 14:32`, the button disappears, and
Django records who pressed it and when. Only the first press counts; a later one
shows "Already acknowledged by …".

It is free, needs no new Python package, and changes nothing on the boards.
Alerts are a convenience only. They go through the Mac and the internet, so
the buzzer and the shutdown still happen on the controller when either is down.
Telegram rather than SMS because SMS to Singapore phones needs a sender ID
registered with SGNIC (a company UEN, S$500 plus S$200 a year), or it arrives
labelled "Likely-SCAM"; see
[demo_vs_deployment.md](demo_vs_deployment.md#operations-and-testing).

## Set it up (about 10 minutes)

1. In Telegram, open **@BotFather**, send `/newbot`, choose a name and a
   username ending in `bot`. BotFather replies with a **token** like
   `123456789:AA...`. Anyone with it can send as the bot, so keep it out of Git.
2. Make a group (e.g. "RB4107 stall alerts"), add the staff and the bot, then
   write any message in the group. (For alerts to one person only, that person
   sends the bot a message instead.)
3. Copy `django/.env.example` to `django/.env` if you don't have one yet, and set
   `RB4107_TELEGRAM_BOT_TOKEN=<the token>`. `.env` is ignored by Git.
4. Find the chat ID, from `django/` with the virtualenv active, while the
   MQTT subscriber is stopped (`tools/stop_dev.sh`; see
   [the button](#the-acknowledge-button) for why):
   ```bash
   python manage.py telegram_alerts --find-chats
   ```
   It prints one line per chat, e.g. `-1001234567890	RB4107 stall alerts`.
   Group IDs start with `-`. Put it in `.env`:
   `RB4107_TELEGRAM_CHAT_IDS=-1001234567890` (comma-separate several).
5. Check it: `python manage.py telegram_alerts --test` sends a test message.
6. Restart `tools/run_dev.sh` (or the `mqtt_subscriber`) so it reads `.env`.

`python manage.py telegram_alerts` on its own shows the bot name, chats and
events, and the last 10 alerts with who acknowledged each one and how long it
took (or `NOT acknowledged`).

## Settings (`django/.env`)

| Variable | Default | |
|---|---|---|
| `RB4107_TELEGRAM_BOT_TOKEN` | empty | from @BotFather. Alerts are off while it is empty |
| `RB4107_TELEGRAM_CHAT_IDS` | empty | comma-separated chat IDs. Alerts are off while it is empty |
| `RB4107_ALERT_EVENTS` | `warning,shutdown` | any of `unattended` (timer started), `warning` (buzzer), `shutdown`, `fault` (controller FAULT) |
| `RB4107_ALERT_MAX_AGE_S` | `600` | skip an event older than this. The broker holds events while the subscriber is down and delivers them when it comes back; this stops a burst of stale alerts. Only works when the controller has the time (SNTP). `0` sends everything |

`unattended` is off by default because the timer starts 1 s after the radars
stop seeing anyone (the current default), so it would fire many times an hour.

## How it works

`ingest/alerts.py`, called from `handle_event()` in `ingest/handlers.py` for
every controller event. The controller publishes one event per transition
([mqtt_schema.md](mqtt_schema.md)), so each escalation sends one message, not
one per telemetry packet. Retained messages are never re-sent. The message is
sent from a background thread with a 10 s timeout, so a slow network never
holds up the MQTT subscriber; a failure is logged as `[ALERTS][WARNING]` and
the other chats still get it. The stall name comes from the location
catalogue (`RB4107_LOCATION_CATALOG_FILE`).

### The Acknowledge button

The MQTT subscriber runs one more thread while alerts are on
(`alerts.poll_acks()`). It asks Telegram for button presses with long polling:
each request waits up to 25 s for a press, so the laptop needs no public
address or webhook. When a press arrives it checks that it came from a chat in
`RB4107_TELEGRAM_CHAT_IDS` and is for an alert in this database, records it in
the `TelegramAlert` table (`ingest/models.py`; one row per alert, never pruned),
and edits every copy of the message. If the internet is down it logs
`[ALERTS][WARNING] Telegram button poll failed` and tries again every 30 s;
presses made meanwhile are kept by Telegram for 24 hours and handled when it
is back.

Things to know:

- Only one program may ask a bot for updates. Run one MQTT subscriber per bot
  token, or both log `Conflict: terminated by other getUpdates request`.
- The poller asks for button presses only, so while it runs Telegram drops
  ordinary chat messages and `--find-chats` sees nothing. Stop the subscriber,
  run `--find-chats` once, send a message in the group and run it again.
- An acknowledgement is a record that someone saw the alert, nothing more. It
  does not reset the controller; a shutdown still needs Reset shutdown on the
  dashboard.
- Alerts sent before this feature (or by `--test`) have no button.

Telegram's limits (about 1 message a second per chat, 20 a minute per group)
are far above what a stall produces.
