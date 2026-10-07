#!/usr/bin/env bash
# Start the whole RB4107 host stack for local development and demos:
#   1. Mosquitto broker   (tools/mqtt/start_broker.sh)
#   2. MQTT subscriber    (python manage.py mqtt_subscriber)
#   3. Django dev server  (python manage.py runserver, dashboard at /)
#
# With RB4107_DEMO=1 (what tools/run_demo.sh sets) it runs made-up stalls
# instead, with no hardware or broker: simulate_fleet --direct replaces the
# broker and subscriber, using django/locations.demo.json and its own
# database (django/demo.sqlite3), so demo data never mixes with real data.
#
# Ctrl-C (or tools/stop_dev.sh from another terminal) stops everything. If any
# one of them exits, the others are stopped too.
# This is a dev helper, not a deployment method.
#
# Usage:   tools/run_dev.sh
# Options (environment variables):
#   RB4107_HTTP_ADDR=0.0.0.0:8000     address for runserver (127.0.0.1:8000 = this Mac only)
#   RB4107_SKIP_BROKER=1              don't start Mosquitto (use one already running)
#   RB4107_NO_BROWSER=1               don't open the dashboard in the browser
#
# Uses django/.venv if it exists. Written for the bash 3.2 that
# ships with macOS.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DJANGO_DIR="$ROOT/django"
# Reachable from the LAN: other machines open http://<this Mac's IP>:8000/.
# Their address must be in DJANGO_ALLOWED_HOSTS (django/.env).
HTTP_ADDR="${RB4107_HTTP_ADDR:-0.0.0.0:8000}"
MQTT_PORT=1883
PID_FILE="${TMPDIR:-/tmp}/rb4107_dev.pid" # read by tools/stop_dev.sh
DEMO="${RB4107_DEMO:-0}"
export PYTHONUNBUFFERED=1
if [[ "$DEMO" == 1 ]]; then
    export RB4107_LOCATION_CATALOG_FILE=locations.demo.json
    export RB4107_SQLITE_PATH="$DJANGO_DIR/demo.sqlite3"
fi

if [[ -f "$PID_FILE" ]] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
    echo "[dev] already running (pid $(cat "$PID_FILE")); stop it with tools/stop_dev.sh" >&2
    exit 1
fi

# --- Python: prefer the project's virtualenv ---------------------------------
if [[ -x "$DJANGO_DIR/.venv/bin/python" ]]; then
    PYTHON="$DJANGO_DIR/.venv/bin/python"
else
    PYTHON="$(command -v python3 || true)"
    echo "[dev] no django/.venv found, using ${PYTHON:-nothing}" >&2
fi
if [[ -z "$PYTHON" ]] || ! "$PYTHON" -c "import django, paho.mqtt, jsonschema" 2>/dev/null; then
    echo "[dev] Django requirements are missing. Set up the venv first:" >&2
    echo "      cd django && python3 -m venv .venv && source .venv/bin/activate && pip install -r requirements.txt" >&2
    exit 1
fi
if [[ ! -f "$DJANGO_DIR/.env" ]]; then
    echo "[dev] note: django/.env not found, using defaults (cp .env.example .env to customise)"
fi

port_open() {
    "$PYTHON" -c "import socket,sys; s=socket.socket(); s.settimeout(0.5); sys.exit(s.connect_ex(('127.0.0.1', $1)))" 2>/dev/null
}

# --- Process bookkeeping -----------------------------------------------------
# Job control gives every background job its own process group, so Ctrl-C
# reaches only this script, and cleanup can stop each job with its children
# (runserver's autoreloader starts a second Python process).
set -m
PIDS=""
NAMES=""

# prefix <label>: copy stdin to stdout line by line, tagging each line.
# (A plain read loop, because some awk/sed builds buffer piped input.)
prefix() {
    local line
    # Keep running when the job is signalled, so its last log lines still get
    # printed; the loop ends by itself once the command closes its output.
    trap '' INT TERM HUP
    while IFS= read -r line || [[ -n "$line" ]]; do
        printf '[%s] %s\n' "$1" "$line"
    done
}

# start <label> <command...>: run in the background, prefixing each output line.
start() {
    local label="$1"; shift
    "$@" > >(prefix "$label") 2>&1 &
    PIDS="$PIDS $!"
    NAMES="$NAMES $label"
}

STOPPING=0
cleanup() {
    [[ $STOPPING == 1 ]] && return
    STOPPING=1
    trap '' INT TERM
    echo
    echo "[dev] stopping..."
    local pid
    for pid in $PIDS; do kill -TERM -- "-$pid" 2>/dev/null; done
    # Give them a few seconds to shut down cleanly, then force it.
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        local alive=0
        for pid in $PIDS; do kill -0 "$pid" 2>/dev/null && alive=1; done
        [[ $alive == 0 ]] && break
        sleep 0.5
    done
    for pid in $PIDS; do kill -KILL -- "-$pid" 2>/dev/null; done
    wait 2>/dev/null
    rm -f "$PID_FILE"
    echo "[dev] all stopped"
}
trap 'cleanup; exit 0' INT TERM HUP
trap cleanup EXIT
echo $$ > "$PID_FILE"

# --- 1. Broker -----------------------------------------------------------------
if [[ "$DEMO" == 1 ]]; then
    echo "[dev] demo mode: simulated stalls from locations.demo.json, no broker"
elif [[ "${RB4107_SKIP_BROKER:-0}" == 1 ]]; then
    echo "[dev] RB4107_SKIP_BROKER=1, not starting Mosquitto"
elif port_open $MQTT_PORT; then
    echo "[dev] something is already listening on port $MQTT_PORT, using it as the broker"
    echo "[dev] (if that is 'brew services' mosquitto, it only listens on localhost and the S3 can't reach it)"
else
    start broker "$ROOT/tools/mqtt/start_broker.sh"
fi

if [[ "$DEMO" != 1 ]]; then
    echo "[dev] waiting for the broker on port $MQTT_PORT..."
    for _ in $(seq 1 40); do
        port_open $MQTT_PORT && break
        sleep 0.25
    done
    if ! port_open $MQTT_PORT; then
        echo "[dev] broker did not come up on port $MQTT_PORT (see [broker] lines above)" >&2
        exit 1
    fi
    echo "[dev] broker is up"
fi

# --- 2. Database, subscriber and web server ----------------------------------
cd "$DJANGO_DIR"
if ! "$PYTHON" manage.py migrate --noinput >/dev/null; then
    echo "[dev] 'manage.py migrate' failed" >&2
    exit 1
fi

if [[ "$DEMO" == 1 ]]; then
    start simulator "$PYTHON" manage.py simulate_fleet --direct
else
    start subscriber "$PYTHON" manage.py mqtt_subscriber
fi
start web "$PYTHON" manage.py runserver "$HTTP_ADDR"

# The dashboard (frontend/) is served by runserver at /, so there is no
# separate frontend process. Open it once the web server answers.
URL="http://127.0.0.1:${HTTP_ADDR##*:}/"
for _ in $(seq 1 60); do
    port_open "${HTTP_ADDR##*:}" && break
    sleep 0.25
done
echo "[dev] dashboard: $URL   (Ctrl-C stops everything)"
if [[ "${HTTP_ADDR%:*}" == 0.0.0.0 ]]; then
    echo "[dev] from other machines: http://$("$ROOT/tools/mqtt/lan_ip.sh" 2>/dev/null || echo '<this-mac-ip>'):${HTTP_ADDR##*:}/"
fi
if [[ "${RB4107_NO_BROWSER:-0}" != 1 ]]; then
    if command -v open >/dev/null 2>&1 && [[ "$(uname)" == Darwin ]]; then
        open "$URL"
    elif command -v xdg-open >/dev/null 2>&1; then
        xdg-open "$URL" >/dev/null 2>&1 &
    fi
fi

# --- Wait until Ctrl-C or until one of them exits ------------------------------
while :; do
    set -- $NAMES
    for pid in $PIDS; do
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "[dev] $1 exited, stopping the rest"
            exit 1
        fi
        shift
    done
    sleep 1
done
