#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CAL_CC=${CC:-cc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
"$CAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_calendar.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/calendar"
"$CAL_CC" -std=c11 -O2 -Wall -Wextra -Werror -DAM_CALENDAR_ONLY -I"$ROOT/core" \
  "$ROOT/tests/test_calendar_values.c" "$ROOT/core/ariannamethod.c" -lm -o "$WORK/calendar-only"
"$WORK/calendar-only"
"$CAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_calendar_values.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/calendar-full"
"$WORK/calendar-full"
JANUS_CALENDAR_REFERENCE="$WORK/calendar" node --experimental-vm-modules --test "$ROOT/tests/calendar_wasm.test.mjs"
