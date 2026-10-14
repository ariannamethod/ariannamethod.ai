#!/usr/bin/env bash
# Fixed decimal text, owned failure, and expanded function table capacity.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FIXED_CC=${CC:-cc}
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# A private locale makes the comma-decimal gate reproducible without changing
# the host's installed locale archive. Hosts without locale sources still run
# all API cases under their available locales and every fenv rounding mode.
if command -v localedef >/dev/null && [ -f /usr/share/i18n/locales/de_DE ]; then
    mkdir -p "$WORK/locales"
    if localedef --no-archive -i de_DE -f UTF-8 "$WORK/locales/de_DE.UTF-8" \
            > "$WORK/locale.log" 2>&1; then
        export LOCPATH="$WORK/locales${LOCPATH:+:$LOCPATH}"
        export AML_FIXED_TEST_LOCALE=1
    fi
fi
"$FIXED_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_text_fixed_api.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/fixed-api"
"$WORK/fixed-api"
if [ "$(uname -s)" = Linux ]; then
    "$FIXED_CC" -O2 -Wall -Wextra -DAML_FIXED_ALLOC_WRAP -I"$ROOT/core" \
        "$ROOT/tests/test_aml_text_fixed_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=realpath \
        -o "$WORK/fixed-alloc"
    "$WORK/fixed-alloc"
else
    echo 'SKIP: fixed allocation-injection gate requires GNU-compatible linker --wrap'
fi
"$FIXED_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_text_fixed.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/fixed-runtime"
"$WORK/fixed-runtime"

mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
"$WORK/fixed-runtime" --fixture > "$WORK/values.aml"
"$WORK/fixed-runtime" --expected > "$WORK/expected.out"
"$ROOT/runner/aml" "$WORK/values.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/fixed-runtime" --run "$mode" "$WORK/values.aml" \
        > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/fixed program" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/fixed program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
echo 'PASS fixed text output: 620 Python fixtures and owned values match in all five execution paths'

bad_count=$("$WORK/fixed-runtime" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/fixed-runtime" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted bad fixed fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/fixed-runtime" --run "$mode" "$WORK/bad.aml" \
                > "$WORK/bad-mode-$mode.out" 2> "$WORK/bad-mode-$mode.err"; then
            echo "FAIL: mode $mode accepted bad fixed fixture $i"; exit 1
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
        echo "FAIL: compiled program accepted bad fixed fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-mode-0.out" \
            "$WORK/bad-mode-1.out" "$WORK/bad-mode-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: fixed fixture $i continued after its error"; exit 1
    fi
done
echo "PASS fixed text errors: $bad_count programs fail in all five execution paths"

for count in 112 239 240 495 496; do
    "$WORK/fixed-runtime" --capacity "$count" > "$WORK/capacity.aml"
    if [ "$count" -lt 496 ]; then
        printf '%s\n' "$((count * (count - 1) / 2))" > "$WORK/capacity.expected"
        "$ROOT/runner/aml" "$WORK/capacity.aml" > "$WORK/capacity.out" 2> "$WORK/capacity.err"
        diff -u "$WORK/capacity.expected" "$WORK/capacity.out"
        "$ROOT/tools/amlc" "$WORK/capacity.aml" --scalar -o "$WORK/capacity" \
            > "$WORK/capacity-build.out" 2> "$WORK/capacity-build.err"
        "$WORK/capacity" > "$WORK/capacity.out" 2> "$WORK/capacity.err"
        diff -u "$WORK/capacity.expected" "$WORK/capacity.out"
    else
        cat >> "$WORK/capacity.aml" <<'EOF'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
        if "$ROOT/runner/aml" "$WORK/capacity.aml" > "$WORK/capacity.out" 2> "$WORK/capacity.err"; then
            echo 'FAIL: runner accepted 496 user functions'; exit 1
        fi
        grep -q 'function limit exceeded' "$WORK/capacity.err"
        test ! -s "$WORK/capacity.out"
        "$ROOT/tools/amlc" "$WORK/capacity.aml" --scalar -o "$WORK/capacity" \
            > "$WORK/capacity-build.out" 2> "$WORK/capacity-build.err"
        if "$WORK/capacity" > "$WORK/capacity.out" 2> "$WORK/capacity.err"; then
            echo 'FAIL: compiled program accepted 496 user functions'; exit 1
        fi
        grep -q 'function limit exceeded' "$WORK/capacity.err"
        test ! -s "$WORK/capacity.out"
    fi
done
echo 'PASS function slots: all five paths admit 112/239/240/495 user definitions and reject 496 before effects'
