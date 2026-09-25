#!/usr/bin/env bash
# Start Mosquitto in the foreground with the RB4107 config.
set -euo pipefail
cd "$(dirname "$0")"

MOSQUITTO="$(command -v mosquitto || true)"
for candidate in /opt/homebrew/sbin/mosquitto /usr/local/sbin/mosquitto /usr/sbin/mosquitto; do
    [[ -z "$MOSQUITTO" && -x "$candidate" ]] && MOSQUITTO="$candidate"
done
if [[ -z "$MOSQUITTO" ]]; then
    echo "mosquitto not found. On macOS: brew install mosquitto" >&2
    exit 1
fi

mkdir -p /tmp/rb4107_mosquitto
echo "Broker LAN address: $(./lan_ip.sh 2>/dev/null || echo unknown):1883"
exec "$MOSQUITTO" -c "$(pwd)/mosquitto.conf"
