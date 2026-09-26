#!/usr/bin/env bash
# Run every test that doesn't need hardware:
#   1. firmware unit tests (project 28) built for ESP-IDF's linux target,
#   2. Django backend tests (the broker round trip runs only if Mosquitto is up).
#
# Needs: ESP-IDF v6.1 environment (. $IDF_PATH/export.sh), libbsd-dev on Linux,
#        Python with backend/django/requirements.txt installed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "== firmware unit tests (linux target) =="
cd "$ROOT/firmware/28_state_machine_tests"
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig --preview set-target linux >/dev/null
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig build >/dev/null
./build_linux/rb4107_28_state_machine_tests.elf | tail -3

echo "== Django backend tests =="
cd "$ROOT/backend/django"
python3 manage.py test ingest
