#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PARLIAMENT_NT=${NOTORCH_ROOT:-"$ROOT/../notorch"}
PARLIAMENT_INCLUDE=${NOTORCH_INCLUDE:-"$PARLIAMENT_NT"}
PARLIAMENT_LIB=${NOTORCH_LIB:-"$PARLIAMENT_NT/libnotorch.a"}
read -r -a PARLIAMENT_FLAGS <<< "${NOTORCH_LDFLAGS:-}"
make -C "$ROOT" notorch NOTORCH_INCLUDE="$PARLIAMENT_INCLUDE" NOTORCH_LIB="$PARLIAMENT_LIB" NOTORCH_LDFLAGS="${NOTORCH_LDFLAGS:-}" >/dev/null
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
"${CC:-cc}" -O2 -Wall -Wextra -I"$ROOT/core" -I"$PARLIAMENT_INCLUDE" \
    "$ROOT/tests/test_aml_parliament.c" "$ROOT/libaml_notorch.a" "$ROOT/libaml.a" \
    "$PARLIAMENT_LIB" -lm -lpthread "${PARLIAMENT_FLAGS[@]}" -o "$WORK/bridge"
"$WORK/bridge"
mkdir -p "$WORK/prefix/lib" "$WORK/prefix/include/ariannamethod" "$WORK/elsewhere"
cp "$ROOT/libaml.a" "$ROOT/libaml_notorch.a" "$PARLIAMENT_LIB" "$WORK/prefix/lib/"
cp "$ROOT/core/ariannamethod.h" "$WORK/prefix/include/ariannamethod/"
export AML_PREFIX="$WORK/prefix"
printf 'AML_PARLIAMENT_VALUES_OK\n' > "$WORK/expected"
"$ROOT/runner/aml-notorch" "$ROOT/tests/parliament_values.aml" > "$WORK/interpreted"
diff -u "$WORK/expected" "$WORK/interpreted"
"$ROOT/tools/amlc" "$ROOT/tests/parliament_values.aml" --scalar -o "$WORK/parliament" > "$WORK/build.out" 2> "$WORK/build.err"
(cd "$WORK/elsewhere" && "$WORK/parliament") > "$WORK/compiled"
diff -u "$WORK/expected" "$WORK/compiled"
printf 'result = nt_parliament_elect([1],[0],[1],1,0)\nPRINT "WRONG_CONTINUATION"\n' > "$WORK/missing.aml"
if "$ROOT/runner/aml" "$WORK/missing.aml" > "$WORK/missing.out" 2> "$WORK/missing.err"; then
    echo 'FAIL: standalone runner supplied parliament arithmetic'; exit 1
fi
grep -q 'parliament backend unavailable' "$WORK/missing.err"
if grep -q WRONG_CONTINUATION "$WORK/missing.out"; then exit 1; fi
echo 'PASS parliament values: native bridge, compiler, optional backend and owned arrays'
