#!/usr/bin/env bash
# tests/test_amlc.sh — sanity test for the amlc transpiler.
#
# 1. `make amlc` builds tools/amlc from tools/amlc.c.
# 2. The binary parses a small synthetic .aml with two BLOOD COMPILE
#    blocks, one BLOOD MAIN, one BLOOD LINK, and one ECHO directive,
#    then compiles the generated C and runs it.
# 3. Asserts the emitted program prints the expected marker.
#
# This guards the happy-path syntax tree of amlc — brace tracker,
# directive recogniser, ECHO injection, accel auto-link.

set -euo pipefail
cd "$(dirname "$0")/.."

echo "== test_amlc =="

make amlc >/tmp/amlc_build.log 2>&1
if [ ! -x tools/amlc ]; then
    echo "  FAIL: make amlc did not produce tools/amlc"
    cat /tmp/amlc_build.log
    exit 1
fi
echo "  PASS [build]: tools/amlc"

# ── Synthetic .aml + helper header ──────────────────────────────────────
WORK=$(mktemp -d)
trap "rm -rf '$WORK'" EXIT

cat > "$WORK/test.aml" <<'EOF'
# tiny smoke .aml — exercises BLOOD COMPILE / BLOOD MAIN / BLOOD LINK / ECHO

BLOOD INCLUDE "test_echo.h"

BLOOD COMPILE smoke_helpers {
#include <stdio.h>
static void say(const char *s) { printf("%s\n", s); }
}

BLOOD COMPILE smoke_logic {
static int compute(int x) {
    if (x > 0) { return x * 2; } else { return -1; }
}
}

BLOOD LINK -lm

BLOOD MAIN {
int main(void) {
    say("AMLC_OK");
    int v = compute(21);
    if (v != 42) { say("FAIL_COMPUTE"); return 1; }
    say(ECHO_MARKER);
    return 0;
}
}
EOF

cat > "$WORK/test_echo.h" <<'EOF'
#define ECHO_MARKER "ECHO_OK"
EOF

# ── Compile + run ───────────────────────────────────────────────────────
cd "$WORK"
"$OLDPWD/tools/amlc" test.aml -o test_smoke --no-accel >/tmp/amlc_compile.log 2>&1
if [ ! -x test_smoke ]; then
    echo "  FAIL: amlc did not emit test_smoke binary"
    cat /tmp/amlc_compile.log
    exit 1
fi
echo "  PASS [compile]: test.aml → test_smoke"

OUT=$(./test_smoke)
EXPECTED=$'AMLC_OK\nECHO_OK'
if [ "$OUT" = "$EXPECTED" ]; then
    echo "  PASS [run]: emitted output matches"
else
    echo "  FAIL: output mismatch"
    echo "  expected: $EXPECTED"
    echo "  got:      $OUT"
    exit 1
fi

# ── A-1 conformance: every top-level directive lowers, case-insensitive ──
#    (Mythos audit A-1/A-2: amlc used to lower only 7 whitelisted names and
#     drop the rest; matching was case-sensitive. Guard against regression.)
cat > directives.aml <<'EOF'
PROPHECY 7
PAIN 0.3
LAW ENTROPY_FLOOR 0.1
EXPERT_STRUCTURAL 0.25
gamma 0.5
EOF
"$OLDPWD/tools/amlc" --emit-c directives.aml >dir.c 2>dir.err || true
N_LOWERED=$(grep -Fc ' "\n"' dir.c || true)
N_DROPPED=$(grep -c 'unknown directive' dir.err || true)
if [ "$N_LOWERED" = "5" ] && [ "$N_DROPPED" = "0" ]; then
    echo "  PASS [A-1]: 5/5 directives lowered, 0 dropped (incl. lowercase 'gamma')"
else
    echo "  FAIL [A-1]: lowered=$N_LOWERED (want 5), dropped=$N_DROPPED (want 0)"
    exit 1
fi

# ── A-3 conformance: a one-line BLOOD COMPILE block keeps its body ───────
#    (Mythos audit A-3a: the opener-line body used to be discarded, so the
#     canonical one-liner failed to compile with "unexpected EOF".)
cat > oneline.aml <<'EOF'
BLOOD COMPILE oneliner { static int f(void) { return 42; } }
BLOOD MAIN { int main(void){ return f()==42?0:1; } }
EOF
if "$OLDPWD/tools/amlc" --emit-c oneline.aml >oneline.c 2>oneline.err \
   && grep -q 'static int f(void)' oneline.c; then
    echo "  PASS [A-3]: one-line BLOOD block body captured"
else
    echo "  FAIL [A-3]: one-line BLOOD body lost"
    cat oneline.err
    exit 1
fi

# ── A-5 conformance: ECHO logs (not #include); BLOOD INCLUDE injects #include ──
#    (Mythos audit A-5: ECHO used to be lowered to a C #include; the spec defines
#     it as console logging, and BLOOD INCLUDE is the explicit header directive.)
cat > a5.aml <<'EOF'
ECHO startup message
BLOOD INCLUDE "stdio.h"
EOF
"$OLDPWD/tools/amlc" --emit-c a5.aml >a5.c 2>/dev/null || true
if grep -q '"ECHO startup message"' a5.c \
   && grep -q '#include "stdio.h"' a5.c \
   && ! grep -q '#include "ECHO' a5.c; then
    echo "  PASS [A-5]: ECHO → am_exec (log); BLOOD INCLUDE → #include"
else
    echo "  FAIL [A-5]: ECHO/BLOOD INCLUDE lowering wrong"
    exit 1
fi

echo
echo "amlc OK"
