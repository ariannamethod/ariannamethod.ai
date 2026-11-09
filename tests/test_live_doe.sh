#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
nt=$(cd "${NOTORCH_ROOT:-$root/../notorch}" && pwd)
lib=${NOTORCH_LIB:-$nt/libnotorch.a}
harness=${NOTORCH_HARNESS_LIB:-$nt/libnotorch_harness.a}
read -r -a flags <<< "${NOTORCH_LDFLAGS:-}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
make -C "$nt" expert_fixtures notorch
cd "$work"
"$nt/expert_fixtures" host.gguf gemma.gguf
mkdir hybrid dense wrong oversized
for dir in dense wrong oversized; do ln host.gguf "$dir/host.gguf"; done
"${CC:-cc}" -O2 -I"$root/core" -I"$nt" "$root/tests/test_live_doe.c" \
    "$root/libaml_doe.a" "$root/libaml.a" "$harness" "$lib" -lm -lpthread "${flags[@]}" -o check
./check
test "$("$nt/notorch" -q -T dense/host.gguf ab)" = 2
test "$("$nt/notorch" -q -T dense/tokenizer.gguf ab)" = 2
if [[ -f hybrid/host.gguf ]]; then
    cat > hybrid-turn.aml <<'AML'
doe_open('hybrid/host.gguf','',16)
doe_prompt(doe_encode('ab'))
doe_choose(-1)
doe_finish('async')
doe_save('hybrid-aml.gguf')
AML
    "$root/runner/aml-harness" hybrid-turn.aml
    opts=(-q --experts --learn-experts --async-consolidation)
    for mode in adjacent explicit; do
        extra=()
        if [[ $mode == explicit ]]; then extra=(--tokenizer hybrid/tokenizer.gguf); fi
        env -u NT_TOKENS NT_CHAT='||' NT_CTX=16 NT_ATTN_THREADS=1 "$nt/notorch" \
            "${opts[@]}" "${extra[@]}" --spore-out "hybrid-$mode.gguf" hybrid/host.gguf ab 1 0 > /dev/null
        cmp hybrid-aml.gguf "hybrid-$mode.gguf"
    done
fi
test ! -e bad.gguf
for mode in 1 2; do cmp mode0.gguf "mode$mode.gguf"; done
export NT_TOKENS=3,4,5 NT_CHAT='||' NT_CTX=16 NT_ATTN_THREADS=1
"$nt/notorch" -q --experts --learn-experts --async-consolidation --spore-out cli.gguf host.gguf prompt 1 0 > cli.txt
cmp mode0.gguf cli.gguf

cat > next.aml <<'AML'
doe_open('host.gguf','cli.gguf',16)
SPAWN neighbor:
    doe_open('host.gguf','cli.gguf',16)
    doe_prompt([3,4,5])
    doe_choose(-1)
    doe_finish('async')
    return doe_save('worker.gguf')
doe_transplant('cli.gguf',0)
doe_prompt([3,4,5])
doe_choose(-1)
doe_finish('learn')
AWAIT neighbor INTO saved
assert(saved == 1,'independent worker')
doe_save('next.gguf')
AML
"$root/runner/aml-harness" next.aml
cmp next.gguf worker.gguf
"$nt/notorch" -q --experts --learn-experts --spore-in cli.gguf --spore-out next-cli.gguf host.gguf prompt 1 0 > next.txt
cmp next-cli.gguf next.gguf

# Multiple chosen tokens in each turn share one accumulated update and KV.
cat > chat.aml <<'AML'
doe_open('host.gguf','',16)
def chat_turn():
    doe_prompt([3,4,5])
    first = doe_choose(-1)
    if first > 2:
        doe_choose(-1)
    doe_finish('async')
chat_turn()
chat_turn()
doe_save('chat.gguf')
AML
"$root/runner/aml-harness" chat.aml
printf 'one\ntwo\n' | "$nt/notorch" -q -n 2 -t 0 --experts --learn-experts --async-consolidation --spore-out chat-cli.gguf host.gguf > chat-cli.txt
cmp chat.gguf chat-cli.gguf

# Compiled standalone program, installed archives, source-relative paths from another cwd.
mkdir -p prefix/lib elsewhere
cp "$root"/{libaml.a,libaml_notorch.a,libaml_doe.a} "$lib" "$harness" prefix/lib/
sed "s/worker.gguf/compiled-worker.gguf/g;s/next.gguf/compiled-next.gguf/g" next.aml > compiled.aml
cat >> compiled.aml <<'AML'
doe_close()
doe_open('dense/host.gguf', '', 16)
ids = doe_encode('ab')
assert(ids[0] == 2, 'source-relative tokenizer')
AML
AML_PREFIX="$work/prefix" "$root/tools/amlc" compiled.aml --scalar -o "$work/compiled"
(cd elsewhere && "$work/compiled")
cmp compiled-worker.gguf next.gguf
cmp compiled-next.gguf next.gguf
if "$root/runner/aml" next.aml > plain.log 2>&1; then exit 1; fi
grep -q 'DOE backend unavailable' plain.log
echo 'Live DOE: Harness CLI parity, isolated workers and installed compiler passed'
