#!/usr/bin/env bash
# Immutable values, native Unigram pieces and source origins in five paths.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
TOKENIZER_CC=${CC:-cc}
TOKENIZER_NTORCH_ROOT=${NOTORCH_ROOT:-"$ROOT/../notorch"}
TOKENIZER_NTORCH_INCLUDE=${NOTORCH_INCLUDE:-"$TOKENIZER_NTORCH_ROOT"}
TOKENIZER_NTORCH_LIB=${NOTORCH_LIB:-"$TOKENIZER_NTORCH_ROOT/libnotorch.a"}
read -r -a TOKENIZER_LINK_FLAGS <<< "${NOTORCH_LDFLAGS:-}"
make -C "$ROOT" notorch NOTORCH_INCLUDE="$TOKENIZER_NTORCH_INCLUDE" \
    NOTORCH_LIB="$TOKENIZER_NTORCH_LIB" NOTORCH_LDFLAGS="${NOTORCH_LDFLAGS:-}" >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$TOKENIZER_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_tokenizer_api.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/tokenizer-api"
"$WORK/tokenizer-api"
if [ "$(uname -s)" = Linux ]; then
    "$TOKENIZER_CC" -O2 -Wall -Wextra -DAML_TOKENIZER_ALLOC_WRAP -I"$ROOT/core" \
        "$ROOT/tests/test_aml_tokenizer_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$WORK/tokenizer-alloc"
    "$WORK/tokenizer-alloc"
else
    echo 'SKIP: tokenizer allocation injection requires linker --wrap'
fi
"$TOKENIZER_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_tokenizer.c" \
    "$ROOT/libaml_notorch.a" "$ROOT/libaml.a" "$TOKENIZER_NTORCH_LIB" \
    -lm -lpthread "${TOKENIZER_LINK_FLAGS[@]}" -o "$WORK/tokenizer-runtime"
mkdir -p "$WORK/module" "$WORK/prefix/lib" "$WORK/prefix/include/ariannamethod" "$WORK/elsewhere"
"$WORK/tokenizer-runtime" --write-model "$WORK/module/tiny.model"
"$WORK/tokenizer-runtime" --write-unsupported "$WORK/unsupported.model"
"$WORK/tokenizer-runtime" --test-model "$WORK/module/tiny.model"
printf '\012\377\377' > "$WORK/corrupt.model"
cp "$ROOT/libaml.a" "$ROOT/libaml_notorch.a" "$TOKENIZER_NTORCH_LIB" "$WORK/prefix/lib/"
cp "$ROOT/core/ariannamethod.h" "$WORK/prefix/include/ariannamethod/"
export AML_PREFIX="$WORK/prefix"
cat > "$WORK/module/hearing.aml" <<'EOF'
def load_words():
    return tokenizer_load('tiny.model')
def preserve_words(model):
    return model
EOF
printf 'IMPORT "%s/module/hearing.aml"\n' "$WORK" > "$WORK/values.aml"
cat >> "$WORK/values.aml" <<'EOF'
model = load_words()
alias = preserve_words(model)
model = 0
PRINT alias
PRINT tokenizer_identity(alias)
PRINT tokenizer_pieces(alias, 'aaa')
PRINT tokenizer_pieces(alias, '⚡👀abéà_')
PRINT tokenizer_pieces(alias, '')
PRINT tokenizer_pieces(alias, 'aa')
PRINT tokenizer_pieces(alias, '  a  ')
pieces = tokenizer_pieces(alias, 'a')
copy = pieces
list_set(copy, 0, 'changed')
assert(text_equal(list_get(pieces, 0), 'a'), 'piece container owns its mutation')
rng = rng_new(42)
assert(rng_uniform(rng) >= 0, 'sampling remains registered')
activated = nt_tanh([0])
assert(activated[0] == 0, 'numerical remains registered')
PRINT 'AML_TOKENIZER_VECTORS_OK'
EOF
cat > "$WORK/expected.out" <<'EOF'
<tokenizer>
fc30e3ec6d1e9014281c11d2c3fc4970e26137d9ca8015f6ae606eeb20fb493a
["a", "aa"]
["⚡👀", "a", "béà_"]
[]
["aa"]
["  ", "a", "  "]
AML_TOKENIZER_VECTORS_OK
EOF
(cd "$WORK/elsewhere" && "$ROOT/runner/aml-notorch" "$WORK/values.aml") > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
test ! -s "$WORK/runner.err"
for mode in 0 1 2; do
    (cd "$WORK/elsewhere" && "$WORK/tokenizer-runtime" --run "$mode" "$WORK/values.aml") \
        > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/tokenizer program" > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/tokenizer program") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
test ! -s "$WORK/compiled.err"
echo 'PASS tokenizer vectors: tie order, unknown surfaces, immutable aliases and imported model paths through five paths'

bad_cases=(
    'tokenizer_load()' 'tokenizer_load(1)' 'tokenizer_load([1])'
    'tokenizer_pieces(model)' 'tokenizer_pieces(model, 1)' 'tokenizer_pieces(1, "a")'
    'tokenizer_pieces(model, "a", "b")' 'nt_tanh(model)' 'text_len(model)'
    'len(model)' 'result = model + 1' 'codepoint_isalnum(model)'
    'tokenizer_identity()' 'tokenizer_identity(1)' 'tokenizer_identity(model, 1)'
    "tokenizer_load('$WORK/missing.model')" "tokenizer_load('$WORK/corrupt.model')"
    "tokenizer_load('$WORK/unsupported.model')"
)
for bad in "${bad_cases[@]}"; do
    printf 'model = tokenizer_load("%s/module/tiny.model")\n%s\nPRINT "WRONG_CONTINUATION"\n' "$WORK" "$bad" > "$WORK/bad.aml"
    if "$ROOT/runner/aml-notorch" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: tokenizer runner accepted $bad"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/tokenizer-runtime" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-$mode.out" 2> "$WORK/bad-$mode.err"; then
            echo "FAIL: tokenizer mode $mode accepted $bad"; exit 1
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
        echo "FAIL: compiled tokenizer accepted $bad"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-0.out" "$WORK/bad-1.out" "$WORK/bad-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: tokenizer continued after $bad"; exit 1
    fi
done
echo "PASS tokenizer errors: ${#bad_cases[@]} malformed calls/models stop in all five paths"

printf 'model = tokenizer_load("%s/module/tiny.model")\nPRINT "WRONG_CONTINUATION"\n' "$WORK" > "$WORK/missing.aml"
if "$ROOT/runner/aml" "$WORK/missing.aml" > "$WORK/missing.out" 2> "$WORK/missing.err"; then
    echo 'FAIL: standalone runner silently supplied tokenizer'; exit 1
fi
grep -q 'tokenizer backend unavailable' "$WORK/missing.err"
mv "$WORK/prefix/lib/libaml_notorch.a" "$WORK/bridge.a"
"$ROOT/tools/amlc" "$WORK/missing.aml" --scalar -o "$WORK/missing" > "$WORK/missing-build.out" 2> "$WORK/missing-build.err"
if "$WORK/missing" > "$WORK/missing-compiled.out" 2> "$WORK/missing-compiled.err"; then
    echo 'FAIL: incomplete prefix silently supplied tokenizer'; exit 1
fi
grep -q 'tokenizer backend unavailable' "$WORK/missing-compiled.err"
if grep -q WRONG "$WORK/missing.out" "$WORK/missing-compiled.out"; then
    echo 'FAIL: missing tokenizer backend continued'; exit 1
fi
echo 'PASS tokenizer registration: standalone runner and incomplete prefix report missing backend'
