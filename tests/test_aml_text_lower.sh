#!/usr/bin/env bash
# Locale-independent Unicode lowercase through every AML execution entry point.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOWER_CC=${CC:-cc}
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$LOWER_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_text_lower_api.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/lower-api"
"$WORK/lower-api"
if [ "$(uname -s)" = Linux ]; then
    "$LOWER_CC" -O2 -Wall -Wextra -DAML_LOWER_ALLOC_WRAP -I"$ROOT/core" \
        "$ROOT/tests/test_aml_text_lower_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=free -o "$WORK/lower-alloc"
    "$WORK/lower-alloc"
else
    echo 'SKIP: lowercase allocation-injection gate requires linker --wrap'
fi
"$LOWER_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_text_lower_runtime.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/lower-runtime"
"$WORK/lower-runtime"

mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
"$WORK/lower-runtime" --fixture > "$WORK/values.aml"
"$WORK/lower-runtime" --expected > "$WORK/expected.out"
"$ROOT/runner/aml" "$WORK/values.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/lower-runtime" --run "$mode" "$WORK/values.aml" \
        > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/lower program" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/lower program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
echo 'PASS lowercase output: 62 Python fixtures, aliases and nested calls match in all five execution paths'

bad_count=$("$WORK/lower-runtime" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/lower-runtime" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted bad lowercase fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/lower-runtime" --run "$mode" "$WORK/bad.aml" \
                > "$WORK/bad-mode-$mode.out" 2> "$WORK/bad-mode-$mode.err"; then
            echo "FAIL: mode $mode accepted bad lowercase fixture $i"; exit 1
        fi
    done
    cat >> "$WORK/bad.aml" <<'EOF'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" \
        > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: compiled program accepted bad lowercase fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-mode-0.out" \
            "$WORK/bad-mode-1.out" "$WORK/bad-mode-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: lowercase fixture $i continued after its error"; exit 1
    fi
done
echo "PASS lowercase errors: $bad_count fixtures fail in all five execution paths"
