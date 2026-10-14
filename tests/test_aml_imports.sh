#!/usr/bin/env bash
# Native modules: C APIs, interpreter, and amlc --scalar runtime constructor.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LINE_ONLY=0
if [ "${1:-}" = --line-capacity ]; then
    LINE_ONLY=1
elif [ "$#" -gt 0 ]; then
    echo 'usage: test_aml_imports.sh [--line-capacity]' >&2; exit 2
fi
if [ "$LINE_ONLY" -eq 0 ]; then make -C "$ROOT" all >/dev/null; fi
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/sources" "$WORK/prefix/lib" "$WORK/prefix/include/ariannamethod"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
cp "$ROOT/core/ariannamethod.h" "$WORK/prefix/include/ariannamethod/"
export AML_PREFIX="$WORK/prefix"

"${CC:-cc}" -Wall -Wextra -O2 -I"$ROOT/core" \
    "$ROOT/tests/test_aml_imports.c" "$ROOT/libaml.a" -lm -lpthread \
    -o "$WORK/import-api"
if [ "$LINE_ONLY" -eq 1 ]; then
    "$WORK/import-api" "$WORK/sources" --line-capacity
else
    "$WORK/import-api" "$WORK/sources"
fi

if [ "$LINE_ONLY" -eq 0 ] && [ "$(uname -s)" = Linux ]; then
    mkdir "$WORK/file-races"
    "${CC:-cc}" -Wall -Wextra -O2 -I"$ROOT/core" \
        "$ROOT/tests/test_aml_import_file.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=stat,--wrap=fstat -o "$WORK/import-file"
    timeout 20 "$WORK/import-file" "$WORK/file-races"
elif [ "$LINE_ONLY" -eq 0 ]; then
    echo 'SKIP: deterministic IMPORT path replacement requires linker --wrap'
fi

# Exact root capacity also survives amlc lowering before a C main effect.
cat > "$WORK/line-witness.aml" <<'EOF'
BLOOD MAIN {
#include <stdio.h>
#include <ariannamethod/ariannamethod.h>
int main(void) {
    if (am_get_state()->tension != 0.625f) {
        fputs("MISSING_BOUNDARY_WORKER\n", stderr);
        return 1;
    }
    puts("AML_EXACT_SOURCE_CAPACITY_OK");
    return 0;
}
}
EOF
cat "$WORK/line-witness.aml" >> "$WORK/sources/exact-lines.aml"
"$ROOT/tools/amlc" "$WORK/sources/exact-lines.aml" --scalar -o "$WORK/exact-lines" > "$WORK/exact-build.out" 2> "$WORK/exact-build.err"
"$WORK/exact-lines" > "$WORK/exact.out" 2> "$WORK/exact.err"
grep -qx 'AML_EXACT_SOURCE_CAPACITY_OK' "$WORK/exact.out"
test ! -s "$WORK/exact.err"
cat "$WORK/line-witness.aml" >> "$WORK/sources/truncated-lines.aml"
"$ROOT/tools/amlc" "$WORK/sources/truncated-lines.aml" --scalar -o "$WORK/truncated-lines" > "$WORK/truncated-build.out" 2> "$WORK/truncated-build.err"
if "$WORK/truncated-lines" > "$WORK/truncated.out" 2> "$WORK/truncated.err"; then
    echo 'FAIL: compiled witness accepted the omitted boundary worker'; exit 1
fi
grep -qx 'MISSING_BOUNDARY_WORKER' "$WORK/truncated.err"
test ! -s "$WORK/truncated.out"
echo 'PASS source capacity: exact 16384 root lines with a final worker through all five paths'
echo 'PASS source witness: omitting the final four lines fails the compiled C-main check'

for ((i=0; i<16385; i++)); do printf 'TENSION 0.875\n'; done > "$WORK/directive-overflow.aml"
if "$ROOT/tools/amlc" "$WORK/directive-overflow.aml" --emit-c > "$WORK/overflow.c" 2> "$WORK/overflow.err"; then
    echo 'FAIL: compiler accepted 16385 root directives'; exit 1
fi
grep -q 'too many AML directives (max 16384)' "$WORK/overflow.err"
echo 'PASS compiler overflow: root directive 16385 is rejected'
if [ "$LINE_ONLY" -eq 1 ]; then exit 0; fi

cat >> "$WORK/sources/function-capacity.aml" <<'EOF'
PRINT 'AML_FUNCTION_CAPACITY_OK'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("AML_FUNCTION_CAPACITY_MAIN_OK"); return 0; }
}
EOF
"$ROOT/runner/aml" "$WORK/sources/function-capacity.aml" > "$WORK/function-runner.out" 2> "$WORK/function-runner.err"
grep -qx 'AML_FUNCTION_CAPACITY_OK' "$WORK/function-runner.out"
"$ROOT/tools/amlc" "$WORK/sources/function-capacity.aml" --scalar -o "$WORK/function-capacity" > "$WORK/function-build.out" 2> "$WORK/function-build.err"
(cd "$WORK/sources/elsewhere" && "$WORK/function-capacity") > "$WORK/function.out" 2> "$WORK/function.err"
printf 'AML_FUNCTION_CAPACITY_OK\nAML_FUNCTION_CAPACITY_MAIN_OK\n' > "$WORK/function-expected.out"
diff -u "$WORK/function-expected.out" "$WORK/function.out"
test ! -s "$WORK/function.err"
test ! -s "$WORK/function-runner.err"
echo 'PASS function capacity: exact 495 imported definitions and inherited worker through interpreter/scalar CLI'

cat "$WORK/sources/graph.aml" > "$WORK/sources/main.aml"
cat >> "$WORK/sources/main.aml" <<'EOF'
if abs(tension - 0.7) < 0.000001:
    ECHO IMPORT_GRAPH_OK
else:
    ECHO WRONG_GRAPH
IMPORT "tree/origins.aml"
import_include()
if tension == 0.625:
    ECHO IMPORT_FUNCTION_ORIGIN_OK
else:
    ECHO WRONG_FUNCTION_ORIGIN
CHANNEL CREATE import_events 2
import_worker()
CHANNEL READ import_events observed
if observed == 0.75:
    ECHO IMPORT_WORKER_ORIGIN_OK
else:
    ECHO WRONG_WORKER_ORIGIN
EOF

cd "$WORK/sources/elsewhere"
"$ROOT/runner/aml" "$WORK/sources/main.aml" > "$WORK/interpreted.out" 2> "$WORK/interpreted.err"
"$ROOT/tools/amlc" "$WORK/sources/main.aml" --scalar -o "$WORK/native" \
    > "$WORK/build.out" 2> "$WORK/build.err"
"$WORK/native" > "$WORK/compiled.out" 2> "$WORK/compiled.err"
printf '[AML] IMPORT_GRAPH_OK\n[AML] IMPORT_FUNCTION_ORIGIN_OK\n[AML] IMPORT_WORKER_ORIGIN_OK\n' > "$WORK/expected.out"
diff -u "$WORK/expected.out" "$WORK/interpreted.out"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
rm "$WORK/sources/main.aml"
"$WORK/native" > "$WORK/embedded.out" 2> "$WORK/embedded.err"
diff -u "$WORK/expected.out" "$WORK/embedded.out"
echo 'PASS imports CLI: interpreter/scalar parity from another cwd, embedded root removed'

for name in missing duplicate cycle overflow functions; do
    cat > "$WORK/sources/failure.aml" <<'EOF'
ECHO WRONG_BEFORE_IMPORT
TENSION 0.99
EOF
    case "$name" in
        missing)
            printf 'IMPORT "absent.aml"\n' >> "$WORK/sources/failure.aml"
            expected='cannot resolve source'
            ;;
        duplicate)
            cat >> "$WORK/sources/failure.aml" <<'EOF'
IMPORT "tree/duplicate.aml"
def duplicate():
    return 2
EOF
            expected='duplicate function'
            ;;
        cycle)
            printf 'IMPORT "tree/cycle-a.aml"\n' >> "$WORK/sources/failure.aml"
            expected='IMPORT cycle'
            ;;
        overflow)
            printf 'IMPORT "tree/too-many-lines.aml"\n' >> "$WORK/sources/failure.aml"
            expected='line limit'
            ;;
        functions)
            cat >> "$WORK/sources/failure.aml" <<'EOF'
IMPORT "tree/capacity-low.aml"
IMPORT "tree/capacity-high.aml"
IMPORT "tree/capacity-overflow.aml"
EOF
            expected='capacity-overflow.aml:1: function limit exceeded'
            ;;
    esac
    cat >> "$WORK/sources/failure.aml" <<'EOF'
ECHO WRONG_AFTER_IMPORT
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
    if "$ROOT/runner/aml" "$WORK/sources/failure.aml" > "$WORK/fail.out" 2> "$WORK/fail.err"; then
        echo "FAIL: interpreter accepted $name"; exit 1
    fi
    grep -q "$expected" "$WORK/fail.err"
    test ! -s "$WORK/fail.out"
    "$ROOT/tools/amlc" "$WORK/sources/failure.aml" --scalar -o "$WORK/failure" \
        > "$WORK/build.out" 2> "$WORK/build.err"
    if "$WORK/failure" > "$WORK/fail.out" 2> "$WORK/fail.err"; then
        echo "FAIL: scalar runtime accepted $name"; exit 1
    fi
    grep -q "$expected" "$WORK/fail.err"
    test ! -s "$WORK/fail.out"
done
echo 'PASS imports CLI: missing/cycle/collision/line/function-budget failures precede AML effects and C main'
echo 'PASS native IMPORT suite'
