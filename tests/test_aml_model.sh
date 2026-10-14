#!/usr/bin/env bash
# Immutable model ownership, canonical PEN7, and interpreter/compiler parity.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL_CC=${CC:-cc}
MODEL_NOTORCH_ROOT=${NOTORCH_ROOT:-"$ROOT/../notorch"}
MODEL_NOTORCH_INCLUDE=${NOTORCH_INCLUDE:-"$MODEL_NOTORCH_ROOT"}
MODEL_NOTORCH_LIB=${NOTORCH_LIB:-"$MODEL_NOTORCH_ROOT/libnotorch.a"}
read -r -a MODEL_LINK_FLAGS <<< "${NOTORCH_LDFLAGS:-}"
make -C "$ROOT" notorch NOTORCH_INCLUDE="$MODEL_NOTORCH_INCLUDE" NOTORCH_LIB="$MODEL_NOTORCH_LIB" NOTORCH_LDFLAGS="${NOTORCH_LDFLAGS:-}" >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/module" "$WORK/elsewhere" "$WORK/prefix/lib" "$WORK/prefix/include/ariannamethod"
"$MODEL_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_model_api.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/model-api"
"$WORK/model-api"
if [ "$(uname -s)" = Linux ]; then
    "$MODEL_CC" -O2 -Wall -Wextra -DAML_MODEL_ALLOC_WRAP -I"$ROOT/core" "$ROOT/tests/test_aml_model_api.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$WORK/model-alloc"
    "$WORK/model-alloc"
fi
if [ -n "${MODEL_FIXTURE_DIR:-}" ]; then
    cp "$MODEL_FIXTURE_DIR/a.gguf" "$MODEL_FIXTURE_DIR/b.gguf" "$MODEL_FIXTURE_DIR/m.gguf" "$WORK/module/"
else
    make -C "$MODEL_NOTORCH_ROOT" test_pen7 BLAS_FLAGS= BLAS_LIBS= X86_SIMD=0 ARM_SIMD=0 > "$WORK/fixture-build.log" 2>&1
    "$MODEL_NOTORCH_ROOT/test_pen7" --fixtures "$WORK/module"
fi
printf 'corrupt PEN7 file' > "$WORK/module/corrupt.gguf"
"$MODEL_CC" -O2 -Wall -Wextra -I"$ROOT/core" -I"$MODEL_NOTORCH_INCLUDE" "$ROOT/tests/test_aml_model.c" \
    "$ROOT/libaml_notorch.a" "$ROOT/libaml.a" "$MODEL_NOTORCH_LIB" -lm -lpthread "${MODEL_LINK_FLAGS[@]}" -o "$WORK/model-native"
"$WORK/model-native" --native "$WORK/module/a.gguf"
if [ -n "${MODEL_REAL_PATH:-}" ]; then "$WORK/model-native" --native "$MODEL_REAL_PATH"; fi
cp "$ROOT/libaml.a" "$ROOT/libaml_notorch.a" "$MODEL_NOTORCH_LIB" "$WORK/prefix/lib/"
cp "$ROOT/core/ariannamethod.h" "$WORK/prefix/include/ariannamethod/"
export AML_PREFIX="$WORK/prefix"
cat > "$WORK/module/bodies.aml" <<'AML'
def body_a():
    return model_try_load('a.gguf')
def body_b():
    return model_try_load('b.gguf')
def body_missing():
    return model_try_load('absent.gguf')
def body_corrupt():
    return model_try_load('corrupt.gguf')
def keep_sixteen(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p):
    return p
AML
printf 'IMPORT "%s/module/bodies.aml"\n' "$WORK" > "$WORK/values.aml"
cat >> "$WORK/values.aml" <<'AML'
a=body_a()
b=body_b()
assert(model_ready(a)==1,'first body')
assert(model_ready(b)==1,'second body')
meta=model_info(a)
assert(record_get(meta,'vocab')==260,'vocab')
assert(record_get(meta,'width')==8,'width')
assert(text_len(model_identity(a))==64,'body digest')
assert(text_equal(model_tokenizer_identity(a),model_tokenizer_identity(b)),'tokenizer digest')
assert(text_equal(model_identity(a),model_identity(b))==0,'two distinct bodies')
ids=model_encode(a,' AAA ',40)
assert(len(ids)==1,'ranked BPE')
assert(ids[0]==259,'BPE rank')
assert(text_equal(model_piece(a,259),' aaa '),'piece')
assert(text_equal(model_piece(a,0),''),'NUL surface')
assert(text_equal(model_piece(a,195),''),'UTF-8 fragment surface')
ra=model_forward(a,ids)
rb=model_forward(b,ids)
rm=model_mix_forward(a,b,0.5,ids)
assert(record_get(ra,'ok')==1,'A forward')
assert(record_get(rb,'ok')==1,'B forward')
assert(record_get(rm,'ok')==1,'mixed forward')
assert(len(record_get(rm,'logits'))==260,'logits')
assert(len(record_get(rm,'hidden'))==8,'hidden')
assert(record_equal(ra,model_mix_forward(a,b,1,ids)),'exact A endpoint')
assert(record_equal(rb,model_mix_forward(a,b,0,ids)),'exact B endpoint')
absent=body_missing()
corrupt=body_corrupt()
assert(model_ready(absent)==0,'absent quiet')
assert(model_ready(corrupt)==0,'corrupt quiet')
assert(text_len(model_error(corrupt))>0,'load outcome')
f=model_forward(absent,ids)
assert(record_get(f,'ok')==0,'zero sources outcome')
assert(record_has(f,'logits')==0,'zero partial logits')
f=model_mix_forward(a,absent,0.5,ids)
assert(record_get(f,'ok')==0,'one absent outcome')
assert(record_has(f,'hidden')==0,'zero partial hidden')
f=model_forward(a,[1.5])
assert(record_get(f,'ok')==0,'fractional ids refuse')
f=model_mix_forward(a,b,2,ids)
assert(record_get(f,'ok')==0,'alpha refuse')
CHANNEL CREATE gate 2
SPAWN carried:
    CHANNEL READ gate start
    return keep_sixteen(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,a)
a=0
CHANNEL WRITE gate 1
AWAIT carried INTO a
assert(model_ready(a)==1,'worker retained model')
turn=0
while turn<3:
    SPAWN pass:
        return model_forward(a,ids)
    AWAIT pass INTO received
    assert(record_equal(received,ra),'worker exact forward')
    turn=turn+1
assert(turn==3,'completed every worker')
PRINT a
PRINT 'AML_MODEL_VALUES_OK'
AML
printf '<model>\nAML_MODEL_VALUES_OK\n' > "$WORK/expected.out"
(cd "$WORK/elsewhere" && "$ROOT/runner/aml-notorch" "$WORK/values.aml") > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
test ! -s "$WORK/runner.err"
for mode in 0 1 2; do
    (cd "$WORK/elsewhere" && "$WORK/model-native" --run "$mode" "$WORK/values.aml") > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
    test ! -s "$WORK/mode-$mode.err"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/compiled" > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/compiled") > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
test ! -s "$WORK/compiled.err"
echo 'PASS models: zero/one/two sources, source-relative BPE, exact mixture endpoints, sixteen typed arguments, worker transfers in five paths'

bad_cases=(
    'model_try_load(1)' 'model_ready(1)' 'model_piece(a,-1)' 'model_encode(a,"",4)'
    'model_encode(a,"aaaa",1)' 'model_encode(a,"rain",41)' 'model_forward(a,1)'
    'model_mix_forward(a,a,"invalid",[1])' 'record_set(record_new(),"body",a)'
    'len(a)' 'nt_tanh(a)' 'sink = a + 1' 'sink = a[0]'
    'keep_sixteen(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17)'
)
for bad in "${bad_cases[@]}"; do
    printf 'IMPORT "%s/module/bodies.aml"\na=body_a()\n%s\nPRINT "WRONG_CONTINUATION"\n' "$WORK" "$bad" > "$WORK/bad.aml"
    if "$ROOT/runner/aml-notorch" "$WORK/bad.aml" > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL model runner accepted: $bad"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/model-native" --run "$mode" "$WORK/bad.aml" > "$WORK/bad-$mode.out" 2> "$WORK/bad-$mode.err"; then
            echo "FAIL model host $mode accepted: $bad"; exit 1
        fi
    done
    cat >> "$WORK/bad.aml" <<'AML'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
AML
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL compiled model accepted: $bad"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-0.out" "$WORK/bad-1.out" "$WORK/bad-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL model continued after: $bad"; exit 1
    fi
done
echo "PASS models: ${#bad_cases[@]} typed/encoding/record failures preserve execution boundary in five paths"
cat > "$WORK/unavailable.aml" <<'AML'
a=model_try_load('')
assert(model_ready(a)==0,'backend absence is quiet')
assert(text_len(model_error(a))>0,'explicit unavailable result')
r=model_forward(a,[1])
assert(record_get(r,'ok')==0,'quiet forward outcome')
PRINT 'AML_MODEL_UNAVAILABLE_OK'
AML
"$ROOT/runner/aml" "$WORK/unavailable.aml" > "$WORK/unavailable.out" 2> "$WORK/unavailable.err"
grep -qx 'AML_MODEL_UNAVAILABLE_OK' "$WORK/unavailable.out"
test ! -s "$WORK/unavailable.err"
mv "$WORK/prefix/lib/libaml_notorch.a" "$WORK/bridge.a"
"$ROOT/tools/amlc" "$WORK/unavailable.aml" --scalar -o "$WORK/unavailable" > "$WORK/unavailable-build.out" 2> "$WORK/unavailable-build.err"
"$WORK/unavailable" > "$WORK/unavailable-compiled.out" 2> "$WORK/unavailable-compiled.err"
grep -qx 'AML_MODEL_UNAVAILABLE_OK' "$WORK/unavailable-compiled.out"
test ! -s "$WORK/unavailable-compiled.err"
echo 'PASS model absence: standalone runner and incomplete prefix stay quiet with explicit outcomes'
