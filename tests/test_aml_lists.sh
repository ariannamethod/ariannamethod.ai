#!/usr/bin/env bash
# List values, fault ownership and interpreter/compiler parity.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LIST_CC=${CC:-cc}
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$LIST_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_lists_api.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/list-api"
"$WORK/list-api"
if [ "$(uname -s)" = Linux ]; then
    "$LIST_CC" -O2 -Wall -Wextra -I"$ROOT/core" -DAML_LIST_ALLOC_WRAP \
        "$ROOT/tests/test_aml_lists_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=realpath -o "$WORK/list-alloc"
    "$WORK/list-alloc"
else
    echo 'SKIP: allocation-injection gate requires GNU-compatible linker --wrap'
fi

"$LIST_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_lists.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/list-runtime"
"$WORK/list-runtime"

mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
"$WORK/list-runtime" --fixture > "$WORK/values.aml"
"$WORK/list-runtime" --expected > "$WORK/expected.out"
"$ROOT/runner/aml" "$WORK/values.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/list-runtime" --run "$mode" "$WORK/values.aml" > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/list program" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/list program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
echo 'PASS list PRINT: byte-exact JSON escaping through five execution paths'

bad_count=$("$WORK/list-runtime" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/list-runtime" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted bad list fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/list-runtime" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-mode-$mode.out" 2> "$WORK/bad-mode-$mode.err"; then
            echo "FAIL: mode $mode accepted bad list fixture $i"; exit 1
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
        echo "FAIL: amlc program accepted bad list fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-mode-0.out" \
            "$WORK/bad-mode-1.out" "$WORK/bad-mode-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: list fixture $i continued after its error"; exit 1
    fi
done
echo "PASS list errors: $bad_count fixtures fail through every execution path before continuation/C main"
