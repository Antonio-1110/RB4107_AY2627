#!/usr/bin/env bash
# Print this machine's LAN IPv4 address: the value for RB_BROKER_HOST on the S3.
set -euo pipefail
if command -v ipconfig >/dev/null 2>&1 && [[ "$(uname)" == "Darwin" ]]; then
    for iface in en0 en1 en2 en3 en4 en5; do
        ip="$(ipconfig getifaddr "$iface" 2>/dev/null || true)"
        if [[ -n "$ip" ]]; then
            echo "$ip"
            exit 0
        fi
    done
else
    ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
    if [[ -n "$ip" ]]; then
        echo "$ip"
        exit 0
    fi
fi
echo "no LAN address found" >&2
exit 1
