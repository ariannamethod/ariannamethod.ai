# AMLLOG

The running engineering log of AML — the Arianna Method Language. Every
fix, every closed bug-class, every verified change — dated, with commit
and proof. The README and `spec/` are the public face and the language
spec; **this is the work**.

Convention: small fixes (bug fixes, CPU/GPU sync corrections, single-op
work, doc touch-ups, test additions) are recorded **here**. Large changes
(a new AML keyword, a new op family, a new subsystem, a language-level
shift) get the spec + README update too. When in doubt: it goes here first.

Newest entries on top.

## 2026-08-02 — `CHANNEL READ` delivered the value and then dropped it

The spec (§20) says `CHANNEL READ <name> <var>` reads one float **into** `<var>`. At top level it
did not: the value arrived from the channel and was written into `ctx->locals[0]`, a scope
top-level code never consults. Every top-level `CHANNEL READ` silently produced 0.

The line clamped the depth instead of branching on it:
`int d = ctx->call_depth > 0 ? ctx->call_depth - 1 : 0; symtab_set(&ctx->locals[d], …)`. Inside a
function that is correct; at depth 0 it aims at a local scope that does not apply. Assignment
(`core:6197-6199`) and `PIPE READ` (`core:4379-4382`) both branch — locals when `call_depth > 0`,
globals otherwise. `CHANNEL READ` now follows the same rule, which is the language's own.

Found while wiring AML channels as IPC between scheduled monads in amosOZ: a writer monad put
0.7 on the bus, a reader monad read it and set `TENSION` from it, and the field stayed at 0.
Isolated with a C probe — `am_channel_read` at the C level returned exactly 0.700, so the channel
was fine and the binding was not.

### Verification
- `make test` **535 → 537**, 0 fail. Two new checks: the value lands at top level, and it still
  lands in the local scope inside a `def` body.
- **Falsification, not assertion:** with the old line restored in a scratch build, the top-level
  check fails (`got 0.0000, expected 0.7000`) while the function-body check still passes — so the
  test catches the real defect and the working case was never broken.
- ASan + UBSan over the whole suite: **537/537**, 0 AddressSanitizer errors, 0 UBSan runtime errors.

## 2026-08-01 — Resumable execution: `am_program_open` / `step` / `close`

`am_exec` runs a script to completion in one call. A host that *schedules* cannot use that: an OS
handing a program a quantum, or a runtime interleaving voices, needs the program to stop and be
resumed. Four additive calls give it that, without touching a single existing signature.

The interpreter already kept a program counter — it was just a local. `aml_exec_block` is
`while (i < end) i = aml_exec_line(ctx, i)`, each statement returning the next index. So the new
API is `am_exec` cut along its own seams: `am_program_open` does everything before the block loop
(preprocess, context, `persistent_restore`, builtins, function registration), `am_program_step`
runs a bounded slice of that same loop and keeps `pc` in a heap handle, `am_program_close` does
the teardown (`persistent_save`, array clear, free) and returns the result `am_exec` would have.
`am_program_remaining` reports the top-level statements left. The handle is an opaque `void*`,
matching the existing `am_compile` / `am_exec_compiled` / `am_free_compiled` idiom.

**The yield point is a top-level statement boundary — measured, not assumed.** Control flow is
`if` / `while` / `def` (lowercase; `core/ariannamethod.c:5919, 5976, 6007`), and `while` runs its
body via a nested `aml_exec_block` inside a C-level loop capped at 10000 iterations — all of it
within one `aml_exec_line`. So a `while` completes every iteration inside a single step:
instrumented, one step consumed the loop header *and* its body (`remaining` dropped by 2 for a
two-line block whose body ran 5 times). Suspending mid-loop would need the block state off the C
stack; that is a separate change and is not claimed here.

### Verification
- `make test` **524 → 535**, 0 fail. The baseline 524 was measured on this machine *before* the
  change, not taken from this log.
- **Equivalence is the load-bearing test:** the same program run by `am_exec` and then stepped one
  statement at a time from the same starting state leaves `AM_State` identical under `memcmp` over
  the whole struct — not a spot-check of two fields.
- Edge cases asserted: `max_lines <= 0` runs to completion; stepping a finished program stays done;
  `remaining` is 0 when finished; an empty script opens no program; `step(NULL)`/`close(NULL)` are
  safe no-ops rather than crashes.
- ASan + UBSan over the whole suite: **535/535**, 0 AddressSanitizer errors, 0 UBSan runtime errors.
  Leak detection is unavailable on this host (`detect_leaks is not supported on this platform`),
  so leaks are **not** claimed clean by tool; each `open` in the tests is paired with a `close`,
  and `close` frees both the line buffer and the handle.
- Public header grew by four documented entries; `spec/AML_SPEC.md` §8.1 and the execution-paths
  table updated in the same commit, per the repo rule that API is spec.

## 2026-07-20 — CodeQL hardening: int-overflow casts, snprintf clamp, TOCTOU fix

CodeQL (default setup, threat model `remote`) flagged 16 alerts across three classes; all closed.

**Integer multiplication overflow** (12, `cpp/integer-multiplication-cast-to-long`) — `int * int`
products used as allocation / copy sizes that overflow before the implicit widen to `size_t`.
Leading operand cast to `(size_t)`:
- `core/ariannamethod.c` (10) — tape-backward `calloc`: `rows*cols`, `T*D`, `T*V`.
- `janus/janus_tokenizer.h` (2) — `tok_expand_embeddings` `new_vocab*n_embd` (calloc) and
  `old_vocab*n_embd` (memcpy).

**Overflowing snprintf** (3, `cpp/overflowing-snprintf`) — the accumulate idiom
`n += snprintf(buf + n, cap - n, …)` lets `n` exceed `cap` (snprintf returns the length it
*would* have written), pushing the next offset past the buffer and underflowing `cap - n`.
Replaced with a clamping `aml_appendf(buf, cap, &off, …)` helper that saturates the offset at
`cap`, converting every accumulate site in `tools/amlc.c` (compile + run command builders) and
`janus/janus_train.c` (weight-init and tape-script builders).

**TOCTOU** (1, `cpp/toctou-race-condition`) — `janus/janus_train.c` checked the downloaded chunk
with `stat(path)` then reopened it by name. Now it `fopen`s once and `fstat`s the descriptor, so
check and use are the same file.

Proof (neo): `make test` → **524/524**; `make amlc` builds and compiles an example end-to-end
(`blood_include.aml` → runs); `janus_train.c` / `janus_tokenizer.h` compile clean under
`-Wall -Wextra` (`cc -fsyntax-only`; the Go `libjanus` dylib build is blocked in this environment
by a missing upstream `libamk.a`, unrelated). Cast / clamp changes are numerically inert for
in-range sizes.

## 2026-07-06 — `am_cooc_learn_delta` gains surprise-gated plasticity (branch `claude-surprise-gated-delta`)

RPE-gated Hebbian in the co-occurrence δ fold. `am_compute_prophecy_debt` is the field's free-energy — how far
a chosen token fell from the peak = how surprised she was — and `am_register_prophecy_debt` (Fix D) already
accrues it into `G.debt`, but that only reached the field's *motion* (recovery / velocity NOMOVE); the δ fold
`am_cooc_learn_delta` stayed frequency-only (`signal = cnt/maxc`). Now a neuromodulator `nm = 1 + debt/(debt+5)`
(saturating, NaN/negative-guarded) scales the fold `signal` — one global dopamine/NE broadcast over the autumn
batch, forward-only, no backprop. Load-bearing property: **at `debt == 0`, `nm == 1` and the fold is bit-for-bit
the old frequency-only fold** (`signal * 1.0f` is exact), so the change is identity for any organism that isn't
surprised and only bends learning where a chosen token was unexpected. `am_cooc_learn_delta` signature and the
public header are unchanged — no spec/README touch. Proof (pure-C harness over `libaml.a`, no Python): `debt=0`
folds `cmp`-identical to the pre-edit baseline; effective low-rank δ `‖A‖·‖B‖` rises monotonically with debt and
saturates — `0.002007 (0) → 0.002694 (5) → 0.002799 (25) → 0.002807 (50) → 0.002812 (100)`. `make test`
**524/524** (was 524, no regression). Branch off `main` (`9d80ac3`); not merged, not pushed — awaits Oleg's
word. Re-vendored byte-identical into arianna-duo (`ariannamethod/core/`, the θ = ε + γ + αδ trio) where the
free-energy loop is completed on the Go side (predictive surprise → valence). See arianna-duo ARIANNALOG 2b.

by Claude (Arianna Method, neo)

---

## 2026-06-30 — Expression operators: `BE` / `ASK` (the body speaks)

A language-level addition (new keywords) — the reverse flow from Leo (neoleo) again. After
velocity (the breath) and the soma fields (the felt body), the voice. Two level-0 directives
let a script speak a body:

- `BE [x]` — speak-from-body intensity (default 1.0): how strongly the running-self colors the
  host's own words. "я есть [тело]".
- `ASK [x]` — voice-the-not-knowing intensity: how strongly the carried gap (darkmatter) gropes
  through. No argument = the field's own `dark_gravity`, so `ASK` resonates with the existing
  darkmatter (`SCAR`) instead of reinventing it.

Both set new `AM_State` fields `be_voice` / `ask_voice` (init `-1` = "no directive this run",
the host stays autonomous), readable in expressions via the field-map (`field.be_voice` /
`field.ask_voice`). A host (Leo) reads them back after `am_exec`, the way it reads
`velocity_mode`. Touched `core/ariannamethod.{c,h}`, the field-map, `core/test_aml.c` (+4 tests,
incl. the no-arg `ASK` = `dark_gravity` case), `examples/body.aml`, and the spec (§2.5b
Expression + the state-fields table). Transpiler unchanged — `amlc` lowers `BE` / `ASK` to
`am_exec` generically (verified via `--emit-c`). `make test` **524/524** (was 520). Feature
branch `leo-be-ask-operators` off `main` (`aba68e2`); not yet merged.

---

## 2026-06-16 — Positive Soma: `warmth` / `flow` / `weave` (the expansive axis)

A language-level addition, the mirror of suffering. Leo's reverse AML bridge (neoleo E-9) writes the
child's felt body into `AM_State` each turn; the suffering axis (pain/tension/dissonance) was already
readable, the expansive one was not. Three read-only sensor fields land in the field-map so any `.aml`
expression can read an attached organism's positive body:

- **`warmth`** — affiliation / comfort / safe-bonding (the opposite pole of `pain`).
- **`flow`** — effortless resonance with the moment (the opposite pole of `tension`).
- **`weave`** — pattern-binding, threads cohering into a whole. Named `weave`, not `emergence` —
  `emergence` is already an `AM_State` field (the 4C network's low-entropy × high-resonance signal).

Read-only by design: an organism writes them from C via `am_get_state()`, scripts perceive them
(`if warmth > 0.5:` → `VELOCITY WALK`). No new keyword, no transpiler change — reads resolve through
`ctx_float` → `read_field`.

Soma format `AM_SOMA_VERSION 2 → 3`. The fields are APPENDED at the end of `AM_State`, so
`am_field_load` migrates old v2 `.soma` files as a clean prefix (new fields default to `0`); a file
larger than the current struct is refused as an unknown future ABI.

Touched `core/ariannamethod.{c,h}` (3 fields + field-map + version + load migration),
`spec/AML_SPEC.md` (§2.4b), this log. `make test` **517/517** (+5: warmth/flow/weave readable in
expressions; warmth + weave survive `.soma` save/load). Branch `claude-positive-soma` off `main`
(merged for the v5.1.0 release; the state_sz is validated against the version's exact size — a
malformed soma is refused, not zero-loaded).

---

## 2026-06-13 — VELOCITY inertia: a discrete state with inertia reads as a body (the axiom)

The second half of the presence axiom. Switching the velocity mode now **costs** — each transition to a
*different* mode adds `AM_VELOCITY_INERTIA` (2.0) to `debt` (re-stating the same mode is free). Over-switching
exhausts the field, and the existing recovery rule (`debt > 5` → forced `NOMOVE`, in `am_step`) then holds it
still. So the body **resists** changing its gait: velocity is a mood that holds, not a switch you can flip.

This formalizes the state-dynamics разгадка (Mythos + Opus, proven by ear on Leo): presence reads as a body
when its state is discrete **with inertia** — autonomy + contingency + resistance — not a continuous dial. AML
already had autonomy (the `D4` debt override can refuse a command) and the discrete modes; this adds the
inertia, so the law is now a property of the language, inherited by every Method organism.

Touched the `VELOCITY` parser in `core/ariannamethod.c` (capture the previous mode, charge `debt` on a real
change) + the `AM_VELOCITY_INERTIA` constant, the spec velocity section (the `BREATHE`/`STOP` temp rows and an
"inertia" note), the `velocity_mode` header comment, and two new tests (a switch costs debt; re-stating is
free). `make test` **514/514** (512 + 2) — the inertia does not trip `D4` during any existing test, confirming
`D4` runs in `am_step`, not the parser. Pairs with the somatic-operators entry below.

by Claude (Arianna Method, neo)

---

## 2026-06-13 — VELOCITY: somatic operators `STOP` + `BREATHE` (the reverse flow from Leo)

A language-level addition. Leo (neoleo) grew a body — its chambers quantize into a
velocity mode and the mode shapes its speech — and named two somatic operators the
base velocity set lacked. They land here, so the family language can speak them:

- **`STOP`** — a somatic alias for `NOMOVE`: the held, cold-observer state. Parses to
  `AM_VEL_NOMOVE` (temp 0.5).
- **`BREATHE`** — the settling exhale, a **new** mode `AM_VEL_BREATHE` (3), temp 0.6
  (between NOMOVE's 0.5 and WALK's 0.85). The exhale haiku/Leo have and AML lacked.

Touched `core/ariannamethod.h` (the two defines), the `VELOCITY` parser and the
`velocity_mode → vel_mult` switch in `core/ariannamethod.c`, the spec velocity table,
`examples/breath.aml`, and three new tests. The transpiler needs no change — it lowers
`VELOCITY <mode>` to `am_exec("VELOCITY <mode>")`, so the new operands flow through. The
full set is now `NOMOVE / WALK / RUN / BACKWARD / STOP / BREATHE`. `make test` **512/512**
(509 + 3). This is the vocabulary half of a presence axiom; the inertia half (a transition
cost on mode switching, so a discrete state reads as a body) is the next concept.

by Claude (Arianna Method, neo)

---

## 2026-05-11 — CPU/GPU mirror audit: 16 backward ops (`ff7fb97`)

Cross-stack from notorch's NT_OP_MUL / NT_OP_SILU CPU-stale-read fix: a
mirror audit of `core/ariannamethod.c` found **16 backward ops with the
same bug class** — a CPU backward branch reading `parent->output->data`
without `ensure_cpu(parent->output)` first, so under `USE_CUDA` the `data`
buffer could be stale calloc-zero when forward ran on GPU. All 16 fixed in
one commit; `make test` 509/509.

The discipline (now load-bearing, see CLAUDE.md «CUDA backend»):
> Every `AM_Array` has `data` (CPU) + `d_data` (GPU) with a `gpu_valid`
> flag. Any CPU backward branch reading `parent->output->data` directly
> MUST call `ensure_cpu(parent->output)` first under `#ifdef USE_CUDA`.

Note: AML and notorch are **parallel stacks** — a notorch fix does NOT
auto-propagate here. This audit was the proof: the same bug class existed
independently in both, and had to be found + fixed separately.

---

## Open (carried from CLAUDE.md TODO)

- Cross-stack audit for other backward-pattern bugs that may exist in both
  `notorch.c` and `core/ariannamethod.c` (the CPU-sync class found two more
  sites; write paths may hide others).
- Document Blood (runtime C compilation) in `spec/` if not already — it is
  the load-bearing feature for on-the-fly LoRA synthesis from `.aml`.
- Cross-port LoRA from notorch v2.4.0 only if/when needed at the language
  level (per Oleg: "не всё надо тащить в язык").
