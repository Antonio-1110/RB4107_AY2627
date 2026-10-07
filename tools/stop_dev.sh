#!/usr/bin/env bash
# Stop the host stack started by tools/run_dev.sh (broker, MQTT subscriber,
# simulator with --demo, Django dev server), e.g. from another terminal or
# after closing the one it ran in.
#
#   1. Ask run_dev.sh to shut down, which stops everything the same way Ctrl-C does.
#   2. Then stop anything of this project's still running (run_dev.sh killed
#      with -9, or a leftover from a crash): manage.py processes whose working
#      directory is this project's django/, and Mosquitto started with
#      tools/mqtt/mosquitto.conf. Other projects' servers are left alone.
#   3. Delete the database (django/db.sqlite3, demo.sqlite3), as run_dev.sh
#      does when it stops: no data is kept between runs.
#
# Usage:   tools/stop_dev.sh
# Written for the bash 3.2 that ships with macOS.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DJANGO_DIR="$ROOT/django"
BROKER_CONF="$ROOT/tools/mqtt/mosquitto.conf"
PID_FILE="${TMPDIR:-/tmp}/rb4107_dev.pid" # written by tools/run_dev.sh

alive() {
    kill -0 "$1" 2>/dev/null
}

# wait_gone <seconds> <pid...>: true once none of the pids is running.
wait_gone() {
    local tries=$(($1 * 4)) pid any
    shift
    while ((tries-- > 0)); do
        any=0
        for pid in "$@"; do alive "$pid" && any=1; done
        [[ $any == 0 ]] && return 0
        sleep 0.25
    done
    return 1
}

# --- 1. run_dev.sh ---------------------------------------------------------------
STOPPED_DEV=0
if [[ -f "$PID_FILE" ]]; then
    DEV_PID="$(cat "$PID_FILE")"
    if alive "$DEV_PID"; then
        STOPPED_DEV=1
        echo "[stop] stopping run_dev.sh (pid $DEV_PID)..."
        kill -TERM "$DEV_PID"
        # run_dev.sh gives its processes up to 5 s before forcing them.
        if ! wait_gone 10 "$DEV_PID"; then
            echo "[stop] run_dev.sh did not exit, killing it"
            kill -KILL "$DEV_PID" 2>/dev/null
        fi
    fi
    rm -f "$PID_FILE"
fi

# --- 2. Leftovers ----------------------------------------------------------------
# Working directory of a process ("" if it can't be read).
cwd_of() {
    lsof -a -p "$1" -d cwd -Fn 2>/dev/null | sed -n 's/^n//p'
}

LEFT=""
for pid in $(pgrep -f "manage.py (mqtt_subscriber|simulate_fleet|runserver)"); do
    [[ "$(cwd_of "$pid")" == "$DJANGO_DIR" ]] && LEFT="$LEFT $pid"
done
for pid in $(pgrep -f "mosquitto -c $BROKER_CONF"); do
    LEFT="$LEFT $pid"
done

if [[ -n "$LEFT" ]]; then
    echo "[stop] stopping leftover processes:"
    ps -o pid=,command= -p "$(echo $LEFT | tr ' ' ',')" | sed 's/^/[stop]   /'
    kill -TERM $LEFT 2>/dev/null
    if ! wait_gone 5 $LEFT; then
        echo "[stop] some did not exit, killing them"
        kill -KILL $LEFT 2>/dev/null
    fi
fi

# --- 3. Data, once nothing is using it -------------------------------------------
STILL=""
for pid in $LEFT; do alive "$pid" && STILL="$STILL $pid"; done
if [[ -n "$STILL" ]]; then
    echo "[stop] could not stop:$STILL (data kept)" >&2
    exit 1
fi
rm -f "$DJANGO_DIR"/db.sqlite3{,-wal,-shm} "$DJANGO_DIR"/demo.sqlite3{,-wal,-shm}
if [[ $STOPPED_DEV == 0 && -z "$LEFT" ]]; then
    echo "[stop] nothing running, data erased"
else
    echo "[stop] all stopped, data erased"
fi
