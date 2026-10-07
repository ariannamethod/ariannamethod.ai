#!/usr/bin/env bash
# Native modules: C APIs, interpreter, and amlc --scalar runtime constructor.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/sources" "$WORK/prefix/lib"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"

"${CC:-cc}" -Wall -Wextra -O2 -I"$ROOT/core" \
    "$ROOT/tests/test_aml_imports.c" "$ROOT/libaml.a" -lm -lpthread \
    -o "$WORK/import-api"
"$WORK/import-api" "$WORK/sources"

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

for name in missing duplicate cycle overflow; do
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
echo 'PASS imports CLI: missing/cycle/collision/expanded-budget failures precede AML effects and C main'
echo 'PASS native IMPORT suite'
