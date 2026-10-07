#!/usr/bin/env bash
# Numerical values: independent fixtures, ownership, errors and optional binding.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NUMERICAL_CC=${CC:-cc}
NUMERICAL_NTORCH_ROOT=${NOTORCH_ROOT:-"$ROOT/../notorch"}
NUMERICAL_NTORCH_INCLUDE=${NOTORCH_INCLUDE:-"$NUMERICAL_NTORCH_ROOT"}
NUMERICAL_NTORCH_LIB=${NOTORCH_LIB:-"$NUMERICAL_NTORCH_ROOT/libnotorch.a"}
read -r -a NUMERICAL_LINK_FLAGS <<< "${NOTORCH_LDFLAGS:-}"
make -C "$ROOT" notorch NOTORCH_INCLUDE="$NUMERICAL_NTORCH_INCLUDE" \
    NOTORCH_LIB="$NUMERICAL_NTORCH_LIB" NOTORCH_LDFLAGS="${NOTORCH_LDFLAGS:-}" >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$NUMERICAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" \
    "$ROOT/tests/test_aml_numerical_api.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/numerical-api"
"$WORK/numerical-api"
if [ "$(uname -s)" = Linux ]; then
    "$NUMERICAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" -DAML_NUMERICAL_ALLOC_WRAP \
        "$ROOT/tests/test_aml_numerical_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=free -o "$WORK/numerical-alloc"
    "$WORK/numerical-alloc"
else
    echo 'SKIP: numerical allocation injection requires GNU-compatible linker --wrap'
fi
"$NUMERICAL_CC" -O2 -Wall -Wextra -I"$ROOT/core" -I"$NUMERICAL_NTORCH_INCLUDE" \
    "$ROOT/tests/test_aml_numerical.c" "$ROOT/libaml_notorch.a" "$ROOT/libaml.a" \
    "$NUMERICAL_NTORCH_LIB" -lm -lpthread "${NUMERICAL_LINK_FLAGS[@]}" -o "$WORK/numerical-runtime"
"$WORK/numerical-runtime"

mkdir -p "$WORK/prefix/lib" "$WORK/prefix/include/ariannamethod" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$ROOT/libaml_notorch.a" "$NUMERICAL_NTORCH_LIB" "$WORK/prefix/lib/"
cp "$ROOT/core/ariannamethod.h" "$WORK/prefix/include/ariannamethod/"
export AML_PREFIX="$WORK/prefix"
printf 'AML_NUMERICAL_VECTORS_OK\n' > "$WORK/expected.out"
"$ROOT/runner/aml-notorch" "$ROOT/tests/numerical_vectors.aml" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
for mode in 0 1 2; do
    "$WORK/numerical-runtime" --run "$mode" "$ROOT/tests/numerical_vectors.aml" > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$ROOT/tests/numerical_vectors.aml" --scalar -o "$WORK/numerical program" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/numerical program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
grep -q 'libaml_notorch libaml libnotorch scalar' "$WORK/build.err"
echo 'PASS numerical vectors: scalar oracle, twelve learning steps and replayable normals through five paths'

cat > "$WORK/host.aml" <<'EOF'
BLOOD MAIN {
#include <ariannamethod/ariannamethod.h>
int main(void) {
    return am_exec("a = nt_tanh([0])\nassert(a[0] == 0, 'host numerical')\n");
}
}
EOF
"$ROOT/tools/amlc" "$WORK/host.aml" --scalar -o "$WORK/host" > "$WORK/host-build.out" 2> "$WORK/host-build.err"
"$WORK/host"
"$ROOT/tools/amlc" "$WORK/host.aml" --emit-c > "$WORK/manual.c" 2> "$WORK/emit.err"
for macro in AML_LINK_NOTORCH AML_LINK_NOTORCH_SAMPLING; do
    "$NUMERICAL_CC" -O2 -D"$macro" -I"$WORK/prefix/include" "$WORK/manual.c" \
        "$ROOT/libaml_notorch.a" "$ROOT/libaml.a" "$NUMERICAL_NTORCH_LIB" \
        -lm -lpthread "${NUMERICAL_LINK_FLAGS[@]}" -o "$WORK/manual-$macro"
    "$WORK/manual-$macro"
done
echo 'PASS numerical registration: BLOOD-only host and both manual bridge macros'

printf 'a = nt_tanh([0])\nPRINT "WRONG_CONTINUATION"\n' > "$WORK/missing.aml"
if "$ROOT/runner/aml" "$WORK/missing.aml" > "$WORK/missing-runner.out" 2> "$WORK/missing-runner.err"; then
    echo 'FAIL: plain runner silently supplied numerical backend'; exit 1
fi
grep -q 'numerical backend unavailable' "$WORK/missing-runner.err"
for absent in bridge notorch; do
    if [ "$absent" = bridge ]; then archive=libaml_notorch.a; else archive=libnotorch.a; fi
    mv "$WORK/prefix/lib/$archive" "$WORK/$archive"
    "$ROOT/tools/amlc" "$WORK/missing.aml" --scalar -o "$WORK/missing-$absent" \
        > "$WORK/missing-build.out" 2> "$WORK/missing-build.err"
    if "$WORK/missing-$absent" > "$WORK/missing-$absent.out" 2> "$WORK/missing-$absent.err"; then
        echo "FAIL: incomplete prefix ($absent absent) silently supplied numerical backend"; exit 1
    fi
    grep -q 'numerical backend unavailable' "$WORK/missing-$absent.err"
    mv "$WORK/$archive" "$WORK/prefix/lib/$archive"
done
if grep -q WRONG "$WORK/missing-runner.out" "$WORK/missing-bridge.out" "$WORK/missing-notorch.out"; then
    echo 'FAIL: missing numerical backend continued execution'; exit 1
fi
echo 'PASS numerical isolation: standalone runner and incomplete prefixes report missing backend'

bad_cases=(
    'nt_linear([1], [0], [1], 2, 1)'
    'nt_linear([1], [0], [1], 1e39, 1)'
    'nt_linear([3e38], [0], [3e38], 1, 1)'
    'nt_linear_vjp([1], [1], [1])'
    'nt_tanh("x")'
    'nt_tanh([1e39])'
    'nt_tanh_vjp([2], [1])'
    'nt_mse_grad([1], [1, 2])'
    'nt_mse_grad([3e38], [-3e38])'
    'nt_sgd([1], [1], -1)'
    'rng_normal(rng, 0)'
    'rng_normal(map_new(), 1)'
    'rng_normal(rng, 1048577)'
    'isfinite([1])'
)
for bad in "${bad_cases[@]}"; do
    printf 'rng = rng_new(42)\n%s\nPRINT "WRONG_CONTINUATION"\n' "$bad" > "$WORK/bad.aml"
    if "$ROOT/runner/aml-notorch" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: numerical runner accepted $bad"; exit 1
    fi
    cat >> "$WORK/bad.aml" <<'EOF'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: compiled numerical accepted $bad"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: numerical continued after $bad"; exit 1
    fi
done
echo "PASS numerical errors: ${#bad_cases[@]} runner/compiler cases stop before continuation and C main"
