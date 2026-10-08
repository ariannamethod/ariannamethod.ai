#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EXPR_CC=${CC:-cc}
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
"$EXPR_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_expressions.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/expressions"
"$WORK/expressions"
mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
"$WORK/expressions" --fixture > "$WORK/values.aml"
"$WORK/expressions" --expected > "$WORK/expected.out"
"$ROOT/runner/aml" "$WORK/values.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/expressions" --run "$mode" "$WORK/values.aml" > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/expressions compiled" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/expressions compiled") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
echo 'PASS expression values: all five execution paths agree'
bad_count=$("$WORK/expressions" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/expressions" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted expression fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/expressions" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-mode-$mode.out" 2> "$WORK/bad-mode-$mode.err"; then
            echo "FAIL: mode $mode accepted expression fixture $i"; exit 1
        fi
    done
    cat >> "$WORK/bad.aml" <<'AML'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
AML
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: compiled program accepted expression fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-mode-0.out" \
            "$WORK/bad-mode-1.out" "$WORK/bad-mode-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: expression fixture $i continued after its error"; exit 1
    fi
done
echo "PASS expression errors: $bad_count fixtures stop before later AML or C-main effects through all five paths"
