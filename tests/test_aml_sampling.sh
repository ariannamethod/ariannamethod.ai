#!/usr/bin/env bash
# Canonical NoTorch bridge, owned stream snapshots and execution-path parity.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SAMPLING_CC=${CC:-cc}
SAMPLING_NTORCH_ROOT=${NOTORCH_ROOT:-"$ROOT/../notorch"}
SAMPLING_NTORCH_INCLUDE=${NOTORCH_INCLUDE:-"$SAMPLING_NTORCH_ROOT"}
SAMPLING_NTORCH_LIB=${NOTORCH_LIB:-"$SAMPLING_NTORCH_ROOT/libnotorch.a"}
read -r -a SAMPLING_LINK_FLAGS <<< "${NOTORCH_LDFLAGS:-}"
make -C "$ROOT" notorch NOTORCH_INCLUDE="$SAMPLING_NTORCH_INCLUDE" \
    NOTORCH_LIB="$SAMPLING_NTORCH_LIB" NOTORCH_LDFLAGS="${NOTORCH_LDFLAGS:-}" >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$SAMPLING_CC" -O2 -Wall -Wextra -I"$ROOT/core" \
    "$ROOT/tests/test_aml_sampling_api.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/sampling-api"
"$WORK/sampling-api"
if [ "$(uname -s)" = Linux ]; then
    "$SAMPLING_CC" -O2 -Wall -Wextra -I"$ROOT/core" -DAML_SAMPLING_ALLOC_WRAP \
        "$ROOT/tests/test_aml_sampling_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=free -o "$WORK/sampling-alloc"
    "$WORK/sampling-alloc"
else
    echo 'SKIP: sampling allocation injection requires GNU-compatible linker --wrap'
fi
"$SAMPLING_CC" -O2 -Wall -Wextra -I"$ROOT/core" -I"$SAMPLING_NTORCH_INCLUDE" \
    "$ROOT/tests/test_aml_sampling.c" "$ROOT/libaml_notorch.a" "$ROOT/libaml.a" \
    "$SAMPLING_NTORCH_LIB" -lm -lpthread "${SAMPLING_LINK_FLAGS[@]}" -o "$WORK/sampling-runtime"
"$WORK/sampling-runtime"

mkdir -p "$WORK/prefix/lib" "$WORK/prefix/include/ariannamethod" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$ROOT/libaml_notorch.a" "$SAMPLING_NTORCH_LIB" "$WORK/prefix/lib/"
cp "$ROOT/core/ariannamethod.h" "$WORK/prefix/include/ariannamethod/"
export AML_PREFIX="$WORK/prefix"
printf 'AML_SAMPLING_VECTORS_OK\n' > "$WORK/expected.out"
"$ROOT/runner/aml-notorch" "$ROOT/tests/sampling_vectors.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/sampling-runtime" --run "$mode" "$ROOT/tests/sampling_vectors.aml" > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$ROOT/tests/sampling_vectors.aml" --scalar -o "$WORK/sampling program" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/sampling program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
grep -q 'libaml_notorch libaml libnotorch scalar' "$WORK/build.err"
echo 'PASS sampling vectors: independent integer oracle and ownership/categorical checks through five paths'

cat > "$WORK/host.aml" <<'EOF'
BLOOD MAIN {
#include <ariannamethod/ariannamethod.h>
int main(void) {
    return am_exec("rng = rng_new(42)\nassert(rng_index(rng, 1) == 0, 'host sampling')\n");
}
}
EOF
"$ROOT/tools/amlc" "$WORK/host.aml" --scalar -o "$WORK/host" > "$WORK/host-build.out" 2> "$WORK/host-build.err"
"$WORK/host"
echo 'PASS sampling registration: BLOOD-only C main receives the optional backend'

# Plain core remains independently linkable and reports an absent backend.
printf 'rng = rng_new(42)\nPRINT "WRONG_CONTINUATION"\n' > "$WORK/missing.aml"
if "$ROOT/runner/aml" "$WORK/missing.aml" > "$WORK/missing-runner.out" 2> "$WORK/missing-runner.err"; then
    echo 'FAIL: plain runner silently supplied sampling'; exit 1
fi
grep -q 'sampling backend' "$WORK/missing-runner.err"
for absent in bridge notorch; do
    if [ "$absent" = bridge ]; then archive=libaml_notorch.a; else archive=libnotorch.a; fi
    mv "$WORK/prefix/lib/$archive" "$WORK/$archive"
    "$ROOT/tools/amlc" "$WORK/missing.aml" --scalar -o "$WORK/missing-$absent" \
        > "$WORK/missing-build.out" 2> "$WORK/missing-build.err"
    if "$WORK/missing-$absent" > "$WORK/missing-$absent.out" 2> "$WORK/missing-$absent.err"; then
        echo "FAIL: incomplete prefix ($absent absent) silently supplied sampling"; exit 1
    fi
    grep -q 'sampling backend' "$WORK/missing-$absent.err"
    mv "$WORK/$archive" "$WORK/prefix/lib/$archive"
done
if grep -q WRONG "$WORK/missing-runner.out" "$WORK/missing-bridge.out" "$WORK/missing-notorch.out"; then
    echo 'FAIL: missing backend continued execution'; exit 1
fi
echo 'PASS sampling isolation: standalone runner and incomplete installed prefixes fail explicitly'

bad_cases=(
    'rng_new(-1)'
    'rng_index(rng, 0)'
    'rng_uniform(map_new())'
    'rng_categorical(rng, [0, 0], 1)'
    'rng_categorical(rng, [1, 2], 0)'
    'rng_categorical(rng, [1, 1e39], 1)'
    'categorical_at([1], 1, 1)'
    'categorical_at([1], 1, (1e39 - 1e39))'
)
for bad in "${bad_cases[@]}"; do
    printf 'rng = rng_new(42)\n%s\nPRINT "WRONG_CONTINUATION"\n' "$bad" > "$WORK/bad.aml"
    if "$ROOT/runner/aml-notorch" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: sampling runner accepted $bad"; exit 1
    fi
    cat >> "$WORK/bad.aml" <<'EOF'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: compiled sampling accepted $bad"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: sampling continued after $bad"; exit 1
    fi
done
echo "PASS sampling errors: ${#bad_cases[@]} runner/compiler cases stop before continuation and C main"

# A modular organism's expanded budget also remains available to a root file.
printf 'count = 0\n' > "$WORK/large.aml"
for ((i=0; i<600; i++)); do printf 'count = count + 1\n' >> "$WORK/large.aml"; done
printf 'assert(count == 600, "compiled root source budget")\n' >> "$WORK/large.aml"
"$ROOT/tools/amlc" "$WORK/large.aml" --scalar -o "$WORK/large" > "$WORK/large-build.out" 2> "$WORK/large-build.err"
"$WORK/large"
echo 'PASS compiler source budget: 602 root directives survive lowering and execution'
for ((i=0; i<4097; i++)); do printf 'count = 1\n'; done > "$WORK/overflow.aml"
if "$ROOT/tools/amlc" "$WORK/overflow.aml" --emit-c > "$WORK/overflow.c" 2> "$WORK/overflow.err"; then
    echo 'FAIL: compiler accepted 4097 root directives'; exit 1
fi
grep -q 'too many AML directives (max 4096)' "$WORK/overflow.err"
echo 'PASS compiler overflow: the 4097th root directive is rejected'
