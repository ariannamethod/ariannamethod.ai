#!/usr/bin/env bash
# Field organ and exp/log through interpreter, prepared, bytecode, CLI and compiled AML.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
FIELD_CC=${CC:-cc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"

"$FIELD_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_field.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/field"
"$WORK/field"
"$WORK/field" --fixture > "$WORK/values.aml"
printf 'FIELD_OK\n' > "$WORK/expected"
for mode in 0 1 2; do
    "$WORK/field" --run "$mode" "$WORK/values.aml" > "$WORK/mode-$mode"
    diff -u "$WORK/expected" "$WORK/mode-$mode"
done
"$ROOT/runner/aml" "$WORK/values.aml" > "$WORK/runner"
diff -u "$WORK/expected" "$WORK/runner"
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/field compiled" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/field compiled") > "$WORK/compiled"
diff -u "$WORK/expected" "$WORK/compiled"
echo 'PASS field values: golden fixture agrees in all five execution paths'

bad_count=$("$WORK/field" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/field" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted field fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/field" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-$mode.out" 2> "$WORK/bad-$mode.err"; then
            echo "FAIL: mode $mode accepted field fixture $i"; exit 1
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
        echo "FAIL: compiled program accepted field fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-0.out" "$WORK/bad-1.out" "$WORK/bad-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: field fixture $i continued after its error"; exit 1
    fi
done
echo "PASS field refusals: $bad_count invalid programs stop in all five execution paths"
