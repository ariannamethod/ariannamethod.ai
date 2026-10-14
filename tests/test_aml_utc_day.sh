#!/usr/bin/env bash
# UTC day values through interpreter, prepared, bytecode, CLI and compiled AML.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UTC_CC=${CC:-cc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"

"$UTC_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_utc_day.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/utc-day"
"$WORK/utc-day"
"$WORK/utc-day" --fixture > "$WORK/values.aml"
printf 'UTC_DAY_OK\n' > "$WORK/expected"
for mode in 0 1 2; do
    "$WORK/utc-day" --run "$mode" "$WORK/values.aml" > "$WORK/mode-$mode"
    diff -u "$WORK/expected" "$WORK/mode-$mode"
done
"$ROOT/runner/aml" "$WORK/values.aml" > "$WORK/runner"
diff -u "$WORK/expected" "$WORK/runner"
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/utc compiled" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/utc compiled") > "$WORK/compiled"
diff -u "$WORK/expected" "$WORK/compiled"

bad_count=$("$WORK/utc-day" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/utc-day" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted UTC day fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/utc-day" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-$mode.out" 2> "$WORK/bad-$mode.err"; then
            echo "FAIL: mode $mode accepted UTC day fixture $i"; exit 1
        fi
    done
    cat >> "$WORK/bad.aml" <<'AML'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
AML
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" \
        > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: compiled program accepted UTC day fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-0.out" "$WORK/bad-1.out" "$WORK/bad-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: UTC day fixture $i continued after its error"; exit 1
    fi
done
echo "PASS UTC day: real-clock values and $bad_count refusals agree in all five execution paths"

if [ "$(uname -s)" != Linux ]; then
    echo 'SKIP: deterministic clock boundaries require linker --wrap=time'
    exit 0
fi
"$UTC_CC" -O2 -Wall -Wextra -DAML_UTC_WRAP -I"$ROOT/core" "$ROOT/tests/test_aml_utc_day.c" \
    "$ROOT/libaml.a" -lm -lpthread -Wl,--wrap=time -o "$WORK/utc-wrapped"
"$WORK/utc-wrapped"
"$UTC_CC" -O2 -Wall -Wextra -I"$ROOT/core" -include "$ROOT/tests/fixtures/utc_day_clock.h" \
    "$ROOT/runner/am.c" "$ROOT/libaml.a" -lm -lpthread -Wl,--wrap=time -o "$WORK/runner-wrapped"
cat > "$WORK/boundary.aml" <<'AML'
day = utc_day()
PRINT day
AML
cp "$WORK/boundary.aml" "$WORK/boundary-compiled.aml"
cat >> "$WORK/boundary-compiled.aml" <<AML
BLOOD INCLUDE "$ROOT/tests/fixtures/utc_day_clock.h"
BLOOD LINK -Wl,--wrap=time
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("C_MAIN_OK"); return 0; }
}
AML
"$ROOT/tools/amlc" "$WORK/boundary-compiled.aml" --scalar -o "$WORK/boundary" \
    > "$WORK/boundary-build.out" 2> "$WORK/boundary-build.err"
for zone in UTC0 'PST8PDT,M3.2.0/2,M11.1.0/2' JST-9; do
    export TZ="$zone"
    while read -r seconds day; do
        if ! "$WORK/utc-wrapped" --supports-seconds "$seconds"; then continue; fi
        export AML_TEST_UTC_SECONDS="$seconds"
        printf '%s\n' "$day" > "$WORK/expected"
        for mode in 0 1 2; do
            "$WORK/utc-wrapped" --run "$mode" "$WORK/boundary.aml" > "$WORK/boundary-$mode"
            diff -u "$WORK/expected" "$WORK/boundary-$mode"
        done
        "$WORK/runner-wrapped" "$WORK/boundary.aml" > "$WORK/boundary-runner"
        diff -u "$WORK/expected" "$WORK/boundary-runner"
        printf '%s\nC_MAIN_OK\n' "$day" > "$WORK/expected"
        "$WORK/boundary" > "$WORK/boundary-compiled"
        diff -u "$WORK/expected" "$WORK/boundary-compiled"
    done <<'CASES'
0 0
86399 0
86400 1
1709164799 19781
1709164800 19782
1709251199 19782
1709251200 19783
1727913599 19998
1727913600 19999
1727956800 19999
-2 -1
-86401 -2
CASES
done
for seconds in -1 -1449551462401 1449551548800; do
    if [ "$seconds" != -1 ] && ! "$WORK/utc-wrapped" --supports-seconds "$seconds"; then continue; fi
    export AML_TEST_UTC_SECONDS="$seconds"
    if [ "$seconds" = -1 ]; then diagnostic='could not read the system clock'; else diagnostic='exact scalar day range'; fi
    for mode in 0 1 2; do
        if "$WORK/utc-wrapped" --run "$mode" "$WORK/boundary.aml" > "$WORK/failed-$mode.out" 2> "$WORK/failed-$mode.err"; then
            echo "FAIL: mode $mode accepted clock failure $seconds"; exit 1
        fi
        grep -q "$diagnostic" "$WORK/failed-$mode.err"
        test ! -s "$WORK/failed-$mode.out"
    done
    if "$WORK/runner-wrapped" "$WORK/boundary.aml" > "$WORK/failed-runner.out" 2> "$WORK/failed-runner.err"; then
        echo "FAIL: runner accepted clock failure $seconds"; exit 1
    fi
    grep -q "$diagnostic" "$WORK/failed-runner.err"
    test ! -s "$WORK/failed-runner.out"
    if "$WORK/boundary" > "$WORK/failed-compiled.out" 2> "$WORK/failed-compiled.err"; then
        echo "FAIL: compiled program accepted clock failure $seconds"; exit 1
    fi
    grep -q "$diagnostic" "$WORK/failed-compiled.err"
    test ! -s "$WORK/failed-compiled.out"
done
echo 'PASS UTC day: fixed midnight/leap/negative boundaries in three timezones and clock/range failures through all five paths'
