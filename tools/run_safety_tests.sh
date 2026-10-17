#!/usr/bin/env bash
# Build and run only the safety state machine tests on this computer, in a second or two:
# plain cc with ESP-IDF's copy of Unity, no idf.py build. Use it after changing
# firmware/components/safety or safety_rules.def; tools/run_host_tests.sh runs everything.
#
# Needs: a C compiler, and IDF_PATH set (. $IDF_PATH/export.sh) or UNITY_SRC pointing
# at a folder with unity.c / unity.h / unity_internals.h.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UNITY_SRC="${UNITY_SRC:-${IDF_PATH:-}/components/unity/unity/src}"
if [ ! -f "$UNITY_SRC/unity.c" ]; then
    echo "Unity not found: set IDF_PATH (. \$IDF_PATH/export.sh) or UNITY_SRC" >&2
    exit 1
fi
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
cat > "$OUT/main.c" <<'C'
#include "unity.h"
void run_safety_tests(void);
void setUp(void) {}
void tearDown(void) {}
int main(void) { UNITY_BEGIN(); run_safety_tests(); return UNITY_END(); }
C
cc -std=gnu11 -O1 -Wall -DUNITY_INCLUDE_DOUBLE \
    -I"$UNITY_SRC" -I"$ROOT/firmware/components/safety/include" -I"$ROOT/firmware/components/rb_types/include" \
    -I"$ROOT/firmware/testing/unit_tests/main" \
    "$ROOT/firmware/components/safety/src/safety.c" "$ROOT/firmware/testing/unit_tests/main/test_safety.c" \
    "$UNITY_SRC/unity.c" "$OUT/main.c" -lm -o "$OUT/safety_tests"
"$OUT/safety_tests" | tail -3
