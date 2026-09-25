#!/usr/bin/env bash
# Publish/subscribe round trip through the broker.
#
#   ./test_pubsub.sh                 # local test against localhost
#   ./test_pubsub.sh 192.168.1.50    # from another LAN device, against the MacBook
set -euo pipefail
HOST="${1:-localhost}"
PORT="${2:-1883}"
TOPIC="rb4107/test/$$"
MESSAGE="rb4107 pubsub test $(date +%s)"

received="$(mktemp)"
trap 'rm -f "$received"; kill "$sub_pid" 2>/dev/null || true' EXIT

mosquitto_sub -h "$HOST" -p "$PORT" -t "$TOPIC" -C 1 -W 5 > "$received" &
sub_pid=$!
sleep 0.5
mosquitto_pub -h "$HOST" -p "$PORT" -t "$TOPIC" -m "$MESSAGE" -q 1
wait "$sub_pid" || true

if grep -qx "$MESSAGE" "$received"; then
    echo "PASS: $HOST:$PORT delivered '$MESSAGE' on $TOPIC"
else
    echo "FAIL: nothing received from $HOST:$PORT on $TOPIC" >&2
    exit 1
fi
