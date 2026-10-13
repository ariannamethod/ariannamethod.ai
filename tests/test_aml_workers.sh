#!/usr/bin/env bash
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/prefix/lib"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
cc -Wall -Wextra -O2 -I"$ROOT/core" "$ROOT/tests/test_aml_workers.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/workers"
"$WORK/workers"
if [ "$(uname -s)" = Linux ]; then
    cc -Wall -Wextra -O2 -DAML_WORKER_ALLOC_WRAP -I"$ROOT/core" "$ROOT/tests/test_aml_workers.c" \
        "$ROOT/libaml.a" -lm -lpthread -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=pthread_create \
        -o "$WORK/workers-allocation"
    "$WORK/workers-allocation"
fi
printf 'AML_WORKERS_OK\n' > "$WORK/expected"
for mode in 0 1 2; do
    "$WORK/workers" "$mode" "$ROOT/tests/workers.aml" > "$WORK/mode-$mode"
    diff -u "$WORK/expected" "$WORK/mode-$mode"
done
"$ROOT/runner/aml" "$ROOT/tests/workers.aml" > "$WORK/runner"
diff -u "$WORK/expected" "$WORK/runner"
"$ROOT/tools/amlc" "$ROOT/tests/workers.aml" --scalar -o "$WORK/compiled" --run > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected" "$WORK/compiled.out"
# A helper's INCLUDE remains relative to its defining source inside the worker.
mkdir "$WORK/modules"
cat > "$WORK/modules/helper.aml" <<'AML'
def imported(value):
    INCLUDE "inside.aml"
    return value * 2
AML
printf 'CHANNEL WRITE origins 1\n' > "$WORK/modules/inside.aml"
cat > "$WORK/imported.aml" <<'AML'
IMPORT "modules/helper.aml"
CHANNEL CREATE origins 1
def start(value):
    SPAWN imported_worker:
        return imported(value)
    AWAIT imported_worker INTO result
    return result
assert(start(7) == 14, 'imported worker function')
CHANNEL READ origins count
assert(count == 1, 'definition origin')
PRINT 'IMPORTED_WORKER_OK'
AML
printf 'IMPORTED_WORKER_OK\n' > "$WORK/imported.expected"
"$ROOT/runner/aml" "$WORK/imported.aml" > "$WORK/imported.out"
diff -u "$WORK/imported.expected" "$WORK/imported.out"
"$ROOT/tools/amlc" "$WORK/imported.aml" --scalar -o "$WORK/imported" --run > "$WORK/imported.out" 2> "$WORK/imported.err"
diff -u "$WORK/imported.expected" "$WORK/imported.out"
# Direct and compiled exit drain failures even when source omits AWAIT.
cat > "$WORK/failed.aml" <<'AML'
SPAWN failed:
    assert(0, 'worker drain failed')
AML
if "$ROOT/runner/aml" "$WORK/failed.aml" > "$WORK/failure.out" 2> "$WORK/failure.err"; then
    echo 'FAIL runner discarded unfinished worker'; exit 1
fi
grep -q 'worker drain failed' "$WORK/failure.err"
if "$ROOT/tools/amlc" "$WORK/failed.aml" --scalar -o "$WORK/failed" --run > "$WORK/failure.out" 2> "$WORK/failure.err"; then
    echo 'FAIL compiled program discarded unfinished worker'; exit 1
fi
grep -q 'worker drain failed' "$WORK/failure.err"
echo 'PASS workers: 80 repeated typed jobs, inherited functions/locals, source origins, five runtime paths and exit failure'
