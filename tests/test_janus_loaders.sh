#!/usr/bin/env bash
# tok_load and load_checkpoint refuse invalid files case by case, plain and under
# ASan/UBSan (zero reports); janus_train saves and resumes a tiny model and exits
# nonzero on a checkpoint whose shape differs from its configuration.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
LOADERS_CC=${CC:-cc}
make -C "$ROOT" libaml.a >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
SAN_FLAGS=(-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=undefined)

"$LOADERS_CC" -O2 -Wall -Wextra -I"$ROOT" "$ROOT/tests/test_janus_loaders.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/loaders-plain"
"$LOADERS_CC" -O2 -Wall -Wextra -I"$ROOT" "$ROOT/janus/janus_train.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/janus_train-plain"
builds=(plain)
if printf 'int main(void){return 0;}\n' > "$WORK/probe.c" &&
   "$LOADERS_CC" "${SAN_FLAGS[@]}" "$WORK/probe.c" -o "$WORK/probe" 2>/dev/null && "$WORK/probe"; then
    "$LOADERS_CC" "${SAN_FLAGS[@]}" -I"$ROOT" "$ROOT/tests/test_janus_loaders.c" \
        "$ROOT/core/ariannamethod.c" -lm -lpthread -o "$WORK/loaders-san"
    "$LOADERS_CC" "${SAN_FLAGS[@]}" -I"$ROOT" "$ROOT/janus/janus_train.c" \
        "$ROOT/core/ariannamethod.c" -lm -lpthread -o "$WORK/janus_train-san"
    builds+=(san)
else
    echo 'SKIP: toolchain has no -fsanitize=address,undefined'
fi

# load_checkpoint's own allocations are counted inside the case binary; leak detection is off
# so janus_train's main, outside these loaders, does not enter the report count.
awk 'BEGIN { for (i = 0; i < 4000; i++) printf "the field remembers line %d\n", i }' > "$WORK/data.txt"
TINY=(--n-embd 8 --n-heads 2 --n-layers 1 --seq-len 8 --grad-accum 1 --warmup-steps 1
      --bpe-after 100000000)
for b in "${builds[@]}"; do
    dir="$WORK/$b"
    mkdir -p "$dir/cases" "$dir/e2e"
    export ASAN_OPTIONS="detect_leaks=0:log_path=$dir/report"
    export UBSAN_OPTIONS="print_stacktrace=1:log_path=$dir/report"
    echo "== janus loaders [$b] =="
    (cd "$dir/cases" && "$WORK/loaders-$b")

    bin="$WORK/janus_train-$b"
    (cd "$dir/e2e" && "$bin" "$WORK/data.txt" --steps 2 --save-every 2 "${TINY[@]}") > "$dir/save.log" 2>&1
    (cd "$dir/e2e" && "$bin" "$WORK/data.txt" --steps 4 --save-every 100 \
        --resume janus_ckpt_step2.bin "${TINY[@]}") > "$dir/resume.log" 2>&1
    if ! grep -q 'Loaded: 10 weights from step 2' "$dir/resume.log" ||
       ! grep -q 'Resuming from step 2' "$dir/resume.log" || [ ! -s "$dir/e2e/janus_ckpt_step4.bin" ]; then
        echo "  FAIL [janus_train_resume]"; cat "$dir/resume.log"; exit 1
    fi
    echo "  PASS [janus_train_resume]"
    if (cd "$dir/e2e" && "$bin" "$WORK/data.txt" --steps 4 --resume janus_ckpt_step2.bin \
        "${TINY[@]}" --n-embd 16) > "$dir/mismatch.log" 2>&1; then
        echo "  FAIL [janus_train_shape_mismatch]: exit 0"; exit 1
    fi
    if ! grep -q 'checkpoint D=8 H=2 L=1 differs from config D=16 H=2 L=1' "$dir/mismatch.log"; then
        echo "  FAIL [janus_train_shape_mismatch]"; cat "$dir/mismatch.log"; exit 1
    fi
    echo "  PASS [janus_train_shape_mismatch]"

    reports=$(find "$dir" -name 'report.*' | wc -l | tr -d ' ')
    if [ "$reports" != 0 ]; then
        echo "  FAIL [$b]: $reports sanitizer report file(s)"; cat "$dir"/report.*; exit 1
    fi
    echo "  PASS [$b]: 0 sanitizer reports"
done
echo "janus loaders OK (${builds[*]})"
