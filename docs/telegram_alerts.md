# Telegram alerts

When the buzzer starts (WARNING) or the cooker is shut down (SHUTDOWN), the
Django MQTT subscriber sends a Telegram message to the stall's staff:

```text
⚠️ Cooker SHUT DOWN
Location: Changi Airport, Terminal 2, Level 3, Public F&B cluster
Stall: Controller 01 — test bench (controller_01)
Unattended for: 300 s
Reason: shutdown timeout
Check the stall, then press Reset shut-off on the dashboard.
Time: 21:01:31, 1 Nov 2026
[ ✅ Acknowledge alert ]
```

Anyone in the chat can press **Acknowledge alert**. The message then changes in
every alert chat to end with who and when, and Django records it. Only the first
press counts. An acknowledgement only records that someone saw the alert: it
does not reset the controller; a shutdown still needs Reset shut-off on the
dashboard.

Alerts are a convenience only. They go through the Mac and the internet, so the
buzzer and the shutdown still happen on the controller when either is down.
Telegram rather than SMS because SMS to Singapore phones needs a sender ID
registered with SGNIC (a company UEN, S$500 plus S$200 a year), or it arrives
labelled "Likely-SCAM"; see
[demo_vs_deployment.md](demo_vs_deployment.md#operations-and-testing).

## Set it up (about 10 minutes)

1. In Telegram, open **@BotFather**, send `/newbot`, choose a name and a
   username ending in `bot`. BotFather replies with a **token**. Anyone with it
   can send as the bot, so keep it out of Git.
2. Make a group, add the staff and the bot, then write any message in it. (For
   alerts to one person only, that person sends the bot a message instead.)
3. From `django/` with the virtualenv active: `python manage.py migrate`
   (`tools/run_dev.sh` also does this). Copy `django/.env.example` to
   `django/.env` if you don't have one, and set
   `RB4107_TELEGRAM_BOT_TOKEN=<the token>`. `.env` is ignored by Git.
4. Find the chat ID, with the MQTT subscriber stopped (`tools/stop_dev.sh`; see
   [Acknowledge button](#the-acknowledge-button) for why):
   ```bash
   python manage.py telegram_alerts --find-chats
   ```
   Group IDs start with `-`. Put it in `.env`:
   `RB4107_TELEGRAM_CHAT_IDS=-1001234567890` (comma-separate several).
5. `python manage.py telegram_alerts --test` sends a test message.
6. Restart `tools/run_dev.sh` (or the `mqtt_subscriber`) so it reads `.env`.

`python manage.py telegram_alerts` alone shows the bot name, chats, events, and
the last 10 alerts with who acknowledged each (or `NOT acknowledged`).

## Try it without hardware

With `tools/run_dev.sh` running and the alerts set up, in a second terminal from
`django/`:

```bash
python manage.py simulate_alert
```

It plays one sped-up incident on `controller_01` (cooking, the person leaves,
buzzer, shutdown, reset) and records it in the stall's history. Options
(`--controller`, `--warning-after`, `--shutdown-after`, `--reset-after`,
`--direct`): `python manage.py simulate_alert --help`. `--direct` needs no
broker but needs the MQTT subscriber stopped (it polls the button itself).

Its messages are marked `"simulation": true`. Don't run it for a stall whose
real controller is online. A stall that `run_dev.sh --demo` already simulates
works for the alerts, but the dashboard will flick between the two.

To run without alerts once (a demo, say) without editing `.env`, start
`tools/run_dev.sh --no-telegram` (or `--demo --no-telegram`).

## Settings (`django/.env`)

| Variable | Notes |
|---|---|
| `RB4107_TELEGRAM_BOT_TOKEN` | from @BotFather. Alerts are off while it is empty |
| `RB4107_TELEGRAM_CHAT_IDS` | comma-separated. Alerts are off while it is empty |
| `RB4107_ALERT_EVENTS` | default `warning,shutdown`; also `unattended` (timer started) and `fault` (controller FAULT) |
| `RB4107_ALERT_MAX_AGE_S` | default `600`: skip an event older than this. The broker holds events while the subscriber is down and delivers them on return; this stops a burst of stale alerts. Only works when the controller has the time (SNTP). `0` sends everything |

`unattended` is off by default because the timer starts 1 s after the radars
stop seeing anyone, so it would fire many times an hour. Defaults:
`django/rb4107_backend/settings.py`.

## How it works

`ingest/alerts.py`, called from `handle_event()` in `ingest/handlers.py` for
every controller event. The controller publishes one event per transition
([mqtt_schema.md](mqtt_schema.md)), so each escalation sends one message, not
one per telemetry packet. Retained messages are never re-sent. The message goes
out from a background thread with a 10 s timeout, so a slow network never holds
up the subscriber; a failure is logged as `[ALERTS][WARNING]` and the other chats
still get it. The Location line comes from the location catalogue
(`RB4107_LOCATION_CATALOG_FILE`: `site_name`, `building`, `level`, `area`,
leaving out placeholders such as "Configure actual building"); fill these in
for each real controller in `django/locations.json`. The Time line is when the
controller says it happened, in `DJANGO_TIME_ZONE` (default `Asia/Singapore`),
or when Django received it if the controller doesn't have the time yet.

### The Acknowledge button

While alerts are on, the subscriber runs one more thread (`alerts.poll_acks()`)
that long-polls Telegram for button presses (each request waits up to 25 s), so
the laptop needs no public address or webhook. A press from a chat in
`RB4107_TELEGRAM_CHAT_IDS`, for an alert in this database, is recorded in the
`TelegramAlert` table (`ingest/models.py`; one row per alert, never pruned) and
every copy of the message is edited. If the internet is down it logs
`[ALERTS][WARNING] Telegram button poll failed` and retries every 30 s; Telegram
keeps presses for 24 hours.

- Only one program may ask a bot for updates. Run one MQTT subscriber per bot
  token, or both log `Conflict: terminated by other getUpdates request`.
- The poller asks for button presses only, so while it runs Telegram drops
  ordinary chat messages and `--find-chats` sees nothing. Stop the subscriber,
  run `--find-chats` once, send a message in the group and run it again.
- Alerts sent by `--test` have no button.

Telegram's limits (about 1 message a second per chat, 20 a minute per group) are
far above what a stall produces.

`manage.py test` never uses the bot: it clears the token while the tests run, whatever `django/.env` holds. `tools/run_dev.sh --no-telegram` does the same for a dev run.
