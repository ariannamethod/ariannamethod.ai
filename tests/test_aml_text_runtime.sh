#!/usr/bin/env bash
# Native UTF-8 + typed values across all execution entry points.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TEXT_CC=${CC:-cc}
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$TEXT_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_text_api.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/text-api"
"$WORK/text-api"

# The wrapper gate accounts for every owner and data-buffer free, including
# a refused second allocation. --wrap is a GNU-compatible linker feature.
if [ "$(uname -s)" = Linux ]; then
    "$TEXT_CC" -O2 -Wall -Wextra -I"$ROOT/core" -DAML_TEXT_ALLOC_WRAP \
        "$ROOT/tests/test_aml_text_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=free -o "$WORK/text-alloc"
    "$WORK/text-alloc"
else
    echo 'SKIP: allocation-injection gate requires GNU-compatible linker --wrap'
fi

"$TEXT_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_text_runtime.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/text-runtime"
"$WORK/text-runtime"

mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
"$WORK/text-runtime" --fixture > "$WORK/values.aml"
"$WORK/text-runtime" --expected > "$WORK/expected.out"
"$ROOT/runner/aml" "$WORK/values.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/text-runtime" --run "$mode" "$WORK/values.aml" > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/text program" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/text program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
echo 'PASS PRINT: byte-exact interpreter, resumable, bytecode, runner, and amlc --scalar output'

bad_count=$("$WORK/text-runtime" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/text-runtime" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted bad text fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/text-runtime" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-mode-$mode.out" 2> "$WORK/bad-mode-$mode.err"; then
            echo "FAIL: mode $mode accepted bad text fixture $i"; exit 1
        fi
    done
    cat >> "$WORK/bad.aml" <<'EOF'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: amlc program accepted bad text fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-mode-0.out" \
            "$WORK/bad-mode-1.out" "$WORK/bad-mode-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: text fixture $i continued after its error"; exit 1
    fi
done
echo "PASS errors: $bad_count fixtures fail through every execution path before continuation/C main"
