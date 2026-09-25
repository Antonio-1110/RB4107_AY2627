#!/usr/bin/env bash
# Check that something is listening on the MQTT port (default 1883), and on which address.
set -uo pipefail
PORT="${1:-1883}"
if command -v lsof >/dev/null 2>&1; then
    if lsof -nP -iTCP:"$PORT" -sTCP:LISTEN; then
        exit 0
    fi
elif command -v ss >/dev/null 2>&1; then
    if ss -ltnp | grep -E ":$PORT\b"; then
        exit 0
    fi
fi
echo "nothing is listening on TCP port $PORT" >&2
exit 1
