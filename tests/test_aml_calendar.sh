#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CAL_CC=${CC:-cc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
"$CAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_calendar.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/calendar"
"$WORK/calendar"
if [ "$(uname -s)" = Linux ]; then
  "$CAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_calendar_alloc.c" "$ROOT/libaml.a" -lm -lpthread \
    -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$WORK/calendar-alloc"
  "$WORK/calendar-alloc"
fi
node "$ROOT/tests/calendar_reference.mjs" "$WORK/calendar" "$ROOT/janus/calendar.aml" "$WORK/values.aml"
printf 'JANUS_CALENDAR_OK\n' > "$WORK/expected"
for mode in 0 1 2; do
  (cd "$WORK/elsewhere" && "$WORK/calendar" --run "$mode" "$WORK/values.aml") > "$WORK/mode-$mode"
  diff -u "$WORK/expected" "$WORK/mode-$mode"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/compiled" > "$WORK/build.out" 2> "$WORK/build.err"
for zone in UTC0 JST-9 'PST8PDT,M3.2.0/2,M11.1.0/2'; do
  (cd "$WORK/elsewhere" && TZ="$zone" "$ROOT/runner/aml" "$WORK/values.aml") > "$WORK/runner"
  diff -u "$WORK/expected" "$WORK/runner"
  (cd "$WORK/elsewhere" && TZ="$zone" "$WORK/compiled") > "$WORK/compiled.out"
  diff -u "$WORK/expected" "$WORK/compiled.out"
done
printf 'held = calendar_date(0.5)\nPRINT "WRONG"\n' > "$WORK/bad.aml"
if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad.out" 2> "$WORK/bad.err"; then exit 1; fi
test ! -s "$WORK/bad.out"
"$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
if "$WORK/bad" > "$WORK/bad.out" 2> "$WORK/bad.err"; then exit 1; fi
test ! -s "$WORK/bad.out"
node --test "$ROOT/tests/calendar_adapter.test.mjs"
echo 'PASS JANUS.CALENDAR: C, AML interpreter/prepared/bytecode, compiled AML, native and HTTP adapters'
