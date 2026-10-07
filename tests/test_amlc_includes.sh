#!/usr/bin/env bash
# File origins, nested failures, scalar linking, and CLI status propagation.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
if [ "$#" -gt 0 ]; then AMLC=$1; else AMLC="$ROOT/tools/amlc"; fi
make -C "$ROOT" all >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/source dir/nested" "$WORK/elsewhere" "$WORK/prefix/lib"
cp "$ROOT/libaml.a" "$WORK/prefix/lib/"
export AML_PREFIX="$WORK/prefix"

cat > "$WORK/source dir/main.aml" <<'EOF'
INCLUDE "child.aml"
ECHO PARENT_OK
EOF
cat > "$WORK/source dir/child.aml" <<'EOF'
ECHO CHILD_OK
INCLUDE nested/grandchild.aml
EOF
cat > "$WORK/source dir/nested/grandchild.aml" <<'EOF'
ECHO GRANDCHILD_OK
EOF

# The unmodified compiler can use --emit-c, so the origin regression is also
# runnable against the pre-fix compiler without relying on its CLI options.
"$AMLC" "$WORK/source dir/main.aml" --emit-c > "$WORK/main.c" 2> "$WORK/emit.log"
cc "$WORK/main.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/elsewhere/main"
cd "$WORK/elsewhere"
"$ROOT/runner/aml" "$WORK/source dir/main.aml" > "$WORK/interpreted.out" 2> "$WORK/interpreted.err"
if ! ./main > "$WORK/compiled.out" 2> "$WORK/compiled.err"; then
    cat "$WORK/compiled.err"
    exit 1
fi
printf '[AML] CHILD_OK\n[AML] GRANDCHILD_OK\n[AML] PARENT_OK\n' > "$WORK/expected.out"
diff -u "$WORK/expected.out" "$WORK/interpreted.out"
diff -u "$WORK/interpreted.out" "$WORK/compiled.out"
rm "$WORK/source dir/main.aml"
./main > "$WORK/embedded.out" 2> "$WORK/embedded.err"
diff -u "$WORK/expected.out" "$WORK/embedded.out"
echo 'PASS: nested relative/quoted includes resolve from source, with main source removed'

cat > "$WORK/source dir/nested/missing.aml" <<'EOF'
INCLUDE absent.aml
ECHO WRONG_CHILD_CONTINUATION
EOF
cat > "$WORK/source dir/failure.aml" <<'EOF'
INCLUDE nested/missing.aml
ECHO WRONG_PARENT_CONTINUATION
BLOOD MAIN {
#include <stdio.h>
int main(void) { puts("WRONG_C_MAIN"); return 0; }
}
EOF
"$AMLC" "$WORK/source dir/failure.aml" --scalar -o "$WORK/failing binary" > "$WORK/build.out" 2> "$WORK/build.err"
if "$WORK/failing binary" > "$WORK/fail.out" 2> "$WORK/fail.err"; then
    echo 'FAIL: missing nested include reported success'; exit 1
fi
grep -q 'cannot open:.*absent.aml' "$WORK/fail.err"
if grep -q WRONG "$WORK/fail.out"; then cat "$WORK/fail.out"; exit 1; fi
if "$AMLC" "$WORK/source dir/failure.aml" --scalar -o "$WORK/failing binary" --run > "$WORK/run.out" 2> "$WORK/run.err"; then
    echo 'FAIL: --run discarded the program failure'; exit 1
fi
grep -q 'cannot open:.*absent.aml' "$WORK/run.err"
echo 'PASS: nested missing file stops parent and C main; --run returns failure'

cat > "$WORK/source dir/nested/broken.aml" <<'EOF'
def recurse():
    recurse()
recurse()
ECHO WRONG_AFTER_RECURSION
EOF
cat > "$WORK/source dir/runtime-error.aml" <<'EOF'
INCLUDE nested/broken.aml
ECHO WRONG_AFTER_INCLUDE
EOF
if "$ROOT/runner/aml" "$WORK/source dir/runtime-error.aml" > "$WORK/error-interpreted.out" 2> "$WORK/error-interpreted.err"; then
    echo 'FAIL: interpreter swallowed child error'; exit 1
fi
"$AMLC" "$WORK/source dir/runtime-error.aml" --scalar -o "$WORK/error" > /dev/null 2> "$WORK/error-build.err"
if "$WORK/error" > "$WORK/error.out" 2> "$WORK/error.err"; then
    echo 'FAIL: compiler swallowed child error'; exit 1
fi
grep -q 'max call depth exceeded' "$WORK/error.err"
if grep -q WRONG "$WORK/error.out" "$WORK/error-interpreted.out"; then exit 1; fi
echo 'PASS: child execution failures stop both execution paths'

cat > "$WORK/source dir/cycle.aml" <<'EOF'
INCLUDE cycle.aml
ECHO WRONG_AFTER_CYCLE
EOF
"$AMLC" "$WORK/source dir/cycle.aml" --scalar -o "$WORK/cycle" > /dev/null 2> "$WORK/cycle-build.err"
if "$WORK/cycle" > "$WORK/cycle.out" 2> "$WORK/cycle.err"; then exit 1; fi
grep -q 'max include depth' "$WORK/cycle.err"
if grep -q WRONG "$WORK/cycle.out"; then exit 1; fi
echo 'PASS: include cycles report failure'

cat > "$WORK/source dir/worker.aml" <<'EOF'
CHANNEL CREATE worker_events 2
SPAWN dream:
    if 0:
        CHANNEL WRITE worker_events 0
    else:
        INCLUDE nested/worker-child.aml
AWAIT dream
CHANNEL READ worker_events observed
if observed == 0.75:
    ECHO WORKER_ORIGIN_OK
else:
    ECHO WRONG_WORKER_ORIGIN
EOF
cat > "$WORK/source dir/nested/worker-child.aml" <<'EOF'
CHANNEL WRITE worker_events 0.75
EOF
"$AMLC" "$WORK/source dir/worker.aml" --scalar -o "$WORK/worker" --run > "$WORK/worker.out" 2> "$WORK/worker.err"
printf '[AML] WORKER_ORIGIN_OK\n' > "$WORK/worker.expected"
diff -u "$WORK/worker.expected" "$WORK/worker.out"
echo 'PASS: spawned blocks retain indentation and inherit the source directory'

for haiku_wait in 'AWAIT bad' 'AWAIT'; do
    cat > "$WORK/source dir/worker-error.aml" <<'EOF'
SPAWN bad:
    INCLUDE nested/missing.aml
EOF
    printf '%s\n' "$haiku_wait" >> "$WORK/source dir/worker-error.aml"
    cat >> "$WORK/source dir/worker-error.aml" <<'EOF'
ECHO WRONG_AFTER_WORKER_ERROR
EOF
    if "$ROOT/runner/aml" "$WORK/source dir/worker-error.aml" > "$WORK/worker-error.out" 2> "$WORK/worker-error.err"; then
        echo 'FAIL: AWAIT ignored worker failure'; exit 1
    fi
    grep -q 'cannot open:.*absent.aml' "$WORK/worker-error.err"
    if grep -q WRONG "$WORK/worker-error.out"; then exit 1; fi
    if "$AMLC" "$WORK/source dir/worker-error.aml" --scalar -o "$WORK/worker-error" --run > "$WORK/worker-error.out" 2> "$WORK/worker-error.err"; then
        echo 'FAIL: compiled AWAIT ignored worker failure'; exit 1
    fi
    grep -q 'cannot open:.*absent.aml' "$WORK/worker-error.err"
    if grep -q WRONG "$WORK/worker-error.out"; then exit 1; fi
done
echo 'PASS: named AWAIT and await-all propagate worker diagnostics and stop the parent'

cat > "$WORK/source dir/api-child.aml" <<'EOF'
TENSION 0.75
EOF
cat > "$WORK/elsewhere/api-child.aml" <<'EOF'
TENSION 0.5
EOF
cat > "$WORK/exec-api.c" <<'EOF'
#include "ariannamethod.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "API failure at line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    CHECK(am_exec_source("INCLUDE api-child.aml\n", argv[1]) == 0);
    CHECK(fabsf(am_get_state()->tension - 0.75f) < 1e-6f);
    CHECK(am_exec("INCLUDE api-child.aml\n") == 0);
    CHECK(fabsf(am_get_state()->tension - 0.5f) < 1e-6f);
    CHECK(am_exec_source("ECHO unreachable\n", "") != 0);
    char too_long[300];
    memset(too_long, 'x', sizeof(too_long));
    too_long[280] = '/'; too_long[290] = 0;
    CHECK(am_exec_source("ECHO unreachable\n", too_long) != 0);
    CHECK(strstr(am_get_error(), "source directory exceeds") != NULL);

    const char *bad = "TENSION 0.25\nwhile 1:\n    INCLUDE absent.aml\n    TENSION 0.99\nTENSION 0.99\n";
    CHECK(am_exec(bad) != 0);
    CHECK(fabsf(am_get_state()->tension - 0.25f) < 1e-6f);
    void *p = am_program_open(bad);
    CHECK(p != NULL);
    CHECK(am_program_step(p, 1) == 0);
    CHECK(am_program_step(p, 1) == 1);
    CHECK(am_program_step(p, 1) == 1);
    CHECK(am_program_remaining(p) == 0);
    CHECK(am_program_close(p) != 0);
    CHECK(fabsf(am_get_state()->tension - 0.25f) < 1e-6f);
    void *bc = am_compile(bad);
    CHECK(bc != NULL);
    CHECK(am_exec_compiled(bc) != 0);
    am_free_compiled(bc);
    CHECK(fabsf(am_get_state()->tension - 0.25f) < 1e-6f);
    const char *recurse = "def recurse():\n    recurse()\nrecurse()\n";
    CHECK(am_spawn_launch("bad", recurse) >= 0);
    CHECK(am_spawn_await("bad") != 0);
    CHECK(strstr(am_get_error(), "max call depth exceeded") != NULL);
    CHECK(am_exec("TENSION 0.25\n") == 0);
    CHECK(am_spawn_launch("bad-all", recurse) >= 0);
    am_spawn_await_all();
    CHECK(strstr(am_get_error(), "max call depth exceeded") != NULL);
    return 0;
}
EOF
cc -I"$ROOT/core" "$WORK/exec-api.c" "$ROOT/libaml.a" -lm -lpthread -o "$WORK/exec-api"
"$WORK/exec-api" "$WORK/source dir/embedded.aml"
echo 'PASS: source scope restores; interpreted, resumable, and bytecode loops stop on failure'

cat > "$WORK/args.aml" <<'EOF'
BLOOD MAIN {
#include <string.h>
int main(int argc, char **argv) {
    return argc == 2 && strcmp(argv[1], "two words $literal") == 0 ? 0 : 9;
}
}
EOF
"$AMLC" "$WORK/args.aml" --scalar -o "$WORK/program with spaces" --run -- 'two words $literal' > /dev/null 2> "$WORK/args.err"
cat > "$WORK/bad-c.aml" <<'EOF'
BLOOD MAIN { this is not valid C; }
EOF
if "$AMLC" "$WORK/bad-c.aml" --scalar -o "$WORK/bad-c" > /dev/null 2> "$WORK/bad-c.err"; then
    echo 'FAIL: C compilation failure reported success'; exit 1
fi
echo 'PASS: scalar builds, literal argv, and compiler failure exit status'
