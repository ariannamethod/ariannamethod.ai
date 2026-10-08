#!/usr/bin/env bash
# Flat typed records and portable checkpoints in every execution path.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
RECORD_CC=${CC:-cc}
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$RECORD_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_records_api.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/records-api"
"$WORK/records-api"
"$RECORD_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_checkpoints.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/checkpoints"
"$WORK/checkpoints"
if [ "$(uname -s)" = Linux ]; then
    "$RECORD_CC" -O2 -Wall -Wextra -DAML_RECORD_ALLOC_WRAP -I"$ROOT/core" \
        "$ROOT/tests/test_aml_records_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$WORK/records-alloc"
    "$WORK/records-alloc"
    "$RECORD_CC" -O2 -Wall -Wextra -DAML_CHECKPOINT_ALLOC_WRAP -I"$ROOT/core" \
        "$ROOT/tests/test_aml_checkpoints.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$WORK/checkpoints-alloc"
    "$WORK/checkpoints-alloc"
    "$RECORD_CC" -O2 -Wall -Wextra -DAML_CHECKPOINT_IO_WRAP -I"$ROOT/core" \
        "$ROOT/tests/test_aml_checkpoints.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=write,--wrap=read,--wrap=fsync,--wrap=close,--wrap=rename,--wrap=fstat,--wrap=stat -o "$WORK/checkpoints-io"
    "$WORK/checkpoints-io"
else
    echo 'SKIP: record/checkpoint failure injection requires linker --wrap'
fi
"$RECORD_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_records_runtime.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/records-runtime"
"$WORK/records-runtime"
mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere" "$WORK/state module"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
printf 'AML_RECORD_VECTORS_OK\n' > "$WORK/expected.out"
"$ROOT/runner/aml" "$ROOT/tests/record_vectors.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/records-runtime" --run "$mode" "$ROOT/tests/record_vectors.aml" > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$ROOT/tests/record_vectors.aml" --scalar -o "$WORK/record program" > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/record program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
echo 'PASS record values: five leaf types, retained organ calls, copies, replacement and swap through five paths'

cat > "$WORK/state module/storage.aml" <<'EOF'
def save_state(state):
    return checkpoint_save(state, 'state file.amlcp')
def load_state():
    return checkpoint_load('state file.amlcp')
def state_exists():
    return file_exists('state file.amlcp')
EOF
printf 'IMPORT "%s/state module/storage.aml"\n' "$WORK" > "$WORK/checkpoint.aml"
cat >> "$WORK/checkpoint.aml" <<'EOF'
assert(state_exists() == 0, 'fresh checkpoint absent')
state = record_new()
record_set(state, 'schema', 1)
record_set(state, 'voice', 'שלום 🌧')
array = matrix_zeros(2, 3)
array[0] = 0.25
array[5] = -7
record_set(state, 'array', array)
parts = list_new()
list_push(parts, '')
list_push(parts, 'é')
record_set(state, 'parts', parts)
weights = map_new()
map_set(weights, 'pulse', 0.75)
record_set(state, 'weights', weights)
assert(save_state(state) == 1, 'durable checkpoint')
assert(state_exists() == 1, 'checkpoint exists')
record_set(state, 'schema', 99)
reloaded = load_state()
assert(record_get(reloaded, 'schema') == 1, 'detached saved scalar')
assert(text_equal(record_get(reloaded, 'voice'), 'שלום 🌧'), 'saved Unicode')
restored_array = record_get(reloaded, 'array')
assert(rows(restored_array) == 2 and cols(restored_array) == 3, 'saved matrix shape')
assert(restored_array[0] == 0.25 and restored_array[5] == -7, 'saved matrix values')
assert(list_len(record_get(reloaded, 'parts')) == 2, 'saved list length')
assert(text_equal(list_get(record_get(reloaded, 'parts'), 0), ''), 'empty list item')
assert(map_get(record_get(reloaded, 'weights'), 'pulse') == 0.75, 'saved ordered map')
assert(text_equal(list_get(record_keys(reloaded), 0), 'schema'), 'saved field order')
record_swap(state, reloaded)
assert(record_get(state, 'schema') == 1, 'loaded state published')
assert(record_get(reloaded, 'schema') == 99, 'old state retained')
PRINT 'AML_CHECKPOINT_VECTORS_OK'
EOF
printf 'AML_CHECKPOINT_VECTORS_OK\n' > "$WORK/checkpoint-expected.out"
(cd "$WORK/elsewhere" && "$ROOT/runner/aml" "$WORK/checkpoint.aml") > "$WORK/checkpoint-runner.out" 2> "$WORK/checkpoint-runner.err"
diff -u "$WORK/checkpoint-expected.out" "$WORK/checkpoint-runner.out"
for mode in 0 1 2; do
    rm "$WORK/state module/state file.amlcp"
    (cd "$WORK/elsewhere" && "$WORK/records-runtime" --run "$mode" "$WORK/checkpoint.aml") > "$WORK/checkpoint-$mode.out" 2> "$WORK/checkpoint-$mode.err"
    diff -u "$WORK/checkpoint-expected.out" "$WORK/checkpoint-$mode.out"
done
rm "$WORK/state module/state file.amlcp"
"$ROOT/tools/amlc" "$WORK/checkpoint.aml" --scalar -o "$WORK/checkpoint program" > "$WORK/checkpoint-build.out" 2> "$WORK/checkpoint-build.err"
(cd "$WORK/elsewhere" && "$WORK/checkpoint program") > "$WORK/checkpoint-compiled.out" 2> "$WORK/checkpoint-compiled.err"
diff -u "$WORK/checkpoint-expected.out" "$WORK/checkpoint-compiled.out"
echo 'PASS checkpoints: detached round-trip, matrix shape and imported paths from another working directory through five paths'

bad_count=$("$WORK/records-runtime" --bad-count)
for ((i=0; i<bad_count; i++)); do
    "$WORK/records-runtime" --bad-fixture "$i" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted record fixture $i"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/records-runtime" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-$mode.out" 2> "$WORK/bad-$mode.err"; then
            echo "FAIL: record mode $mode accepted fixture $i"; exit 1
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
        echo "FAIL: compiled record accepted fixture $i"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-0.out" "$WORK/bad-1.out" "$WORK/bad-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: record fixture $i continued after error"; exit 1
    fi
done
echo "PASS record rejection: $bad_count typed failures stop in all five paths before later AML or C-main effects"
