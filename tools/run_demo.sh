#!/usr/bin/env bash
# Start the dashboard with made-up stalls, for demos without hardware:
# simulated controllers (python manage.py simulate_fleet --direct) and the
# Django dev server, with django/locations.demo.json and a separate database
# (django/demo.sqlite3). No broker, so a real controller is not shown.
#
# For the real controller use tools/run_dev.sh. Only one of the two can run at
# a time (they share port 8000). Ctrl-C or tools/stop_dev.sh stops it.
# Takes the same options as run_dev.sh (RB4107_HTTP_ADDR, RB4107_NO_BROWSER).
RB4107_DEMO=1 exec "$(dirname "$0")/run_dev.sh" "$@"
