#!/usr/bin/env bash
# Print everything the RB4107 system publishes (topic + payload).
HOST="${1:-localhost}"
exec mosquitto_sub -h "$HOST" -p "${2:-1883}" -t 'rb4107/#' -v
