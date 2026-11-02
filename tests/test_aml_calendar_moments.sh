#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CAL_CC=${CC:-cc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/prefix/lib" "$WORK/run"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
"$CAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_calendar_moments.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/moments"
"$WORK/moments"
"$CAL_CC" -std=c11 -O2 -Wall -Wextra -Werror -DAM_CALENDAR_ONLY -I"$ROOT/core" \
 "$ROOT/tests/test_calendar_moments.c" "$ROOT/core/ariannamethod.c" -lm -o "$WORK/pure"
"$WORK/pure"
node "$ROOT/tests/calendar_moments_reference.mjs" "$ROOT/janus/calendar.aml" "$WORK/moments.aml"
printf 'MOMENTS_OK\n' > "$WORK/expected"
"$ROOT/tools/amlc" "$WORK/moments.aml" --scalar -o "$WORK/compiled" > "$WORK/build.out" 2> "$WORK/build.err"
for zone in UTC0 JST-9 'PST8PDT,M3.2.0/2,M11.1.0/2';do
 for mode in 0 1 2;do
  (cd "$WORK/run" && TZ="$zone" "$WORK/moments" --run "$mode" "$WORK/moments.aml") > "$WORK/mode-$mode"
  diff -u "$WORK/expected" "$WORK/mode-$mode"
 done
 (cd "$WORK/run" && TZ="$zone" "$ROOT/runner/aml" "$WORK/moments.aml") > "$WORK/runner"
 diff -u "$WORK/expected" "$WORK/runner"
 (cd "$WORK/run" && TZ="$zone" "$WORK/compiled") > "$WORK/compiled.out"
 diff -u "$WORK/expected" "$WORK/compiled.out"
done
printf 'a=calendar_instant(0,0,0)\nx=calendar_compare(a,17)\nPRINT "WRONG"\n' > "$WORK/bad.aml"
"$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
if "$WORK/bad" > "$WORK/bad.out" 2> "$WORK/bad.err";then exit 1;fi
test ! -s "$WORK/bad.out"
node --test "$ROOT/tests/calendar_moments.test.mjs"
echo 'PASS exact calendar moments through C, AML, compiled AML, WASM, native and HTTP transports'
