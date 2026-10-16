#!/usr/bin/env bash
# amlc keeps BLOOD LINK values and build paths out of shell syntax. A negative case passes
# only when amlc refuses before writing the .c file, with the expected diagnostic, and the
# sentinel file that the shell would create stays absent.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
make -C "$ROOT" amlc >/dev/null
AMLC="$ROOT/tools/amlc"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
SENTINEL="$WORK/sentinel"
failed=0

echo "== test_amlc_link =="
pass() { echo "  PASS [$1]"; }
fail() { echo "  FAIL [$1]: $2"; [ -f "${3:-}" ] && cat "$3"; failed=$((failed + 1)); }

main_block() {
    printf 'BLOOD MAIN {\n#include <stdio.h>\n#include <math.h>\n'
    printf 'int main(void) { printf("LINK_OK %%d\\n", (int)sqrt(49.0)); return 0; }\n}\n'
}

# refused NAME DIAGNOSTIC CFILE -- AMLC_ARGS...
refused() {
    local name=$1 want=$2 cfile=$3 log="$WORK/$1.log"
    shift 4
    rm -f "$SENTINEL"
    if "$AMLC" "$@" > "$log" 2>&1; then fail "$name" "amlc accepted" "$log"; return; fi
    if [ -e "$SENTINEL" ]; then fail "$name" "the shell ran the input" "$log"; return; fi
    if [ -e "$cfile" ]; then fail "$name" "C emitted before the refusal" "$log"; return; fi
    if ! grep -qF -- "$want" "$log"; then fail "$name" "missing \"$want\"" "$log"; return; fi
    pass "$name"
}

# link_refused NAME VALUE: BLOOD LINK VALUE on line 2 is refused at parse time.
link_refused() {
    local dir="$WORK/$1"
    mkdir -p "$dir"
    { printf '# %s\n' "$1"; printf 'BLOOD LINK %s\n' "$2"; main_block; } > "$dir/t.aml"
    refused "$1" "line 2: invalid input: BLOOD LINK byte" "$dir/out.c" -- \
        "$dir/t.aml" -o "$dir/out" --no-accel
}

link_refused link_semicolon "-lm; touch $SENTINEL"
link_refused link_command_subst "-lm \$(touch $SENTINEL)"
link_refused link_backtick "-lm \`touch $SENTINEL\`"
link_refused link_pipe "-lm | touch $SENTINEL"
link_refused link_background "-lm & touch $SENTINEL"
link_refused link_redirect "-lm > $SENTINEL"
link_refused link_single_quote "-lm 'x'"
link_refused link_double_quote "-lm \"x\""

other_ok=1
for byte in '<' '\' '*' '?' '[' ']' '{' '}' '~' '#' '!' '^' '(' ')' '$' $'\t' $'\001'; do
    mkdir -p "$WORK/other"
    { printf 'BLOOD LINK -lm%sx\n' "$byte"; main_block; } > "$WORK/other/t.aml"
    if "$AMLC" "$WORK/other/t.aml" --emit-c > /dev/null 2> "$WORK/other.log" ||
       ! grep -q 'line 1: invalid input: BLOOD LINK byte' "$WORK/other.log"; then
        fail link_other_bytes "byte $(printf '%s' "$byte" | od -An -tx1 | tr -d ' ') accepted" "$WORK/other.log"
        other_ok=0
        break
    fi
done
[ "$other_ok" = 1 ] && pass link_other_bytes

# Path quoting: each of input dir, -o and AML_PREFIX alone carries the quote.
QUOTED="q'; touch $SENTINEL; '"
mkdir -p "$WORK/$QUOTED" "$WORK/plain"
main_block > "$WORK/$QUOTED/t.aml"
main_block > "$WORK/plain/t.aml"
refused path_quote_input "path contains a single quote" "$WORK/plain/in.c" -- \
    "$WORK/$QUOTED/t.aml" -o "$WORK/plain/in" --no-accel
refused path_quote_output "path contains a single quote" "$WORK/$QUOTED/out.c" -- \
    "$WORK/plain/t.aml" -o "$WORK/$QUOTED/out" --no-accel
AML_PREFIX="$WORK/$QUOTED" refused path_quote_prefix "path contains a single quote" \
    "$WORK/plain/pfx.c" -- "$WORK/plain/t.aml" -o "$WORK/plain/pfx" --no-accel

# Legitimate forms keep working.
{ printf 'BLOOD LINK -lm\n'; main_block; } > "$WORK/plain/lm.aml"
mkdir -p "$WORK/out dir"
if "$AMLC" "$WORK/plain/lm.aml" -o "$WORK/out dir/lm prog" --no-accel > "$WORK/lm.log" 2>&1 &&
   [ "$("$WORK/out dir/lm prog")" = "LINK_OK 7" ]; then
    pass link_lm_builds
else
    fail link_lm_builds "-lm program did not build and run" "$WORK/lm.log"
fi
for value in '-Wl,--wrap=time' \
    '-DABCDEFGHIJKLMNOPQRSTUVWXYZ=abcdefghijklmnopqrstuvwxyz0123456789 _+.,=/:@%-'; do
    name=link_wrap_time_parses
    [ "$value" = '-Wl,--wrap=time' ] || name=link_charset_parses
    { printf 'BLOOD LINK %s\n' "$value"; main_block; } > "$WORK/plain/v.aml"
    if "$AMLC" "$WORK/plain/v.aml" --emit-c > /dev/null 2> "$WORK/v.log" &&
       grep -q '1 LINK(s)' "$WORK/v.log"; then
        pass "$name"
    else
        fail "$name" "value refused: $value" "$WORK/v.log"
    fi
done

echo
if [ "$failed" != 0 ]; then echo "amlc link: $failed case(s) failed"; exit 1; fi
echo "amlc link OK"
