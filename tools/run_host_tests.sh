#!/usr/bin/env bash
# Run every test that doesn't need hardware:
#   0. the safety rules table vs docs/safety_rules.md (tools/safety_rules.py check),
#   1. firmware unit tests (firmware/testing/unit_tests) built for ESP-IDF's linux target,
#   2. Django backend tests (the broker round trip runs only if Mosquitto is up).
#
# Needs: ESP-IDF v6.1 environment (. $IDF_PATH/export.sh), libbsd-dev on Linux,
#        Python with django/requirements.txt installed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "== safety rules doc vs code =="
python3 "$ROOT/tools/safety_rules.py" check

echo "== firmware unit tests (linux target) =="
cd "$ROOT/firmware/testing/unit_tests"
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig --preview set-target linux >/dev/null
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig build >/dev/null
./build_linux/rb4107_unit_tests.elf | tail -3

echo "== Django backend tests =="
cd "$ROOT/django"
python3 manage.py test ingest
