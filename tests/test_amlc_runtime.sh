#!/usr/bin/env bash
# Compile the same Level 2 program that the interpreter executes.
# Optional first argument: an older compiler, to reproduce the original defect.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ "$#" -gt 0 ]; then AMLC=$1; else AMLC="$ROOT/tools/amlc"; fi
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

cat > "$WORK/control.aml" <<'EOF'
x = 0
if x == 1:
    ECHO WRONG_BRANCH
else:
    ECHO BRANCH_OK

def warm(d):
    return 0.3 + d * 1.2

temperature = warm(0.5)
if abs(temperature - 0.9) < 0.00001:
    ECHO FUNCTION_OK
else:
    ECHO WRONG_FUNCTION

i = 0
total = 0
while i < 3:
    total = total + i
    i = i + 1
if total == 3:
    ECHO LOOP_OK
else:
    ECHO WRONG_LOOP

weights = [1, 2, 3]
scaled = scale(weights, 2)
amount = sum(scaled)
if amount == 12:
    ECHO ARRAY_OK
else:
    ECHO WRONG_ARRAY

CHANNEL CREATE haiku_events 2
SPAWN reflection:
    CHANNEL WRITE haiku_events 0.7
AWAIT reflection
CHANNEL READ haiku_events value
if abs(value - 0.7) < 0.00001:
    ECHO ASYNC_OK
else:
    ECHO WRONG_ASYNC
EOF

# Explicit scalar linking keeps OpenBLAS out of this correctness gate.
compile_program() {
    local name=$1
    "$AMLC" "$WORK/$name.aml" --emit-c >"$WORK/$name.c" 2>"$WORK/$name.emit.log"
    cc "$WORK/$name.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/$name"
}

"$ROOT/runner/aml" "$WORK/control.aml" >"$WORK/interpreted.out" 2>"$WORK/interpreted.err"
compile_program control
"$WORK/control" >"$WORK/compiled.out" 2>"$WORK/compiled.err"
cat > "$WORK/expected.out" <<'EOF'
[AML] BRANCH_OK
[AML] FUNCTION_OK
[AML] LOOP_OK
[AML] ARRAY_OK
[AML] ASYNC_OK
EOF
diff -u "$WORK/expected.out" "$WORK/interpreted.out"
diff -u "$WORK/interpreted.out" "$WORK/compiled.out"
echo "PASS: compiled/interpreted branch, function, loop, array, and async behavior"

# A runtime error must stop the compiled program before its C main is entered.
cat > "$WORK/error.aml" <<'EOF'
def recurse():
    recurse()
recurse()
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_MAIN_AFTER_ERROR"); return 0; }
}
EOF
compile_program error
if "$WORK/error" >"$WORK/error.out" 2>"$WORK/error.err"; then
    echo "FAIL: compiled program discarded a runtime error"
    exit 1
fi
grep -q "max call depth exceeded" "$WORK/error.err"
if grep -q "WRONG_MAIN_AFTER_ERROR" "$WORK/error.out"; then
    echo "FAIL: C main ran after runtime failure"
    exit 1
fi
echo "PASS: runtime errors terminate before C main"

# Mixed BLOOD/AML programs retain field initialization before C main.
cat > "$WORK/mixed.aml" <<'EOF'
base = 0.2
if base < 0.5:
    PAIN base + 0.1
else:
    PAIN 0.9
BLOOD COMPILE mixed {
#include <math.h>
#include <stdio.h>
#include "ariannamethod.h"
}
BLOOD MAIN {
int main(void) {
    if (fabsf(am_get_state()->pain - 0.3f) > 0.00001f) return 1;
    puts("MIXED_OK");
    return 0;
}
}
EOF
"$AMLC" "$WORK/mixed.aml" --emit-c >"$WORK/mixed.c" 2>"$WORK/mixed.emit.log"
cc -I"$ROOT/core" "$WORK/mixed.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/mixed"
"$WORK/mixed" >"$WORK/mixed.out" 2>"$WORK/mixed.err"
grep -qx "MIXED_OK" "$WORK/mixed.out"
echo "PASS: mixed BLOOD main observes the completed AML program"

# Runtime lines are retained in a bounded slot; reject instead of shortening.
{ printf 'ECHO '; printf '%0600d\n' 0; } >"$WORK/long.aml"
if "$AMLC" "$WORK/long.aml" --emit-c >"$WORK/long.c" 2>"$WORK/long.err"; then
    echo "FAIL: oversized runtime line was accepted"
    exit 1
fi
grep -q "AML line exceeds" "$WORK/long.err"
echo "PASS: oversized runtime source is rejected"
