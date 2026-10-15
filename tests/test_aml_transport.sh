#!/usr/bin/env bash
# Content hashes, line journal, file slices and external processes through every execution path.
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TRANSPORT_CC=${CC:-cc}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere" "$WORK/vector dir"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"

"$TRANSPORT_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_transport.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/transport"
"$WORK/transport"
# Symbol interposition (both macOS and Linux): fsync failure, partial writes
# with and without restore, short reads, and open/fstat/pread failures, beneath an otherwise full run.
FAULT_LIBS=
if [ "$(uname -s)" = Linux ]; then FAULT_LIBS=-ldl; fi
"$TRANSPORT_CC" -O2 -Wall -Wextra -DAML_TRANSPORT_FAULTS -I"$ROOT/core" "$ROOT/tests/test_aml_transport.c" \
    "$ROOT/libaml.a" -lm -lpthread $FAULT_LIBS -o "$WORK/transport-faults"
"$WORK/transport-faults"

VECTORS="$WORK/vector dir/transport_vectors.aml"
cp "$ROOT/tests/transport_vectors.aml" "$VECTORS"
printf 'AML_TRANSPORT_VECTORS_OK\n' > "$WORK/expected"
(cd "$WORK/elsewhere" && "$ROOT/runner/aml" "$VECTORS") > "$WORK/runner"
diff -u "$WORK/expected" "$WORK/runner"
for mode in 0 1 2; do
    rm "$WORK/vector dir/journal.log"
    (cd "$WORK/vector dir" && "$WORK/transport" --run "$mode" "$VECTORS") > "$WORK/mode-$mode"
    diff -u "$WORK/expected" "$WORK/mode-$mode"
done
rm "$WORK/vector dir/journal.log"
"$ROOT/tools/amlc" "$VECTORS" --scalar -o "$WORK/transport program" > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/transport program") > "$WORK/compiled"
diff -u "$WORK/expected" "$WORK/compiled"
printf 'first\nsecond\n' | cmp - "$WORK/vector dir/journal.log"
echo 'PASS transport vectors: NIST hashes, source-relative journal, file size and slice cursor, process status/env/argv/timeout through five paths'

bad_count=$("$WORK/transport" --bad-count)
mkdir "$WORK/bad"
for ((i=0; i<bad_count; i++)); do
    "$WORK/transport" --bad-fixture "$i" > "$WORK/bad/bad.aml"
    if (cd "$WORK/elsewhere" && "$ROOT/runner/aml" "$WORK/bad/bad.aml") > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted transport fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if (cd "$WORK/bad" && "$WORK/transport" --run "$mode" "$WORK/bad/bad.aml") > "$WORK/bad-$mode.out" 2> "$WORK/bad-$mode.err"; then
            echo "FAIL: mode $mode accepted transport fixture $i"; exit 1
        fi
    done
    cat >> "$WORK/bad/bad.aml" <<'AML'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
AML
    "$ROOT/tools/amlc" "$WORK/bad/bad.aml" --scalar -o "$WORK/bad/program" \
        > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if (cd "$WORK/elsewhere" && "$WORK/bad/program") > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: compiled program accepted transport fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-0.out" "$WORK/bad-1.out" "$WORK/bad-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: transport fixture $i continued after its error"; exit 1
    fi
    if [ -e "$WORK/bad/journal.log" ]; then
        echo "FAIL: transport fixture $i created the journal before its error"; exit 1
    fi
done
echo "PASS transport rejection: $bad_count invalid calls stop in all five paths before later AML, C main or journal effects"
