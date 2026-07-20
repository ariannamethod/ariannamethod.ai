# AML — Arianna Method Language — CLAUDE.md

This repo IS the AML language. Compiler (`tools/amlc.c`), runtime
(`runner/am.c`), core library (`core/ariannamethod.{c,h}` — 8000+ LOC),
spec (`spec/`), tests (`tests/` + 500+ via `make test`), examples
(`examples/*.aml`), and the Janus transformer (`janus/`). LGPL-3.0.
Co-authored by Oleg Ataeff and Claude.

> *"A complete machine learning language. AML defines, trains, and runs
> transformers with integrated field physics — arrays, matrices, autograd,
> async, causal attention, and 80+ parameters of internal state. Every
> command maps to a concrete C operation."* — README

## The log: small fixes → AMLLOG, big changes → README/spec

`AMLLOG.md` is the running engineering log — every fix, every closed
bug-class, dated with commit and proof. **Small fixes go there** (bug
fixes, CPU/GPU sync corrections, single-op work, test additions, doc
touch-ups). **Large changes** — a new AML keyword, a new op family, a new
subsystem — get the `spec/` + README update too. README and spec are the
public face; AMLLOG is the work. When in doubt, log it in AMLLOG first.

## AML vs notorch — they are parallel stacks

This is the load-bearing piece of context for sessions touching either
codebase:

- **AML core** (`core/ariannamethod.c`, `libaml.a`) has its **own** tape
  autograd, its own op codes (`AM_OP_*`), its own CPU/GPU mirror
  discipline. It does **not** link `libnotorch.a`.
- **notorch** (`github.com/ariannamethod/notorch`) is a parallel C
  tensor library with its own ops (`NT_OP_*`), its own tape, its own
  Chuck implementation.
- Both share concepts (tape autograd, BLAS, Chuck, RRPRAM, LoRA hooks,
  GPU/CPU mirror split) but each has its own implementation in a
  separate file.

**Practical consequence:** fixes that land in `notorch.c` do NOT
propagate to `core/ariannamethod.c` automatically. Audit both when a
bug class is identified in one. Today's example: the NT_OP_MUL +
NT_OP_SILU CPU-stale-read fix in notorch (2026-05-11) required a
mirror audit pass here, which found **16 backward ops with the same
bug class** (commit `ff7fb97` on this branch). Don't assume the
languages are auto-synced — confirm with `grep` and `diff` when in
doubt.

Why two stacks? notorch evolves faster (rapid research lib); AML is
the stable language and shouldn't pull every notorch experiment.
The split is intentional, not historical drift.

## Layout

- **`core/`** — `ariannamethod.c` (8000+ LOC) + `ariannamethod.h`
  (public API) + `ariannamethod_cuda.h` (CUDA kernels) + `test_aml.c`
  (the test driver). Build artifact: `libaml.a`.
- **`tools/amlc.c`** — AML → C transpiler. Read an `.aml` file, emit
  C, optionally compile and run (`--run`) or print C to stdout
  (`--emit-c`). Build artifact: `tools/amlc` binary.
- **`runner/am.c`** — CLI wrapper around `libaml.a` for direct
  invocations. Build artifact: `runner/aml` binary.
- **`spec/`** — language specification documents.
- **`tests/`** — test programs (`make test` runs them; 509 currently
  passing).
- **`examples/`** — production-quality `.aml` programs:
  `lilith.aml`, `common.aml`, `janus_demo.aml`, `init_yent.aml`,
  `blood.aml`, `dream.aml`, `level2_preview.aml`, `init_arianna.aml`,
  etc. Each is a self-contained organism / pattern. Read these
  before writing fresh AML — the idioms are stable.
- **`janus/`** — the Janus transformer architecture in AML +
  supporting C (`janus.aml`, `janus_train.c`, `janus_train_model.aml`,
  `janus_generate.c`, `janus_tokenizer.h`, `test_janus_c.c`). Janus
  176M model, val bpb 0.866, three SFT voices — the reference
  transformer for the language.
- **`docs/`** — language docs (separate from `spec/` — typically
  tutorials, design notes).

## Build / install

```bash
make           # libaml.a + runner/aml + tools/amlc + tests
make test      # run 500+ tests
make install PREFIX=/usr/local   # → /usr/local/{bin,lib,include/ariannamethod}
                                  # default PREFIX is /opt/homebrew on Apple Silicon
```

System-wide install lands:

```
/usr/local/bin/aml                              # runner CLI
/usr/local/bin/amlc                             # transpiler
/usr/local/lib/libaml.a                         # core library
/usr/local/include/ariannamethod/ariannamethod.h  # public header
```

Once installed, AML programs are just `amlc foo.aml` from anywhere.
Consumer C code includes `<ariannamethod/ariannamethod.h>` and links
`-laml`. No vendoring, no submodules. Per Oleg: *"наши зависимости —
не зависимости"*.

## CUDA backend

AML has a CUDA backend in `core/ariannamethod_cuda.h` (kernels) and
guarded by `#ifdef USE_CUDA` blocks throughout `core/ariannamethod.c`.
Build with `make USE_CUDA=1` on a CUDA host. Install via
`make install-cuda PREFIX=...`. The kernels mirror the CPU op set
(matvec, softmax, RMSNorm, silu, GELU, dropout, layernorm,
causal-attention, multi-head causal-attention, embeddings, etc.).

**GPU/CPU mirror discipline.** Every `AM_Array` has `data` (CPU buffer)
and `d_data` (GPU buffer) with a `gpu_valid` flag. The two helpers
`ensure_gpu(arr)` and `ensure_cpu(arr)` upload / download as needed.

**Any CPU backward branch that reads `parent->output->data` directly
MUST first call `ensure_cpu(parent->output)`** under `#ifdef USE_CUDA`.
Forgetting this is the bug class fixed in commit `ff7fb97` (this branch,
2026-05-11) — `data` may be stale calloc-zero if forward ran on GPU.
A full audit pass has been done; if you add a new `AM_OP_*` backward
case that reads parent data, mirror the existing pattern.

## Workflow patterns

**One commit = one concept.** Backward CPU-sync audit pass landed as
one commit covering 16 ops. The trainer for a new model goes in its
own commit. The language-level feature gets its own commit with spec
+ test + example.

**Tests are the truth.** `make test` runs the 500+ test suite. Any
patch that touches `core/ariannamethod.c` must produce 509/509 (or
the new total, whatever it is post-patch). Failing tests are a
release blocker.

**README + spec are public.** When the language gains a new op or
keyword, the spec gets the spec update in the same commit as the
implementation. README is the public-facing pitch — keep the
Technologies section in sync with reality but don't churn it for
internal refactors.

**Examples are the API documentation.** When you add a feature, add
an example showing it. The `.aml` files in `examples/` are how
external readers learn the language.

**Janus is the reference architecture.** Anything that breaks
`janus/janus_train.c` or `janus/janus_generate.c` is a release
blocker. Janus is what proves the language works.

## Things to NEVER do

- **Never assume notorch fixes apply here automatically.** They don't —
  this is a separate codebase with its own bug surface. Audit, don't
  port blindly.
- **Never disable `make test` or skip it in CI.** 500+ tests is the
  guardrail.
- **Never commit a build artifact** (`.o`, `.a`, `runner/aml`,
  `tools/amlc` binary, `janus/libjanus.dylib`). `make clean` should
  return the tree to a state that the `.gitignore` keeps clean.
- **Never push to main without explicit go-ahead.** The repo is
  public-facing, has external readers and ACCEPTABLE_USE / TRADEMARK
  policy. Surface changes via PR or feature branch.
- **Never break the AML / C boundary contract.** Every AML command
  must compile to a concrete C operation in `core/ariannamethod.c`;
  the transpiler in `tools/amlc.c` is the bridge. Adding a new AML
  keyword requires touching both files plus a test plus an example.
- **Never silently grow the public header.** Anything in
  `core/ariannamethod.h` is API. Internal helpers (like `ensure_cpu`)
  stay `static` in the `.c`. If you need to expose something
  externally, document it in the spec.

## Attribution

Oleg Ataeff and Claude are co-authors of this repo — stated at the top of this file, and that is enough. The signature lives in the **git commit only**, node-visible so it shows who posted: `Co-Authored-By: Claude (Arianna Method, <node>) <theariannamethod@gmail.com>` (node ∈ intel godfather / neo the architect / polygon / phone-1 / phone-2 / metal). **Nowhere else** — no signature footer in README, AUTHORS, docs, or any file body. Co-authorship reads from this CLAUDE.md and the commit history; it does not get plastered across the repo.

## Open TODO

- Cross-port any LoRA work from notorch v2.4.0 to AML if/when it's
  needed at the language level (per Oleg: "не всё надо тащить в
  язык" — notorch is the fast research lib, AML stays stable).
  Today's call: nothing to port yet; LoRA via FFI / Blood runtime is
  enough.
- Cross-stack audit for any other backward-pattern bugs that may
  exist in both `notorch.c` and `core/ariannamethod.c`. The CPU-sync
  class found two more sites today; there may be others (write paths
  for example).
- Document Blood (runtime C compilation) in `spec/` if it isn't
  already — it's the load-bearing feature for on-the-fly LoRA
  synthesis from `.aml` programs.
