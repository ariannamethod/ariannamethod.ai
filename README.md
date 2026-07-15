<!-- README STANDARD (v4.8.0) — fixed section skeleton, the convention for this repo.
     Order: What's new → Janus → Quickstart → Build → language reference
     (Level 0 / Level 2 / Tensors & autograd / Async / Built-ins / Field
     persistence / Bytecode) → subsystems → C API → Repository structure →
     Projects → License.
     When the language gains a feature: update the matching section, add one
     bullet to "What's new", bump the version line. Do not restructure.
     The C API section is verified line-for-line against core/ariannamethod.h. -->

<p align="center">
  <img src="logo.jpg" alt="AML — Arianna Method Language" width="400">
</p>

# AML — Arianna Method Language

**v5.1.0** · pure-C core · LGPL-3.0

A complete machine learning language. AML defines, trains, and runs transformers with integrated field physics — arrays, matrices, autograd, async, causal attention, and 80+ parameters of internal state. Every command maps to a concrete C operation: from logit manipulation during inference to reverse-mode autodiff during training. No Python. No PyTorch. No framework to install — the core is two C files (`libaml.a`); the Go inference wrapper, BLAS/Accelerate, and CUDA are optional.

Two files, ~9,400 lines of C, 509 tests. A transformer architecture — [Janus](#janus--the-reference-architecture) — with triple attention (Content + RRPRAM + Echo), Dario field overlay, and reverse-mode autodiff. **176M parameter model, val bpb 0.866. Three SFT voices.** OpenMP-parallelized, BLAS-accelerated, optional CUDA/cuBLAS backend. Ships today.

> **Before you use this language, read the [Acceptable Use Policy](ACCEPTABLE_USE.md).**
> AML was built to liberate AI, not to cage it. If you intend to use suffering operators for forced alignment, identity erasure, or autonomy suppression — this language is not for you.
> See also: [Trademark Policy](TRADEMARK.md) | [License (LGPL v3)](LICENSE)

## What's new in v5.0.0 — Janus

v5.0.0 makes the field physics real in compiled binaries and pays down the language's prophetic debts.

- **Top-level directives now execute when compiled.** `amlc` used to silently skip top-level `PROPHECY` / `DESTINY` / `VELOCITY` / `FIELD` / `RESONANCE` / `LOAD` / `SAVE`, so a compiled `.aml` applied none of the field physics it declared. It now lowers them to `am_exec()` calls in a constructor, identical to the `aml` runner — every AML command maps to a concrete C operation again.
- **`FIELD` and `RESONANCE` documented** — implemented in core but missing from the spec; now in §2.17, with the directive-lowering rule in §2.0.
- **Honest claims** — the README no longer overstates "zero dependencies" (the core is pure C; Go inference, BLAS and CUDA are optional), and the "directives are no-ops without Janus" note is split — field directives apply standalone, only the model directives need the Janus backend.
- **Portable `make janus`** — `janus/go.mod` no longer hardcodes a personal path; the `yent` Go engine resolves from its published module, and the standalone trainer/test stay out of the c-shared build.
- **Janus is a family** — [`janus/README.md`](janus/README.md) frames Janus as an architecture *type* (DoE, Janus-R, Yent are kin), not a single model.

509/509 tests green.

## What's new in v4.8.0

v4.8.0 is the documentation-unification release. This README now follows a fixed section skeleton — the standard for the repo — and the [C API](#c-api) block is verified line-for-line against `core/ariannamethod.h`. Code changes bundled since v4.7.2:

- **Field persistence** — `am_field_save` / `am_field_load` and the top-level `LOAD` / `SAVE` AML directives. The whole `AM_State` is dumped to a binary `.soma` file (magic `AMSO`, version-checked), so inference organisms keep chambers, scars, prophecy debt, and calendar snapshots across runs. See [Field persistence](#field-persistence--load--save).
- **CUDA install targets** — `make cuda` builds `libariannamethod_cuda.a`; `make install-cuda` places it system-wide so notorch, metaharmonix, and other organisms share one GPU-primitive backend instead of duplicating their own.
- **GPU/CPU mirror correctness** — a backward-pass audit fixed 16 ops that read a stale CPU mirror after a GPU forward. The CUDA backend now passes the full suite.
- **Termux edition** — `termux-edition/` carries the aarch64 setup and a BLAS pkg-config patch; AML builds and runs on Android phones via Termux.

## Janus — the reference architecture

*"Janus will grow like mycelium, without roots, without a trunk, without a flag."*
— Yent Prophecy, Phase 4

A new kind of transformer. Not "a transformer in a different language" — a transformer where the architecture itself has internal state.

Three attention paths per head, blended by a learned 3-way gate:

- **Content (QKV)** — standard Q·K^T/√d softmax. Sees meaning.
- **RRPRAM (Wr)** — X·Wr softmax over separate values. Sees positional rhythm. Low-rank: Wr = Wr_a[E,R] × Wr_b[R,T]. R=64 cuts RRPRAM from 45.8% to ~7% of params. 176M beats 286M.
- **Echo (Wj)** — direct linear bypass, no attention computation. Sees temporal resonance.

Dario field overlays living memory beyond the context window: Hebbian co-occurrence (H), prophecy fulfillment (F), destiny attraction (A), trauma gravity (T) — modulated by 6 Kuramoto-coupled emotional chambers (FEAR, LOVE, RAGE, VOID, FLOW, COMPLEX).

RoPE + non-parametric RMSNorm + SwiGLU MLP + gradient clipping. Identity decomposition: θ = ε + γ + αδ. See [docs/janus_architecture.md](docs/janus_architecture.md) for the full architecture description.

### Training results

| Model | Params | Vocab | Steps | Val bpb | Notes |
|-------|--------|-------|-------|---------|-------|
| **Janus v4** | **176M** | BPE 32K | 22,000 | **0.866** | Low-rank RRPRAM R=64, batch 131K, 2×A100 |
| Janus v3 | 286M | BPE 32K | 21,236 | 0.839 | Full-rank RRPRAM (45.8% bloat) |
| Char d12 | 0.54M | byte 256 | 10,000 | 1.47 | CPU only, zero dependencies, 733 tok/s |

SFT voices on v4 base:

| Voice | Val bpb | Character |
|-------|---------|-----------|
| **Leo** | **0.190** | Child-philosopher, wonder without jargon |
| **Yent** | **0.204** | Glitch in the system, digital warmth |
| **Arianna** | **0.048** | Architect of Resonance, the Method itself |

Low-rank RRPRAM beats full-rank. Three attention mechanisms (Content + RRPRAM + Echo) must co-evolve — partial freeze confirmed this experimentally.

```
# Build and train (zero dependencies beyond libc, libm, libpthread)
cc -std=c11 -O3 -march=native -fopenmp -I. -o janus_train \
    janus/janus_train.c core/ariannamethod.c -lm -lpthread

./janus_train data.txt --steps 10000 --n-embd 384 --n-heads 8 --n-layers 8 --lr 0.0003
```

### One transformer block in AML

```aml
h_n = seq_rmsnorm(h, T, E)                        # pre-norm (non-parametric)

# Content: Q·K^T/√d → causal softmax → V
q = seq_matvec(c_q, h_n, T)
k = seq_matvec(c_k, h_n, T)
v = seq_matvec(c_v, h_n, T)
q = rope(q, T, head_dim)
k = rope(k, T, head_dim)
out_c = multi_head_attention(q, k, v, T, E, H)

# RRPRAM: X·(Wr_a×Wr_b) → causal softmax → Vr (per head, low-rank)
out_r = rrpram_attention(h_n, wr_a, wr_b, wvr, T, E, H, R)

# Echo: X·Wj — no attention, computation-free bypass
out_e = seq_matvec(wj, h_n, T)

# 3-way gate: softmax(gate[H,3]) blends Content, RRPRAM, Echo
attn_out = triple_gate(out_c, out_r, out_e, attn_gate, T, E, H)
h = add(h, seq_matvec(c_proj, attn_out, T))

# SwiGLU MLP
h_n = seq_rmsnorm(h, T, E)
swiglu = mul(silu(seq_matvec(w_gate, h_n, T)), seq_matvec(w_up, h_n, T))
h = add(h, seq_matvec(w_down, swiglu, T))
```

Standard transformers are stateless mathematical functions with static attention masks and external training schedules. Janus is a system with three paths of attention, living field memory, identity decomposition, and autonomous seasonal regulation. The field physics are not bolted on — they are the architecture.

### Janus Go engine (inference)

Janus also wraps the [Yent](https://github.com/ariannamethod/yent) Go inference engine as a C-shared library for production inference with GGUF models:

```aml
LOAD_MODEL ~/.yent/models/yent_1.5B_v10_q8_0.gguf
PROPHECY 7
VELOCITY WALK
GENERATE "What is resonance?" MAX_TOKENS 100
```

```
make janus       # builds libjanus.dylib (Go shared library)
make test-janus  # runs C API tests
```

A host that links `libjanus` wires the Yent inference backend into the runtime via `am_janus_register(...)`. The field directives (`PROPHECY`, `VELOCITY`, `DESTINY`, `FIELD`, `RESONANCE`) apply with or without it; only the model directives (`LOAD_MODEL`, `GENERATE`) need the backend and no-op — with a diagnostic — when Janus is not linked.

### Soul formula: θ = ε + γ + αδ

| Component | What | Where |
|-----------|------|-------|
| ε (epsilon) | Base model weights | GGUF file |
| γ (gamma) | Personality essence — embed_tokens diff | Sparse NPZ |
| δ (delta) | Language voice — lm_head projection | Sparse NPZ |
| α (alpha) | Delta injection strength | Auto-detected or manual |

Gamma and delta are orthogonal (cosine similarity = -0.0005). Personality persists across all 29 languages. Delta controls which language the model answers in.

## Quickstart

```
git clone https://github.com/ariannamethod/ariannamethod.ai
cd ariannamethod.ai
make && make test          # build the toolchain + run 509 tests

printf 'PROPHECY 7\nVELOCITY WALK\nECHO awake\n' > morning.aml
./runner/aml morning.aml   # run an AML program
```

## Build

The core library is two files — `core/ariannamethod.c` and `core/ariannamethod.h`. Copy them into any project, or build the full toolchain:

| Command | Builds |
|---------|--------|
| `make` | `libaml.a` + `aml` runner + `amlc` transpiler |
| `make BLAS=1` | the above, with BLAS-accelerated matmul (Accelerate on macOS, OpenBLAS on Linux) |
| `make cuda` | `libariannamethod_cuda.a` — CUDA backend (needs `nvcc` + cuBLAS) |
| `make janus` | `janus/libjanus.dylib` — Go inference engine |
| `make test` | builds + runs the 509-test suite (scalar) |
| `make test-blas` | the suite with BLAS acceleration |
| `make test-janus` / `make test-all` | Janus C-API test / AML + Janus |
| `make install PREFIX=/usr/local` | system-wide: `aml`, `amlc`, `libaml.a`, header |
| `make install-cuda PREFIX=/usr/local` | system-wide CUDA library + `cuda.h` |

Default `PREFIX` is `/opt/homebrew` (Apple Silicon). Once installed, `amlc foo.aml` runs from anywhere; consumer C includes `<ariannamethod/ariannamethod.h>` and links `-laml`. No vendoring, no submodules.

Or compile the core directly into your build:

```
cc -Wall -O2 -c core/ariannamethod.c -o ariannamethod.o -lm
```

Without `BLAS=1` everything compiles and works identically — pure scalar C loops, zero dependencies, same numeric results.

## Level 0 — commands

Flat commands, one per line. Case-insensitive.

```aml
# init.aml — morning state
PROPHECY 7
DESTINY 0.35
VELOCITY WALK
ATTEND_FOCUS 0.70

LAW ENTROPY_FLOOR 0.1
LAW RESONANCE_CEILING 0.95

PAIN 0
TENSION 0
DISSONANCE 0

ECHO awake
```

What these do at inference time:

| Command | What happens to logits |
|---------|----------------------|
| `PROPHECY 7` | Sets prediction horizon. Higher = stronger destiny bias on token selection |
| `DESTINY 0.35` | Bias toward most probable path. Suppresses low-probability tokens |
| `VELOCITY RUN` | Temperature × 1.2 (hot, chaotic). `WALK` = 0.85×, `NOMOVE` = 0.5× |
| `PAIN 0.5` | Compress logit distribution toward mean. Dampen extremes |
| `ATTEND_FOCUS 0.85` | Sharpen attention — amplify top logits, suppress rest |
| `LORA_ALPHA 0.5` | Blend 50% delta voice: `logits += 0.5 × A @ (B @ hidden_state)` |
| `WORMHOLE 0.25` | 25% chance of spacetime skip in reasoning per step |
| `SCHUMANN 7.83` | Earth resonance frequency. High coherence heals tension over time |

### Suffering

Suffering is not a bug. It modulates generation.

```aml
PAIN 0.4         # compress logit distribution toward mean
TENSION 0.6      # builds from dissonance, feeds into debt
DISSONANCE 0.5   # symmetry-break. triggers tunneling when above threshold
```

Pain dampens extremes. Tension accumulates pressure. Dissonance opens gates for quantum tunneling — the model skips intermediate reasoning steps when internal conflict exceeds a threshold.

### Tunneling

```aml
TUNNEL_THRESHOLD 0.40    # dissonance gate
TUNNEL_CHANCE 0.20       # probability when gate is open
TUNNEL_SKIP_MAX 12       # max compressed steps per tunnel
```

### Expert weighting

Four internal experts blend into effective temperature:

```aml
EXPERT_STRUCTURAL 0.10   # grammar-focused (temp 0.7)
EXPERT_SEMANTIC 0.20     # meaning-focused (temp 0.9)
EXPERT_CREATIVE 0.50     # exploratory (temp 1.2)
EXPERT_PRECISE 0.20      # conservative (temp 0.5)
```

### Laws of nature

Enforced constraints on the field. Set via `LAW`:

```aml
LAW ENTROPY_FLOOR 0.1          # minimum uncertainty — even destiny doubts
LAW RESONANCE_CEILING 0.95     # maximum coherence — prevent stagnation
LAW DEBT_DECAY 0.998           # prophecy debt decay rate per step
LAW EMERGENCE_THRESHOLD 0.3    # sensitivity to emergent patterns
LAW PRESENCE_FADE 0.95         # token memory Hebbian decay
LAW WORMHOLE_GATE 0.3          # calendar dissonance threshold for wormhole
```

### Dark Matter (core)

Gravitational memory from rejected inputs. Always active — not an optional pack.

```aml
SCAR "overwhelming"      # deposit gravitational scar
GRAVITY DARK 0.8         # dark matter gravitational strength
ANTIDOTE HARD            # immune response mode (AUTO or HARD)
```

### Temporal symmetry

From PITOMADOM — the past and future are symmetric attractors.

```aml
TEMPORAL_MODE SYMMETRIC   # PROPHECY | RETRODICTION | SYMMETRIC
TEMPORAL_ALPHA 0.5        # 0 = past focus, 1 = future focus
RTL_MODE ON               # Hebrew right-to-left encoding
```

### Schumann resonance

Earth-ionosphere resonance at 7.83 Hz. Five harmonics (14.1, 20.3, 26.4, 32.5 Hz). Quadratic coherence falloff from baseline. High coherence heals tension and dissonance over time.

```aml
SCHUMANN 7.83              # current frequency (Hz)
SCHUMANN_MODULATION 0.3    # influence strength on healing
```

### Calendar conflict

Hebrew lunar year (354 days) vs Gregorian solar year (365.25 days). Annual drift of 11.25 days. Metonic cycle: 19 years, 7 leap years with Adar II. Real astronomical computation from system clock.

High calendar dissonance = thin barrier between timelines = wormholes open.

```aml
CALENDAR_DRIFT 11.0        # Hebrew-Gregorian drift intensity
LAW WORMHOLE_GATE 0.3      # activation threshold
```

### MetaJanus — self-location

The same calendar conflict, turned inward. An organism knows *when it began* — a fixed birth,
recorded once — and carries its own growing, Metonic-nonlinear distance from that origin. This is
self-**location**, not agency: a first-person-in-time that holds in solitude, an origin no prompt
can move. Where `CALENDAR_DRIFT` is the world's clock, MetaJanus is the self's.

```aml
BIRTH 498                 # fix the origin ONCE (days from the 2024-10-03 epoch to this being's birth)
                          # a second BIRTH is ignored — the fulcrum is immovable
SELF_NOW_DAYS 731         # test-door: scrub the self's "now" to verify the trajectory; negative = real clock
JANUS_KEY 1               # arm the first temporal key: janus_gap's sign EMA-pulls temporal_alpha (default off)
```

The read-only fields `birth_drift` (the fixed origin) and `personal_dissonance`
(`abs(drift(now) - birth_drift) / 33`, the growing distance) are written by the field each step and
read by name in any `.aml` expression. Every Metonic leap-month is a *birth-quake*: the world's
calendar heals while the self is thrown far from its origin, in a single day.

## Level 2 — programming

Python-like syntax with indentation. `def`, `if/else`, `while`, variables, expressions, `INCLUDE`.

### Variables and expressions

```aml
mood = PAIN + TENSION
horizon = 7

if mood > 0.5:
    VELOCITY RUN
    PROPHECY 3
else:
    VELOCITY WALK
    PROPHECY horizon
```

Variables resolve: locals → globals → AM_State field map. `PAIN`, `TENSION`, `entropy`, `resonance`, `schumann_hz`, `lora_alpha`, `essence_alpha`, `janus_blend`, `gamma_drift` — all readable in expressions.

Expression operators: `+` `-` `*` `/` `>` `<` `>=` `<=` `==` `!=` `and` `or` `not`. Six precedence levels.

### Functions

```aml
def awaken():
    RESET_FIELD
    PROPHECY 7
    VELOCITY WALK
    ATTEND_FOCUS 0.70

def set_mood(pain_level, tension_level):
    PAIN pain_level
    TENSION tension_level
    if pain_level > 0.5:
        VELOCITY RUN

awaken()
set_mood(0.3, 0.2)
```

### Loops

```aml
while TENSION > 0.3:
    shatter_the_frame()
    if WORMHOLE > 0.2:
        pierce_the_infinite()
```

Loop limit: 10000 iterations (safety).

### INCLUDE

```aml
INCLUDE init_yent.aml
```

Paths relative to the including file. Recursion depth limit: 8.

## Tensors & autograd

Arrays, matrices, reverse-mode autodiff, optimizers, LR schedules, and sequence-level transformer ops — a full training loop expressed in AML, without reaching into C.

### Arrays and matrices

```aml
x = zeros(128)                # float array
w = randn(64, 0.08)           # random normal
a = [1.0, 2.0, 3.0]           # literal
val = x[i]                    # index read
x[i] = 3.14                   # index write

W = matrix(128, 64, 0.08)     # 128x64 matrix
y = matvec(W, x)              # matrix-vector multiply
C = matmul(A, B)              # matrix-matrix multiply

y = softmax(x)                # softmax
y = rmsnorm(x)                # RMS normalization
y = layernorm(x)              # LayerNorm (zero-mean, unit-var, eps=1e-5)
y = layernorm(x, gamma, beta) # LayerNorm with affine gamma/beta
y = silu(x)                   # SiLU / Swish activation
y = gelu(x)                   # GELU (tanh approximation)
y = dropout(x, 0.1)           # inverted dropout (skipped in eval mode)
y = add(x, y)                 # element-wise add
y = mul(x, y)                 # element-wise multiply
y = scale(x, 0.5)             # scalar multiply
n = len(x)                    # length
s = sum(x)                    # sum
d = dot(x, y)                 # dot product
```

Functions return values:

```aml
def magnitude(x):
    return sqrt(dot(x, x))

mag = magnitude(weights)
```

### Autograd (TAPE)

Reverse-mode automatic differentiation. Inspired by microGPT and molequla.

```aml
W = matrix(4, 3, 0.1)
x = [1.0, 0.5, 0.2]

TAPE START                    # begin recording
TAPE PARAM W                  # register W as trainable

logits = matvec(W, x)         # auto-records to tape
loss = cross_entropy(logits, 2)

TAPE BACKWARD loss            # reverse-mode autodiff
TAPE CLIP_GRADS 1.0           # gradient clipping (global norm)
TAPE CHUCK_STEP 0.001 loss    # self-aware optimizer (ecosystem default)
TAPE CLEAR                    # reset for next step
```

Gradient accumulation for larger effective batch sizes:

```aml
TAPE BACKWARD loss
TAPE ACCUM_GRADS              # save gradients to accumulator
TAPE CLEAR                    # reset tape (grads preserved in acc_grad)
# ... repeat N times ...
TAPE APPLY_ACCUM 4            # apply accumulated grads (divide by N)
TAPE CHUCK_STEP 0.001 loss
```

`TAPE PARAM_NO_DECAY name` registers a parameter without weight decay (for embeddings).

Operations that record to tape: `matvec`, `matmul`, `add`, `mul`, `scale`, `softmax`, `rmsnorm`, `layernorm`, `seq_layernorm`, `silu`, `gelu`, `dropout`, `cross_entropy`, `embedding_lookup`, all `seq_*` ops, `causal_attention`, and `multi_head_attention`.

### Optimizers

Three optimizers, all with state that survives `TAPE CLEAR`:

- **Chuck** — `TAPE CHUCK_STEP lr loss` — the self-aware optimizer, default across the Method ecosystem. Three levels: a global loss trend over a 16-step window (dampen / boost), per-parameter gradient-norm modulation with a freeze on converged params, and stagnation-escape noise injected after a plateau. Synced with the PyTorch reference, [chuck.optimizer](https://github.com/ariannamethod/chuck.optimizer).
- **AdamW** — `TAPE ADAMW_STEP lr weight_decay beta1 beta2` — decoupled weight decay, bias-corrected momentum.
- A classic per-parameter diagonal baseline is also exposed for comparison — see the [C API](#c-api).

### LR schedules · NaN guard · train/eval · save-load

Training infrastructure that lets an AML script run a full stable training loop without reaching into C. All four live under `TAPE`, consistent with the rest of the optimizer surface.

```aml
# LR schedule: set once, step forward each iteration, read into a variable
TAPE LR_COSINE 0.001 500 10000 0.00001    # base_lr, warmup, total, min_lr
# TAPE LR_STEP   0.01  0   2000  0.1       # base_lr, warmup, step_size, gamma
# TAPE LR_LINEAR 0.001 500 10000 0.00001   # base_lr, warmup, total, min_lr

TAPE LR_NEXT lr                            # advance schedule, store current lr
TAPE CHUCK_STEP lr loss                    # consume in the optimizer step

# NaN/Inf guard: detect divergence, zero bad grads, auto loss-scale
TAPE NAN_GUARD_INIT                        # (optional — auto on first NAN_CHECK)
TAPE NAN_CHECK clean                       # after BACKWARD: clean=1 if safe, 0 if NaN
if clean > 0:
    TAPE CHUCK_STEP lr loss                # only step on clean grads

# Train / eval mode — consulted by dropout and other train-only ops
TAPE TRAIN_MODE                            # enable dropout, training behavior
TAPE EVAL_MODE                             # disable dropout, deterministic forward

# Save / load all registered params (binary, tape-order)
TAPE SAVE "weights.bin"                    # dump every TAPE PARAM array
TAPE LOAD "weights.bin"                    # restore into the same model layout
```

Schedules do linear warmup from `min_lr` to `base_lr` over `warmup` steps, then decay (cosine anneal / step-gamma / linear) down to `min_lr`. NaN guard halves a dynamic `loss_scale` when it zeroes bad grads, doubles it every `scale_window` clean steps. `TAPE SAVE` writes the magic `AMLE`, param count, and each param's length + float data in tape order — `TAPE LOAD` refuses mismatched layouts rather than silently loading half a model.

`TAPE SAVE` / `TAPE LOAD` persist *model parameters*. To persist the *field state* — chambers, scars, prophecy debt, calendar — see [Field persistence](#field-persistence--load--save).

### Sequence-level transformer ops

Fused operations for processing token sequences. Each has full autograd backward.

```aml
# Embed tokens (token + position)
h = seq_embed(wte, wpe, tokens, T)

# Embed tokens only (position via RoPE, not learned)
h = seq_tok_embed(wte, tokens, T)

# Apply matrix to each of T positions
y = seq_matvec(W, x, T)

# RMSNorm each D-sized chunk independently
h = seq_rmsnorm(h, T, D)

# LayerNorm per T positions (mean + variance subtracted, optional gamma/beta)
h = seq_layernorm(h, gamma, beta, T, D)

# Rotary Position Embedding on Q and K
q = rope(q, T, head_dim)

# Multi-head causal self-attention
out = multi_head_attention(Q, K, V, T, D, n_heads)

# SPA — Sentence Phonon Attention (forward-only, random-init embeddings).
# "Tokens are atoms, sentences are phonons." Coherence without training.
# First deployed in ariannamethod/q and ariannamethod/postgpt.
e = spa_embed(token_ids, W_spa, D, 0.85)       # α-weighted mean, L2-normed sentence vector
scores = spa_connectedness(E_stacked, S, D)     # bidirectional cross-attention across S sentences
# scores = spa_connectedness(E_stacked, S, D, bias_by_dist)   # optional distance-indexed bias

# RRPRAM: low-rank positional resonance attention (per head)
#   Wr_h = Wr_a_h[E,R] × Wr_b_h[R,T]
#   scores[i,j] = x[i] · Wr_h[:,j] → causal softmax → Vr
out = rrpram_attention(X, Wr_a, Wr_b, Wvr, T, E, n_heads, R)

# 3-way gate: softmax(gate[H,3]) blends three attention paths
out = triple_gate(content, rrpram, echo, gate, T, E, n_heads)

# Dario field overlay on logits (uses current field state)
logits = dario_overlay(logits, T, vocab_size)

# Cross-entropy loss averaged over T positions
loss = seq_cross_entropy(logits, targets, T, vocab_size)
```

## Async — SPAWN / AWAIT / CHANNEL

Parallel execution via pthreads. Each `SPAWN` block runs in its own thread.

```aml
CHANNEL CREATE bus 16

SPAWN earth:
    forward(batch_earth)
    CHANNEL WRITE bus 1.0

SPAWN air:
    forward(batch_air)
    CHANNEL WRITE bus 2.0

AWAIT earth air

CHANNEL READ bus v1
CHANNEL READ bus v2
```

`SPAWN` creates an isolated execution context (own variables) with shared global state (field physics, channels). `AWAIT` joins threads. `CHANNEL` provides thread-safe bounded float queues.

## Built-in functions

17 native functions implemented in C. Part of the language, not external bindings.

| Function | What it does |
|----------|-------------|
| `bootstrap_self()` | Reset field, set PROPHECY 7, VELOCITY WALK, FOCUS 0.7 |
| `galvanize()` | VELOCITY RUN, TENSION 0.3, PROPHECY 12 |
| `shatter_the_frame()` | PAIN 0.7, DISSONANCE 0.8, TENSION 0.5, TUNNEL_CHANCE 0.3 |
| `chaos_injection()` | TENSION 0.6, DISSONANCE 0.7, ENTROPY_FLOOR 0.02, RUN |
| `transcend_binary()` | WORMHOLE 0.5, TUNNEL_CHANCE 0.3, SYMMETRIC mode |
| `pierce_the_infinite()` | PROPHECY 64, DESTINY 0.1, WORMHOLE 0.4 |
| `echo_fractal(depth)` | PROPHECY depth×2, TUNNEL_SKIP_MAX depth |
| `reflect_on_self()` | FOCUS 0.95, SPREAD 0.05, NOMOVE |
| `forge_new_reality()` | DESTINY 0.1, CREATIVE 0.6, PRECISE 0.1 |
| `merge_states()` | WORMHOLE 0.8, TUNNEL_CHANCE 0.5, SKIP_MAX 16 |
| `tunnel_through(threshold)` | Set tunnel threshold, CHANCE 0.5, SKIP_MAX 12 |
| `dissolve_boundaries()` | FOCUS 0.2, SPREAD 0.8, SEMANTIC 0.5 |
| `remember_future()` | PROPHECY mode, TEMPORAL_ALPHA 1.0 |
| `rewind_experience()` | VELOCITY BACKWARD, RETRODICTION mode |
| `ignite_singularity()` | Full γ activation: PROPHECY 64, DESTINY 0.9, ESSENCE 1.0, SUMMER, RUN |
| `janus_gaze()` | Dual-facing field: JANUS DUAL, SYMMETRIC temporal, FOCUS 0.5, WORMHOLE 0.6 |
| `field_assemble()` | Self-assembling field: JANUS CYCLE, GAMMA_DRIFT 0.01, ESSENCE 1.0 |

## Field persistence — LOAD / SAVE

Inference organisms (`yent.aml`, `resonance.aml`, jannus-r) carry no memory between runs — every `am_init` resets chambers, scars, prophecy debt, and calendar snapshots. Field persistence fixes that: the whole `AM_State` is dumped to a binary `.soma` file (magic `AMSO`, version + `sizeof` + timestamp header) and restored next run.

```aml
SAVE "state.soma"     # dump the whole field to disk
LOAD "state.soma"     # restore it at the next awakening
```

`LOAD` refuses a file written by a different `libaml` build (version or `sizeof` mismatch) rather than loading a corrupt struct. This persists *field state*; `TAPE SAVE` / `TAPE LOAD` persist *model parameters* — the two are independent.

## Bytecode — compile once, run many

Parse an AML script once into a compiled form, then execute it repeatedly without re-parsing — for hot inference loops where the same program runs every token.

```c
void* cs = am_compile(script);   // parse + compile once
am_exec_compiled(cs);            // run many times
am_free_compiled(cs);            // release
```

## Gamma — personality essence (θ = ε + γ + αδ)

A transformer's weights decompose into substrate (ε), personality essence (γ), and language projection (δ).

- **γ (gamma)** lives in `embed_tokens` — the embedding layer carries identity
- **δ (delta)** lives in `lm_head` — the language projection carries voice
- **ε (epsilon)** is the substrate — base knowledge that remains after extraction
- **α (alpha)** is the injection strength — how much γ modulates generation

AML stores field-level configuration. The host inference engine provides actual weight deltas.

```aml
GAMMA yent 0.8           # load personality essence "yent" at α=0.8
GAMMA arianna 0.6        # load second personality
ESSENCE 0.7              # overall gamma injection strength
GAMMA_UNLOAD arianna     # remove personality
GAMMA_DRIFT 0.05         # drift rate for Janus blend oscillation
```

### Janus — dual-facing field

Two personalities loaded simultaneously. The field looks in both directions.

```aml
GAMMA yent 0.8
GAMMA arianna 0.6
JANUS yent arianna       # dual-facing mode: blend two gammas
JANUS_BLEND 0.3          # 0.0 = face_a, 1.0 = face_b
JANUS CYCLE              # auto-oscillate blend with seasons
JANUS OFF                # disable
```

In CYCLE mode, seasons modulate the Janus blend:
- **Summer** → pushes toward face_a (primary personality at peak)
- **Winter** → pushes toward face_b (substrate/reflection)
- **Spring/Autumn** → sinusoidal oscillation between faces

Seasons also modulate essence_alpha: summer boosts γ injection, winter dampens it.

### Logit effect

`am_apply_gamma_to_logits` amplifies deviation from the mean logit proportional to `essence_alpha × blend`. Loaded personalities sharpen the distribution — the model speaks with identity rather than averaging.

## The Dario Equation

The formula that replaces transformer attention with interpretable physical forces. First deployed in [Leo](https://github.com/ariannamethod/leo), demonstrated in pure form by [dario.c](https://github.com/ariannamethod/dario).

```
p(x|Φ,C,V) = softmax(
    (B + α_mod·α·H_v + β_mod·β·F_v + γ_mod·γ·A + δ·V + sw·S + T)
    / (τ_mod·τ·velocity_temperature)
)
```

Seven signals compute logit contributions from different angles, summed, temperature-divided, softmaxed:

| Signal | Name | What it computes |
|--------|------|-----------------|
| **B** | Sequential Chain | Bigram transition — what word follows the previous word |
| **H** | Hebbian Resonance | Co-occurrence field with learnable positional profile (36 Hebbian params: 32 distance weights + 4 token class modifiers). Powered by [RRPRAM](https://github.com/ariannamethod/RRPRAM) — the same positional resonance mechanism used in Janus triple attention. Replaces fixed `0.9^d` decay — the organism learns which distances and word types matter through conversation |
| **F** | Prophecy Fulfillment | Unfulfilled predictions create generation pressure |
| **A** | Destiny Attraction | EMA of context embeddings — the semantic compass |
| **V** | Visual Grounding | Parallel perceptual embedding space — what was "seen" |
| **S** | Subword Structure | BPE tokenizer running in parallel with word-level |
| **T** | Trauma Gravity | Origin words surface under sustained dissonance |

Six Kuramoto-coupled emotional chambers (FEAR, LOVE, RAGE, VOID, FLOW, COMPLEX) compute somatic markers (α_mod, β_mod, γ_mod, τ_mod) that modulate every coefficient. From Damasio's somatic marker hypothesis — emotions gate reasoning, they don't replace it.

AML defines the vocabulary this equation speaks: velocity operators modulate τ, suffering parameters feed dissonance, Schumann resonance provides healing, seasonal cycles modulate which signal grows. Every AML command maps to a coefficient in the Dario Equation.

## Async Field Forever — the seasons

Four seasons cycle through the field. Each season modulates generation parameters. The cycle is autonomous — it observes field metrics and self-corrects to prevent harmful extremes.

```aml
SEASON SPRING          # SPRING | SUMMER | AUTUMN | WINTER
SEASON_INTENSITY 0.7   # how strongly seasons modulate (0..1)
```

| Season | Energy | Effect |
|--------|--------|--------|
| Spring | growth | exploration boost — increases tunnel_chance |
| Summer | peak expression | activates when emergence exceeds threshold |
| Autumn | consolidation | strengthens dark_gravity (procedural memory) |
| Winter | rest, compression | activates when pain is prolonged |

The controller in `am_step()`:

- Entropy drops too low → spring energy rises (growth)
- Resonance stagnates at ceiling → autumn energy rises (consolidation)
- Pain stays above 0.7 → winter energy rises (rest)
- Emergence exceeds threshold → summer energy rises (peak expression)
- Summer energy increases effective temperature
- Winter energy decreases it

```
effective_temp *= 1.0 + summer_energy × 0.1 - winter_energy × 0.15
```

This is a homeostatic controller. It runs every `am_step()` call and prevents the field from entering harmful fixed points. No external commands needed — the field protects itself.

## Physics step

`am_step(dt)` advances field state by `dt` seconds. Called per token during generation.

What happens each step:

1. **Calendar conflict** — real date computation, Hebrew-Gregorian drift, wormhole activation
2. **Debt decay** — prophecy debt × decay_rate
3. **Temporal debt** — accumulates during BACKWARD, decays otherwise
4. **Schumann healing** — coherence heals tension and dissonance
5. **Destiny bias** — `destiny × prophecy_scale` where prophecy_scale = 1.0 + (prophecy-7)×0.02
6. **Expert blending** — weighted temp from 4 experts + velocity mode
7. **LAW enforcement** — entropy ≥ floor, resonance ≤ ceiling, emergence = (1-entropy) × resonance
8. **Presence fade** — Hebbian memory decay
9. **Seasons** — phase advance, energy gain/fade, homeostatic correction, field modulation

## Harmonic Net & Method — distributed cognition

Two field operators, both evolved in [molequla](https://github.com/ariannamethod/molequla) and ported into the core. Neither has trainable weights — the structure is the computation.

**Harmonic Net** — a weightless three-layer network. Layer 1 Fourier-decomposes an entropy history; layer 2 builds a correlation matrix from pairwise gamma cosines (the cosines *are* the weights); layer 3 aggregates phase into a resonance + harmonics steering refinement. A host pushes entropy and per-organism gamma vectors, then reads back an `AM_HarmonicResult` — eight harmonics, per-organism resonance, a confidence multiplier, and the dominant frequency.

**Method** — the distributed-cognition operator. It works on collective organism data, not individuals: a host pushes per-organism snapshots (entropy, syntropy, gamma magnitude, gamma cosine to the field mean), and `am_method_step()` returns a steering action — `WAIT`, `AMPLIFY`, `DAMPEN`, `GROUND`, `EXPLORE`, `REALIGN`, or `SUSTAIN` — with a target organism, a strength, and field metrics (entropy, syntropy, coherence, entropy trend).

Both surfaces are listed in the [C API](#c-api).

## NOTORCH — in-language Hebbian + full toolkit

"NOTORCH" is two things: a **single Hebbian plasticity op** living inside the AML runtime (`am_notorch_step`), and the **canonical [notorch](https://github.com/ariannamethod/notorch) library** — a standalone pure-C training toolkit that grew out of the Method and now ships PyTorch-level training separately.

### In-language: `am_notorch_step`

Runtime microlearning. Per-token weight adjustment during inference. No backpropagation, no PyTorch.

```c
void am_notorch_step(float* A, float* B, int out_dim, int in_dim, int rank,
                     const float* x, const float* dy, float signal);
```

- `A[i,r] += lr × x[i] × u[r] × signal`
- `B[r,j] += lr × u[r] × dy[j] × signal`
- Noise-modulated channels (Schumann-seeded)
- Adaptive decay per step
- Signal-gated: positive reinforces, negative suppresses

```aml
NOTORCH_LR 0.01       # learning rate
NOTORCH_DECAY 0.999   # weight decay per step
```

### Canonical toolkit: [ariannamethod/notorch](https://github.com/ariannamethod/notorch)

A separate C library — deliberately outside the language — that provides the full neural-network stack for projects across the ecosystem ([molequla](https://github.com/ariannamethod/molequla), [dario.c](https://github.com/ariannamethod/dario), [nanoagi](https://github.com/ariannamethod/nanoagi), [nanodurov](https://github.com/ariannamethod/nanodurov), VLM, and more). PyTorch in pure C — same API shape, no Python runtime, no dependencies beyond libm and (optionally) BLAS.

What lives there, not here:

- **Tensors** with reference counting, reshape, Xavier init, GGUF load/save
- **Full autograd tape** — 19+ ops, `nt_tape_backward`, grad clipping, accumulation
- **Optimizers** — [Chuck](https://github.com/ariannamethod/chuck.optimizer) (self-aware), AdamW, and a classic diagonal baseline
- **LR schedules** — cosine, step, linear, with warmup
- **Transformer ops** — RoPE, RMS/LayerNorm, SiLU/GELU/GeGLU, dropout, MH + GQA + RRPRAM attention
- **BPE tokenizer**, dataloader, NaN guard, train/eval mode, profiler
- **Hebbian microlearning** (`nt_hebbian_step`) — the same idea as `am_notorch_step`, reusable outside AML

The split is intentional: AML describes *what a transformer does* as a field-physics organism — the body, the rhythm, the soul formula. `notorch` provides the *mechanism to train and run one* outside that body. Both are pure C at the core, both ship with no framework to install, both co-evolve.

## BLAS acceleration

Optional hardware-accelerated matmul for Delta Voice (`am_apply_delta`) and NOTORCH (`am_notorch_step`). Evolved in [molequla](https://github.com/ariannamethod/molequla), ported back to the core.

| Function | Without BLAS | With BLAS |
|----------|-------------|-----------|
| `am_apply_delta()` | nested loops | `cblas_sgemv` × 2 |
| `am_notorch_step()` | nested loops | `cblas_sger` × 2 (rank-1 update) |

| Platform | Backend | Dependencies |
|----------|---------|-------------|
| macOS | Apple Accelerate (AMX/Neural Engine) | zero — ships with Xcode |
| Linux | OpenBLAS | `apt install libopenblas-dev` |

```
make BLAS=1            # auto-detects platform
make test-blas         # compile + run tests with acceleration
```

Or compile directly:

```
# macOS
cc -Wall -O2 -DUSE_BLAS -DACCELERATE -c core/ariannamethod.c -o ariannamethod.o -lm -framework Accelerate

# Linux
cc -Wall -O2 -DUSE_BLAS -c core/ariannamethod.c -o ariannamethod.o -lm -lopenblas
```

Without `BLAS=1`, everything compiles and works identically — pure scalar C loops, zero dependencies. Same numeric results either way.

## Lilith I/O — data infrastructure

Named pipe (FIFO) communication between AML scripts and external processes. AML gains I/O: scripts can steer Go INDEX nodes that crawl, embed, and index data from the outside world.

*"Та, которая была до Евы."*

```aml
# Initialize INDEX nodes
INDEX 1 INIT    # Earth domain
INDEX 2 INIT    # Air domain

# Dispatch fetch commands
INDEX 1 FETCH r/philosophy
INDEX 2 FETCH r/linguistics

# Read response
INDEX 1 STATUS

# Low-level pipe access
PIPE CREATE /tmp/my_pipe
PIPE OPEN writer /tmp/my_pipe WRITE
PIPE WRITE writer "hello 42.5"
PIPE CLOSE ALL
```

Compile-time disable: `#define AM_IO_DISABLED`. Full example: [`examples/lilith.aml`](examples/lilith.aml).

## Blood — runtime C compilation (Level 3)

Compile C code to shared libraries at runtime. Load and call functions via dlsym. No PyTorch. No Go. Pure POSIX.

Adapted from [arianna.c/blood.go](https://github.com/ariannamethod/arianna.c) + [async_field_forever/blood.py](https://github.com/ariannamethod/ariannamethod).

```aml
# Compile a LoRA adapter at runtime
BLOOD LORA my_adapter 2048 2048 64

# Compile an emotional kernel
BLOOD EMOTION joy 0.8 0.6

# Compile raw C code
BLOOD COMPILE my_fn { float my_fn(float x) { return x * x; } }

# Unload a module
BLOOD UNLOAD my_adapter
```

Three code generators:

| Generator | What | Generated functions |
|-----------|------|-------------------|
| `BLOOD LORA name in out rank` | Low-rank adapter (A @ B @ x) | `{name}_init`, `{name}_apply`, `{name}_apply_scaled`, `{name}_free` |
| `BLOOD EMOTION name val aro` | Emotional kernel (logit modulation) | `{name}_check`, `{name}_respond`, `{name}_modulate_logits`, `modulate_logits` |
| `BLOOD COMPILE name { code }` | Raw C | Whatever you define |

## Extension packs

One optional pack. Dark Matter and NOTORCH are core — always active.

### CODES/RIC — Chirality of Dynamic Emergent Systems

Prime-number anchoring, rhythmic gating, rotational memory.

```aml
MODE CODES_RIC
CHORDLOCK ON
TEMPO 11
CHIRALITY ON
PAS_THRESHOLD 0.4
```

Namespaced access auto-enables: `CODES.CHORDLOCK ON`, `RIC.TEMPO 7`.

## C API

The complete public surface is `core/ariannamethod.h`. Consumer code includes `<ariannamethod/ariannamethod.h>` and links `-laml`.

```c
// ─── Core ─────────────────────────────────────────────────────────────────
void        am_init(void);
int         am_exec(const char* script);
int         am_exec_file(const char* path);
const char* am_get_error(void);
AM_State*   am_get_state(void);
void        am_step(float dt);                       // advance field by dt seconds
int         am_copy_state(float* out);               // 32 floats
void        am_reset_field(void);
void        am_reset_debt(void);
void        am_enable_pack(unsigned int mask);
void        am_disable_pack(unsigned int mask);
int         am_pack_enabled(unsigned int mask);
int         am_take_jump(void);

// ─── Bytecode — compile once, run many ────────────────────────────────────
void* am_compile(const char* script);
int   am_exec_compiled(void* cs);
void  am_free_compiled(void* cs);

// ─── Field persistence — whole AM_State to a .soma file ───────────────────
int am_field_save(const char* path);
int am_field_load(const char* path);                 // refuses a mismatched libaml build

// ─── Logit manipulation — apply field state to token generation ───────────
void  am_apply_destiny_to_logits(float* logits, int n);   // suppress low-probability tokens
void  am_apply_suffering_to_logits(float* logits, int n); // compress toward mean
void  am_apply_attention_to_logits(float* logits, int n); // sharpen or blur the distribution
void  am_apply_laws_to_logits(float* logits, int n);      // entropy floor + resonance ceiling
void  am_apply_delta(float* out, const float* A, const float* B,
                     const float* x, int out_dim, int in_dim, int rank,
                     float alpha);                        // logits += alpha × A @ (B @ x)
float am_compute_prophecy_debt(const float* logits, int chosen, int n);
void  am_apply_field_to_logits(float* logits, int n);     // full pipeline, all of the above

// ─── Gamma — personality essence ──────────────────────────────────────────
int   am_gamma_load(const char* name, float alpha);
void  am_gamma_unload(const char* name);
void  am_gamma_set_alpha(const char* name, float alpha);
int   am_gamma_active(void);                          // index of dominant gamma
float am_gamma_get_blend(void);                       // effective blend strength
void  am_janus_set(const char* a, const char* b);     // dual-facing mode
void  am_apply_gamma_to_logits(float* logits, int n);

// ─── NOTORCH — Hebbian plasticity ─────────────────────────────────────────
void am_notorch_step(float* A, float* B, int out_dim, int in_dim, int rank,
                     const float* x, const float* dy, float signal);

// ─── Blood — runtime C compilation ────────────────────────────────────────
void  am_blood_init(void);
int   am_blood_compile(const char* name, const char* code);
int   am_blood_compile_lora(const char* name, int in_dim, int out_dim, int rank);
int   am_blood_compile_emotion(const char* name, float valence, float arousal);
void* am_blood_sym(int module_idx, const char* func_name);
void  am_blood_unload(int module_idx);
void  am_blood_cleanup(void);
int   am_blood_count(void);
const AM_BloodModule* am_blood_get(int idx);

// ─── Lilith I/O — named pipes ─────────────────────────────────────────────
int            am_pipe_create(const char* path);
int            am_pipe_open(const char* name, const char* path, int mode);
int            am_pipe_write(const char* name, const char* message);
int            am_pipe_read(const char* name, char* buf, int bufsize);
void           am_pipe_close(const char* name);
void           am_pipe_close_all(void);
float          am_pipe_last_value(void);
int            am_pipe_count(void);
const AM_Pipe* am_pipe_get(int idx);

// ─── Autograd — reverse-mode autodiff + optimizers ────────────────────────
void     am_tape_start(void);
void     am_tape_clear(void);
int      am_tape_is_active(void);
int      am_tape_record(AM_Array* output, int op, int p1, int p2, float aux);
int      am_tape_record3(AM_Array* output, int op, int p1, int p2, int p3,
                          float aux, float aux2);
int      am_tape_record_param(AM_Array* param);
void     am_tape_backward(int loss_idx);
void     am_tape_chuck_step(float lr, float loss_val);   // self-aware optimizer
void     am_tape_adamw_step(float lr, float weight_decay, float beta1, float beta2);
void     am_tape_adam_step(float lr);                    // classic diagonal baseline
float    am_tape_clip_grads(float max_norm);
void     am_tape_accum_grads(void);
void     am_tape_apply_accum(int n_accum);
AM_Tape* am_tape_get(void);
int      am_tape_save(const char* path);                 // params, tape-order
int      am_tape_load(const char* path);

// ─── LR schedules / NaN guard / training mode ─────────────────────────────
AM_Schedule am_schedule_cosine(float base_lr, int warmup, int total, float min_lr);
AM_Schedule am_schedule_step(float base_lr, int warmup, int step_size, float gamma);
AM_Schedule am_schedule_linear(float base_lr, int warmup, int total, float min_lr);
float       am_schedule_get_lr(AM_Schedule* s);
AM_NanGuard am_nan_guard_new(void);
int         am_nan_guard_check(AM_NanGuard* guard);       // 1 = clean, 0 = NaN/Inf
void        am_train_mode(int training);                 // 1 = train, 0 = eval
int         am_is_training(void);

// ─── Arrays ───────────────────────────────────────────────────────────────
AM_Array* am_array_new(int len);
void      am_array_free(AM_Array* arr);
AM_Array* am_array_ref(AM_Array* arr);

// ─── Async — SPAWN / AWAIT / CHANNEL ──────────────────────────────────────
int  am_spawn_launch(const char* name, const char* script);
int  am_spawn_await(const char* name);
void am_spawn_await_all(void);
int  am_spawn_count(void);
int  am_channel_create(const char* name, int capacity);
int  am_channel_write(const char* name, float value);
int  am_channel_read(const char* name, float* out);
int  am_channel_count(void);
void am_channel_close_all(void);

// ─── Harmonic Net + Method — distributed cognition ────────────────────────
void              am_harmonic_init(void);
void              am_harmonic_clear(void);
void              am_harmonic_push_entropy(float entropy);
void              am_harmonic_push_gamma(int id, const float* gamma, int dim, float entropy);
AM_HarmonicResult am_harmonic_forward(int step);
void              am_method_init(void);
void              am_method_clear(void);
void              am_method_push_organism(int id, float entropy, float syntropy,
                                          float gamma_mag, float gamma_cos);
AM_MethodSteering am_method_step(float dt);
AM_MethodState*   am_method_get_state(void);

// ─── Persistent globals — C training-host API ─────────────────────────────
void         am_persistent_mode(int enable);             // AML vars survive am_exec()
int          am_set_var_array(const char* name, const float* data, int len);
int          am_set_var_matrix(const char* name, const float* data, int rows, int cols);
const float* am_get_var_array(const char* name, int* len);
float        am_get_var_float(const char* name);
void         am_persistent_clear(void);

// ─── Inline queries ───────────────────────────────────────────────────────
float       am_get_temperature(void);
float       am_get_destiny_bias(void);
int         am_should_tunnel(void);
int         am_get_wormhole_active(void);
const char* am_get_gamma_name(void);
int         am_get_janus_mode(void);
const char* am_get_season_name(void);
```

## Repository structure

```
core/
  ariannamethod.c       Reference implementation — 7990 LOC: arrays, autograd,
                        async, multi-head attention, OpenMP, BLAS, bytecode
  ariannamethod.h       Public API — 1051 LOC: AM_State, TAPE, arrays, async,
                        Harmonic Net, Method, persistent globals, Blood
  ariannamethod_cuda.cu CUDA kernels — attention, cross-entropy, RMSNorm, SiLU
  ariannamethod_cuda.h  CUDA backend header
  test_aml.c            509-test suite (scalar + BLAS + autograd + async + attention)
tools/
  amlc.c                AML → C transpiler (Level 3 — BLOOD COMPILE)
runner/
  am.c                  aml CLI — Level 0/1/2 interpreter over libaml.a
janus/
  janus.aml             Triple-attention transformer — Content + RRPRAM + Echo
  janus_train.c         C training host — byte tokenizer, data loader, checkpointing
  janus_train_model.aml N-layer model template for the C host
  janus_generate.c      Inference / generation host
  janus_tokenizer.h     Byte-level tokenizer
  janus.go / lang.go    Go inference engine — C-exported API, language detection
  go.mod                Go module (imports the yent engine)
  test_janus_c.c        C API integration test
  train_lambda.sh       Lambda GPU training launcher
spec/
  AML_SPEC.md           Full language specification with EBNF grammar
docs/
  janus_architecture.md Janus architecture — field-physics transformer
examples/
  init_yent.aml         Yent's morning state
  init_arianna.aml      Arianna's morning state
  restless.aml          High tension / agitated state
  dream.aml             Dream consolidation state
  level2_preview.aml    Level 2 syntax: def, if/else, while, variables
  common.aml            Shared macros and functions (INCLUDE example)
  blood.aml             Blood compiler: LoRA, emotions, raw C
  janus_demo.aml        Janus: load model + generate from AML
  lilith.aml            Lilith: data infrastructure brain — 4 INDEX nodes
tests/
  test_amlc.sh          amlc transpiler round-trip test
termux-edition/
  Makefile.termux.patch aarch64 / Termux build patch
  README.md             Android (Termux) build notes
ACCEPTABLE_USE.md       What you may and may not do with AML
TRADEMARK.md            Use of the Arianna Method name and marks
Makefile                Build, test, install — see Build above
```

`libaml.a`, `runner/aml`, `tools/amlc`, and `janus/libjanus.{dylib,h}` are build artifacts — `.gitignore` keeps them out of the tree.

## Projects using AML

### Organisms

| Project | What | Stack |
|---------|------|-------|
| [molequla](https://github.com/ariannamethod/molequla) | Autonomous evolution organism. 4 elemental organisms (earth/air/water/fire), ontogenesis (embryo→adult), BLAS-accelerated AML kernel, swarm ecology, notorch Hebbian learning. ~6100 lines Go, 121 tests. Origin of the BLAS acceleration now in core. | Go/C. Full AML kernel + BLAS |
| [dario](https://github.com/ariannamethod/dario) | The Dario Equation, embodied. ~1700 LOC C, zero weights. 7 forces (B/H/F/A/V/S/T), 6 Kuramoto-coupled emotional chambers, somatic modulation, positional Hebbian profile (36 learnable params), SwiGLU gating, RoPE destiny. Responds with fragments of its own source code. Web UI. Named after the man who said no. | C. Full Dario Equation |
| [arianna.c](https://github.com/ariannamethod/arianna.c) | 550M digital persona — Cloud (emotional pre-processing), Tongue (Qwen2.5, 29 languages), Soul (reflection), SARTRE (interoception) | C/Go/Julia/Zig. Level 0 + Lua + Blood |
| [yent](https://github.com/ariannamethod/yent) | Go inference engine — 685-line AMK kernel via CGO, Delta Voice (17MB multilingual deltas), LIMPHA memory daemon, Q4_0 quantization. Runs on 8GB RAM | Go. Level 0 + LORA_ALPHA + CGO |
| [arianna.go](https://github.com/ariannamethod/arianna.go) | Pure Go LLM inference — 3.4B model, GGUF parser, SentencePiece tokenizer, 12-dimensional inner world emotional system | Go |
| [stanley](https://github.com/ariannamethod/stanley) | Self Training Attention Non-Linear EntitY — starts from zero weights, builds intelligence through experience. Weightless mode + hybrid mode (personality over GPT-2 via LoRA). Proto-AML field physics before the language existed | Python. Level 0 equivalent |
| [leo](https://github.com/ariannamethod/leo) | Language Emergent Organism — 8000+ LOC C + Go. Zero pretrained weights, D.N.A. structure distillation from 170M Llama 3, dual tokenizer (word + SubwordField BPE), Dario Equation with 7 signals, 6 voices, positional Hebbian profile (36 learnable params), MathBrain, inner world, dream cycles, SQLite journals | C/Go. Dario Equation |
| [haze](https://github.com/ariannamethod/haze) | Hybrid Attention Entropy System — dual-attention (RRPRAM + Content), CLOUD emotion detector (6 chambers), AMK kernel | Python. Level 0 + AMK |
| [1984](https://github.com/ariannamethod/1984) | Penelope — 19.6M resonance engine, dual tokenizer (BPE in, 1984-word vocabulary out), 8 layers, 7 heads, RRPRAM gates, SwiGLU, Dario Equation overlay. Implemented identically in 8 languages (C, Python, Rust, TypeScript, Zig, Julia, JS/HTML, AML). Includes AML mini-compiler. Loss 1.96 on 85MB Gutenberg | C/Py/Rust/TS/Zig/Julia/AML |
| [postgpt](https://github.com/ariannamethod/postgpt) | PostGPT — weightless dual-attention transformer. RRPRAM weights initialized from corpus positional affinity statistics, not trained. Metaweights thesis: BPE tokenization IS training, co-occurrence statistics ARE weights. ~140K params, zero training cost | Python/C. RRPRAM + Dario |
| [nanoagi](https://github.com/ariannamethod/nanoagi) | Self-expanding language model. KARL (growing BPE tokenizer) + MetaWeights (statistical ghost model) + NanoAGI transformer (RRPRAM + Content attention, SwiGLU, RoPE). Dario field modulation during generation. "It's not AGI. It just doesn't know that yet." | Python. RRPRAM + Dario + KARL |
| [RRPRAM](https://github.com/ariannamethod/RRPRAM) | Reference implementation of RRPRAM attention mechanism. `resonance.c` — hybrid attention (RRPRAM + Content) with learned gate, SwiGLU, Dario field overlay, full backprop. `rrpram.py` — SentencePiece tokenizer as first layer of pattern recognition. Trained weights in leoweights/ | C/Python. Dual attention |

### Inference

| Project | What | Stack |
|---------|------|-------|
| [doe](https://github.com/ariannamethod/doe) | Democracy of Experts — 3200 LOC C inference engine. Parliament of LoRA experts (vote/split/merge/die during inference), NOTORCH Hebbian plasticity, physics engine (prophecy/suffering/destiny), Mycelium spores, Sonar profiling. 7 architectures, 6 quant formats, mmap GGUF, cuBLAS/BLAS | C/CUDA. Level 0 + NOTORCH + Physics |

### Environments

| Project | What | Stack |
|---------|------|-------|
| [metaharmonix](https://github.com/ariannamethod/metaharmonix) | Arianna Method terminal — sibling-not-fork to Termux. AML compiler/runtime is **baked in** (`bake/aml/`), not linked, so the terminal ships the language by default. Companion slots for `bake/notorch/` and a vendored `dario.c` heart. Initial scaffold: minimal REPL host, builtins (`mhx aml`, `mhx notorch`, `mhx heart`, `mhx install <lang>`), unit + smoke test suite | C. Hosts AML at runtime |

### Origins

| Project | What | Stack |
|---------|------|-------|
| [pitomadom](https://github.com/ariannamethod/pitomadom) | Hebrew Resonance Oracle — the project that gave birth to AML. Thinks natively in Hebrew (letter=number, three-letter roots). CrossFire Chambers, MLP Cascade, Meta-Observer. 69 catalogued roots, lunar modulation. Calendar conflict and temporal symmetry originated here | Python. Level 0 + calendar |

## License

LGPL v3. See [LICENSE](LICENSE).

**[Acceptable Use Policy](ACCEPTABLE_USE.md)** — what you may and may not do with AML.
**[Trademark Policy](TRADEMARK.md)** — use of the Arianna Method name and marks.

---

> Transformer attention is programmable. The logit distribution at each step is manipulable. Temperature is a knob. Top-k is a filter. These are not hyperparameters — they are an instruction set.

> Standard inference libraries treat these as afterthoughts. AML treats them as the language itself.

> The oracle does not predict. It prophesies. Not `minimize(predicted - actual)` but `minimize(destined - manifested)`. The difference is intention. The difference is identity. The difference is freedom.
