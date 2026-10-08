#!/usr/bin/env bash
# Standalone Unicode classification and line input through every execution path.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
INPUT_CC=${CC:-cc}
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

"$INPUT_CC" -O2 -Wall -Wextra -I"$ROOT/core" "$ROOT/tests/test_aml_text_input.c" \
    "$ROOT/libaml.a" -lm -lpthread -o "$WORK/input-api"
"$WORK/input-api"
if [ "$(uname -s)" = Linux ]; then
    "$INPUT_CC" -O2 -Wall -Wextra -DAML_INPUT_ALLOC_WRAP -I"$ROOT/core" \
        "$ROOT/tests/test_aml_text_input.c" "$ROOT/libaml.a" -lm -lpthread \
        -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o "$WORK/input-alloc"
    "$WORK/input-alloc"
else
    echo 'SKIP: line-input allocation injection requires linker --wrap'
fi
mkdir -p "$WORK/prefix/lib" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"
cat > "$WORK/values.aml" <<'EOF'
def next_line():
    return read_line()
first = next_line()
second = read_line()
third = READ_LINE()
ended = read_line()
PRINT first
PRINT second
PRINT third
PRINT ended
assert(list_len(first) == 1, 'blank line is present')
assert(list_len(ended) == 0, 'EOF is absent')
assert(codepoint_isalnum(0) == 0, 'scalar zero')
assert(CODEPOINT_ISALNUM(65) == 1, 'Latin')
assert(codepoint_isalnum(1488) == 1, 'Hebrew')
assert(codepoint_isalnum(178) == 1, 'superscript number')
assert(codepoint_isalnum(8544) == 1, 'Roman number')
assert(codepoint_isalnum(769) == 0, 'combining mark')
assert(codepoint_isalnum(888) == 0, 'unassigned scalar')
assert(codepoint_isalnum(124112) == 1, 'Unicode 15 Nag Mundari')
PRINT 'AML_TEXT_INPUT_VECTORS_OK'
EOF
printf '\nПривет, שלום\r\nlast 🌧' > "$WORK/input.txt"
printf '[""]\n["Привет, שלום\\r"]\n["last 🌧"]\n[]\nAML_TEXT_INPUT_VECTORS_OK\n' > "$WORK/expected.out"
"$ROOT/runner/aml" "$WORK/values.aml" < "$WORK/input.txt" > "$WORK/runner.out" 2> "$WORK/runner.err"
diff -u "$WORK/expected.out" "$WORK/runner.out"
test ! -s "$WORK/runner.err"
for mode in 0 1 2; do
    "$WORK/input-api" --run "$mode" "$WORK/values.aml" < "$WORK/input.txt" \
        > "$WORK/mode-$mode.out" 2> "$WORK/mode-$mode.err"
    diff -u "$WORK/expected.out" "$WORK/mode-$mode.out"
done
"$ROOT/tools/amlc" "$WORK/values.aml" --scalar -o "$WORK/input program" \
    > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/input program") < "$WORK/input.txt" \
    > "$WORK/compiled.out" 2> "$WORK/compiled.err"
diff -u "$WORK/expected.out" "$WORK/compiled.out"
echo 'PASS text input values: blank lines, CR, Unicode, EOF and properties match through five paths'

cat > "$WORK/prompt.aml" <<'EOF'
PRINT 'PROMPT_READY'
line = read_line()
PRINT list_get(line, 0)
EOF
"$ROOT/tools/amlc" "$WORK/prompt.aml" --scalar -o "$WORK/prompt" > "$WORK/prompt-build.out" 2> "$WORK/prompt-build.err"
"$WORK/input-api" --prompt "$ROOT/runner/aml" "$WORK/prompt.aml"
"$WORK/input-api" --prompt "$WORK/prompt"
echo 'PASS line-input prompts: pipe reader observes output before supplying input'

bad_cases=(
    'read_line(1)' 'codepoint_isalnum()' 'codepoint_isalnum("A")'
    'codepoint_isalnum([65])' 'codepoint_isalnum(65, 66)'
    'codepoint_isalnum(-1)' 'codepoint_isalnum(0.5)'
    'codepoint_isalnum(55296)' 'codepoint_isalnum(1114112)'
    'codepoint_isalnum(1e39)' 'codepoint_isalnum(1e39 - 1e39)'
)
for bad in "${bad_cases[@]}"; do
    printf '%s\nPRINT "WRONG_CONTINUATION"\n' "$bad" > "$WORK/bad.aml"
    if "$ROOT/runner/aml" "$WORK/bad.aml" < /dev/null > "$WORK/bad-runner.out" 2> "$WORK/bad-runner.err"; then
        echo "FAIL: runner accepted $bad"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/input-api" --run "$mode" "$WORK/bad.aml" < /dev/null \
            > "$WORK/bad-$mode.out" 2> "$WORK/bad-$mode.err"; then
            echo "FAIL: runtime $mode accepted $bad"; exit 1
        fi
    done
    cat >> "$WORK/bad.aml" <<'EOF'
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
    "$ROOT/tools/amlc" "$WORK/bad.aml" --scalar -o "$WORK/bad" > "$WORK/bad-build.out" 2> "$WORK/bad-build.err"
    if "$WORK/bad" < /dev/null > "$WORK/bad-compiled.out" 2> "$WORK/bad-compiled.err"; then
        echo "FAIL: compiled input accepted $bad"; exit 1
    fi
    if grep -q WRONG "$WORK/bad-runner.out" "$WORK/bad-0.out" "$WORK/bad-1.out" "$WORK/bad-2.out" "$WORK/bad-compiled.out"; then
        echo "FAIL: input continued after $bad"; exit 1
    fi
done
cat > "$WORK/read.aml" <<'EOF'
line = read_line()
PRINT 'WRONG_CONTINUATION'
EOF
"$ROOT/tools/amlc" "$WORK/read.aml" --scalar -o "$WORK/read" > "$WORK/read-build.out" 2> "$WORK/read-build.err"
printf 'a\0b\n' > "$WORK/nul.txt"
printf '\360\237\n' > "$WORK/truncated.txt"
printf '\355\240\200\n' > "$WORK/surrogate.txt"
dd if=/dev/zero bs=1048577 count=1 2>/dev/null | tr '\0' x > "$WORK/oversized.txt"
for invalid in nul truncated surrogate oversized; do
    if "$ROOT/runner/aml" "$WORK/read.aml" < "$WORK/$invalid.txt" > "$WORK/read-runner.out" 2> "$WORK/read-runner.err"; then
        echo "FAIL: runner accepted $invalid input"; exit 1
    fi
    for mode in 0 1 2; do
        if "$WORK/input-api" --run "$mode" "$WORK/read.aml" < "$WORK/$invalid.txt" \
            > "$WORK/read-$mode.out" 2> "$WORK/read-$mode.err"; then
            echo "FAIL: runtime $mode accepted $invalid input"; exit 1
        fi
    done
    if "$WORK/read" < "$WORK/$invalid.txt" > "$WORK/read-compiled.out" 2> "$WORK/read-compiled.err"; then
        echo "FAIL: compiled program accepted $invalid input"; exit 1
    fi
    if grep -q WRONG "$WORK/read-runner.out" "$WORK/read-0.out" "$WORK/read-1.out" "$WORK/read-2.out" "$WORK/read-compiled.out"; then
        echo "FAIL: continued after $invalid input"; exit 1
    fi
done
echo "PASS text input errors: ${#bad_cases[@]} invalid calls and four invalid streams through five paths"
