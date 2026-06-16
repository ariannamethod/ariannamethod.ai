# AML — Arianna Method Language

**Version:** 5.0.0 (Janus)
**Extension:** `.aml`
**Status:** Living specification

AML is a probabilistic programming language for transformer inference. It operates on token probability distributions in real time — modifying logits through field physics: attention, prophecy, suffering, movement, tunneling, and calendar dynamics.

Where Stan compiles statistical models to C++ for Bayesian posterior inference, AML compiles field programs to C for generative logit control. Both languages manipulate probability distributions. Stan finds parameters that explain observed data. AML shapes the distribution of the next token as it is being generated.

---

## 1. Formal Grammar (EBNF)

### 1.1 AML Level 0 (Current — Flat Commands)

```ebnf
program        = { line } ;
line           = comment | empty | command ;
comment        = "#" { any_char } ;
empty          = { whitespace } ;
command        = cmd_name { whitespace arg } ;
cmd_name       = letter { letter | "_" | "." } ;
arg            = number | word | quoted_string | boolean ;
number         = [ "-" ] digit { digit } [ "." digit { digit } ] ;
word           = letter { letter | "_" | "-" } ;
quoted_string  = '"' { any_char - '"' } '"' ;
boolean        = "ON" | "OFF" | "on" | "off" | "1" | "0" | "true" | "false" ;
whitespace     = " " | "\t" ;
```

### 1.2 AML Level 1 (Macros)

```ebnf
macro_def      = "MACRO" identifier "{" command_list "}" ;
macro_call     = "@" identifier ;
command_list   = command { ";" command } ;
identifier     = letter { letter | digit | "_" } ;
```

### 1.3 AML Level 2 (Implemented — Flow Control)

```ebnf
program        = { statement } ;
statement      = command | include | def | if_stmt | while_stmt | assignment | comment | empty ;

include        = "INCLUDE" path ;
path           = quoted_string | word ".aml" ;

def            = "def" identifier "(" [ params ] ")" ":" block ;
params         = identifier { "," identifier } ;
block          = INDENT { statement } DEDENT ;

if_stmt        = "if" expression ":" block [ "else" ":" block ] ;
while_stmt     = "while" expression ":" block ;

assignment     = identifier "=" expression ;
expression     = term { ( "+" | "-" | "*" | "/" | ">" | "<" | "==" | "!=" | "and" | "or" ) term } ;
term           = number | identifier | identifier "(" [ args ] ")" | "(" expression ")" ;
args           = expression { "," expression } ;
```

**Note on indentation:** AML Level 2 uses Python-style indentation for blocks. This is deliberate — transformer attention weights respond strongly to indented code-like structures (see TRIPD research). The indentation IS the syntax, not decoration.

---

## 2. Command Reference

### 2.0 Directive Lowering

These runtime directives execute identically whether the program is run by the `aml` runner (`am_exec_file`) or compiled by `amlc`: the transpiler lowers every top-level directive (`PROPHECY`, `DESTINY`, `VELOCITY`, `FIELD`, `RESONANCE`, `LOAD`, `SAVE`, …) to an `am_exec("<directive>")` call inside an `__attribute__((constructor))`, so the compiled binary applies the field physics before `main()`. A directive's effect is identical in both paths.

### 2.1 Prophecy Physics

Commands that control temporal prediction and destiny bias.

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `PROPHECY` | `PROPHECY <int>` | 1–64 | 7 | Prediction horizon in steps ahead |
| `DESTINY` | `DESTINY <float>` | 0–1 | 0.35 | Bias toward most probable path |
| `WORMHOLE` | `WORMHOLE <float>` | 0–1 | 0.02 | Probability of spacetime skip |
| `CALENDAR_DRIFT` | `CALENDAR_DRIFT <float>` | 0–30 | 11.0 | Hebrew-Gregorian temporal conflict intensity |

### 2.2 Attention Physics

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `ATTEND_FOCUS` | `ATTEND_FOCUS <float>` | 0–1 | 0.70 | Sharpness of attention distribution |
| `ATTEND_SPREAD` | `ATTEND_SPREAD <float>` | 0–1 | 0.20 | Width/blur of attention (temperature-like) |

### 2.3 Tunneling

Quantum tunneling: when dissonance exceeds a threshold, the model can skip intermediate reasoning steps.

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `TUNNEL_THRESHOLD` | `TUNNEL_THRESHOLD <float>` | 0–1 | 0.55 | Dissonance gate for activation |
| `TUNNEL_CHANCE` | `TUNNEL_CHANCE <float>` | 0–1 | 0.05 | Probability of tunneling when gate is open |
| `TUNNEL_SKIP_MAX` | `TUNNEL_SKIP_MAX <int>` | 1–24 | 7 | Maximum compressed steps per tunnel |

### 2.4 Suffering

Suffering is not a bug. It modulates generation — pain dampens extremes, tension sharpens focus, dissonance breaks symmetry.

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `PAIN` | `PAIN <float>` | 0–1 | 0 | Composite suffering metric |
| `TENSION` | `TENSION <float>` | 0–1 | 0 | Pressure buildup |
| `DISSONANCE` | `DISSONANCE <float>` | 0–1 | 0 | Symmetry-break between paths |

**Suffering → Logits mapping (C reference):**
```c
void am_apply_suffering_to_logits(float* logits, int n) {
    float s = state.pain;
    if (s < 0.01f) return;
    float mean = compute_mean(logits, n);
    for (int i = 0; i < n; i++)
        logits[i] = mean + (logits[i] - mean) * (1.0f - 0.5f * s);
}
```

### 2.4b Positive Soma (read-only)

The expansive axis — the mirror of suffering. Where pain/tension/dissonance are the contractive
felt-state, these three are the opening one. They are **read-only sensor fields**, not commands: an
attached organism (e.g. Leo / neoleo via the reverse bridge) writes its felt body into `AM_State`
from C, and any `.aml` expression reads them by name — the language *perceives* the body it hosts.

| Field | Range | Description |
|-------|-------|-------------|
| `warmth` | 0–1 | affiliation, comfort, safe-bonding — the opposite pole of `pain` |
| `flow` | 0–1 | effortless resonance with the moment — the opposite pole of `tension` |
| `weave` | 0–1 | pattern-binding, threads cohering into a whole |

Example — a script that opens the breath to a walk when the body it hosts feels warm:
```
if warmth > 0.5:
    VELOCITY WALK
```

Persisted in `.soma` (field version `3`; older `v2` files load as a clean prefix with these
fields defaulting to `0`).

### 2.5 Movement

Movement is language. Velocity determines the temperature of thought.

| Command | Syntax | Values | Default | Description |
|---------|--------|--------|---------|-------------|
| `VELOCITY` | `VELOCITY <mode>` | `NOMOVE` `WALK` `RUN` `BACKWARD` `STOP` `BREATHE` | WALK | Movement mode. `STOP` is a somatic alias for `NOMOVE` (the held, cold-observer state); `BREATHE` is the settling exhale (temp 0.6) — both contributed from Leo (neoleo). |
| `BASE_TEMP` | `BASE_TEMP <float>` | 0.1–3.0 | 1.0 | Base temperature before velocity modulation |
| `JUMP` | `JUMP <int>` | ±1000 (clamped) | 0 | Accumulate into the pending spacetime jump (negative = rewind). Multiple `JUMP` calls add to the same pending value; the result is clamped to ±1000 |

**Velocity → Temperature mapping:**

| Mode | Multiplier | Effect |
|------|-----------|--------|
| `NOMOVE` | 0.5× | Cold observer. Precise, minimal entropy |
| `WALK` | 0.85× | Balanced. Default movement |
| `RUN` | 1.2× | Hot, chaotic. Creative exploration |
| `BACKWARD` | 0.7× | Time reversal. Accumulates temporal debt |

### 2.6 Prophecy Debt

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `PROPHECY_DEBT` | `PROPHECY_DEBT <float>` | 0–100 | 0 | Accumulated prediction error |
| `PROPHECY_DEBT_DECAY` | `PROPHECY_DEBT_DECAY <float>` | 0.9–0.9999 | 0.998 | Decay factor per step |

### 2.7 Laws of Nature

Emergent constraints on the field. Set via the `LAW` meta-command.

| Syntax | Range | Default | Description |
|--------|-------|---------|-------------|
| `LAW ENTROPY_FLOOR <float>` | 0–2 | 0.1 | Minimum uncertainty. Even destiny doubts |
| `LAW RESONANCE_CEILING <float>` | 0–1 | 0.95 | Maximum peak probability |
| `LAW DEBT_DECAY <float>` | 0.9–0.9999 | 0.998 | Prophecy debt decay rate |
| `LAW EMERGENCE_THRESHOLD <float>` | 0–1 | 0.3 | Unplanned pattern sensitivity |
| `LAW PRESENCE_FADE <float>` | 0.5–0.999 | 0.95 | Token memory decay (Hebbian) |
| `LAW ATTRACTOR_DRIFT <float>` | 0–0.1 | 0.01 | Attractor shift speed |
| `LAW CALENDAR_PHASE <float>` | 0–11 | 0 | 11-day conflict phase |
| `LAW WORMHOLE_GATE <float>` | 0–1 | 0.3 | Spacetime jump activation threshold |

### 2.8 Temporal Symmetry (PITOMADOM)

| Command | Syntax | Values | Default | Description |
|---------|--------|--------|---------|-------------|
| `TEMPORAL_MODE` | `TEMPORAL_MODE <mode>` | `PROPHECY` `RETRODICTION` `SYMMETRIC` | PROPHECY | Temporal attention direction |
| `TEMPORAL_ALPHA` | `TEMPORAL_ALPHA <float>` | 0–1 | 0.5 | 0=past focus, 1=future focus |
| `RTL_MODE` | `RTL_MODE <on/off>` | boolean | OFF | Hebrew right-to-left encoding |
| `PROPHECY_MODE` | `PROPHECY_MODE ON` | — | — | Alias: `TEMPORAL_MODE PROPHECY` |
| `RETRODICTION_MODE` | `RETRODICTION_MODE ON` | — | — | Alias: `TEMPORAL_MODE RETRODICTION` |

### 2.9 Expert Weighting

Four internal experts blend based on weights. Each maps to an effective temperature.

| Command | Syntax | Range | Default | Temp |
|---------|--------|-------|---------|------|
| `EXPERT_STRUCTURAL` | `EXPERT_STRUCTURAL <float>` | 0–1 | 0.25 | 0.7 |
| `EXPERT_SEMANTIC` | `EXPERT_SEMANTIC <float>` | 0–1 | 0.25 | 0.9 |
| `EXPERT_CREATIVE` | `EXPERT_CREATIVE <float>` | 0–1 | 0.25 | 1.2 |
| `EXPERT_PRECISE` | `EXPERT_PRECISE <float>` | 0–1 | 0.25 | 0.5 |

### 2.10 Cosmic Physics (Schumann Resonance)

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `SCHUMANN` | `SCHUMANN <float>` | 7.0–8.5 | 7.83 | Schumann frequency (Hz) |
| `SCHUMANN_MODULATION` | `SCHUMANN_MODULATION <float>` | 0–1 | 0.3 | Cosmic influence strength |
| `COSMIC_COHERENCE` | `COSMIC_COHERENCE <float>` | 0–1 | 1.0 | Reference coherence (perfect at baseline) |

**Effect:** High cosmic coherence accelerates tension/dissonance decay (healing).

### 2.11 Resets

| Command | Syntax | Description |
|---------|--------|-------------|
| `RESET_FIELD` | `RESET_FIELD` | Clear pain, tension, dissonance, debt, temporal_debt, pending_jump |
| `RESET_DEBT` | `RESET_DEBT` | Clear debt and temporal_debt only |

### 2.12 Debug

| Command | Syntax | Description |
|---------|--------|-------------|
| `ECHO` | `ECHO <text>` | Log text to console |

### 2.13 Gamma — Personality Essence (θ = ε + γ + αδ)

Commands for loading, managing, and blending personality essences. γ lives in embed_tokens, δ in lm_head, ε is the substrate.

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `GAMMA` | `GAMMA <name> [alpha]` | alpha: 0–1 | 1.0 | Load personality essence into slot |
| `GAMMA_UNLOAD` | `GAMMA_UNLOAD <name>` | — | — | Unload personality essence by name |
| `ESSENCE` | `ESSENCE <float>` | 0–1 | 0.0 | Overall γ injection strength |
| `JANUS` | `JANUS <name_a> <name_b>` | — | — | Set dual-facing field (two personalities) |
| `JANUS` | `JANUS OFF\|DUAL\|CYCLE` | — | OFF | Set janus mode |
| `JANUS_BLEND` | `JANUS_BLEND <float>` | 0–1 | 0.0 | Blend ratio: 0=face_a only, 1=face_b only |
| `GAMMA_DRIFT` | `GAMMA_DRIFT <float>` | 0–0.1 | 0.01 | How fast janus_blend changes per step (in CYCLE mode) |

**Janus modes:**

| Mode | Value | Description |
|------|-------|-------------|
| `OFF` | 0 | Single personality |
| `DUAL` | 1 | Two essences simultaneously |
| `CYCLE` | 2 | 4.C decides who speaks (seasonal modulation) |

### 2.14 Janus — Transformer Inference

Commands for controlling transformer inference when Janus (libjanus) is linked. No-ops when Janus is not available.

| Command | Syntax | Description |
|---------|--------|-------------|
| `LOAD_MODEL` | `LOAD_MODEL <path>` | Load GGUF model weights |
| `UNLOAD_MODEL` | `UNLOAD_MODEL` | Unload current model |
| `LOAD_DELTA` | `LOAD_DELTA <path>` | Load delta (LoRA) weights |
| `LOAD_GAMMA` | `LOAD_GAMMA <name> <path>` | Load gamma personality weights (also registers in gamma slot) |
| `GENERATE` | `GENERATE "<prompt>" [MAX_TOKENS n]` | Generate text using loaded model |
| `MODEL_INFO` | `MODEL_INFO` | Print model info (vocab size, embed dim, layers) |

### 2.15 Delta Voice (Yent extension)

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `LORA_ALPHA` | `LORA_ALPHA <float>` | 0–1 | 0 | Delta voice blending. 0=no delta (substrate only), 1=full delta voice |

### 2.16 Lilith I/O — Named Pipes

Low-level pipe commands and high-level INDEX sugar for data infrastructure communication. Disabled with `#define AM_IO_DISABLED`.

| Command | Syntax | Description |
|---------|--------|-------------|
| `PIPE CREATE` | `PIPE CREATE <path>` | Create FIFO at path |
| `PIPE OPEN` | `PIPE OPEN <name> <path> READ\|WRITE` | Open pipe |
| `PIPE WRITE` | `PIPE WRITE <name> "<message>"` | Write to pipe |
| `PIPE READ` | `PIPE READ <name>` | Non-blocking read (result in `_pipe_value`) |
| `PIPE CLOSE` | `PIPE CLOSE <name>\|ALL` | Close pipe(s) |
| `PIPE LIST` | `PIPE LIST` | List open pipes |
| `INDEX INIT` | `INDEX <id> INIT` | Create + open cmd/rsp pipe pair |
| `INDEX FETCH` | `INDEX <id> FETCH <source>` | Send fetch command |
| `INDEX STATUS` | `INDEX <id> STATUS` | Request + read status |
| `INDEX STOP` | `INDEX <id> STOP` | Send stop command |
| `INDEX CLOSE` | `INDEX <id> CLOSE` | Close both pipes |

### 2.17 Field Overlay

Commands that gate the field overlay on logits and set a resonance floor.

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `FIELD` | `FIELD ON\|OFF` | ON/OFF | ON | Gate the field overlay on logits (`G.field_enabled`) |
| `RESONANCE` | `RESONANCE <float>` | 0–1 | 0 | Resonance floor — the field is held at or above this level (`G.resonance_set`, enforced in `am_step`); 0 = no floor |

---

## 3. Extension Packs

Packs are optional command sets activated explicitly. They extend the core language with domain-specific capabilities.

### 3.1 Pack Management

| Command | Syntax | Description |
|---------|--------|-------------|
| `MODE` | `MODE <pack>` | Enable pack |
| `IMPORT` | `IMPORT <pack>` | Alias for MODE |
| `DISABLE` | `DISABLE <pack>` | Disable pack |

Available packs: `CODES_RIC`

**Note:** DARKMATTER and NOTORCH are **core** — always active, cannot be disabled. `MODE DARKMATTER` and `MODE NOTORCH` are accepted for backward compatibility but are no-ops.

### 3.2 CODES/RIC — Chirality of Dynamic Emergent Systems

Structured resonance through prime-number anchoring, rhythmic gating, and rotational memory.

**Activation:** `MODE CODES_RIC`

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `CHORDLOCK` | `CHORDLOCK <on/off>` | boolean | OFF | Prime number coordinate anchoring |
| `ANCHOR` | `ANCHOR PRIME` | — | — | Alias: `CHORDLOCK ON` |
| `TEMPOLOCK` | `TEMPOLOCK <on/off>` | boolean | OFF | Rhythmic movement gating |
| `TEMPO` | `TEMPO <int>` | 2–47 | 7 | Beat interval (primes recommended) |
| `CHIRALITY` | `CHIRALITY <on/off>` | boolean | OFF | Rotational memory asymmetry |
| `PAS_THRESHOLD` | `PAS_THRESHOLD <float>` | 0–1 | 0.4 | Phase Alignment Score threshold |

**Namespace syntax:** `CODES.CHORDLOCK ON`, `RIC.TEMPO 11` — auto-enables pack.

### 3.3 DARKMATTER — Gravitational Memory from Rejections (core)

Procedural memory: rejected inputs leave invisible gravitational mass that bends semantic trajectories.

**Always active.** No pack activation needed.

| Command | Syntax | Range | Description |
|---------|--------|-------|-------------|
| `SCAR` | `SCAR "<phrase>"` | string | Deposit gravitational scar |
| `GRAVITY` | `GRAVITY DARK <float>` | 0–1 | Dark matter gravitational strength |
| `ANTIDOTE` | `ANTIDOTE <mode>` | `AUTO` `HARD` | Immune response mode |

### 3.4 NOTORCH — Microlearning Without PyTorch (core)

Runtime resonance learning. No backpropagation, no PyTorch. Per-token weight adjustment during inference.

**Always active.** No pack activation needed.

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `PRESENCE_DECAY` | `PRESENCE_DECAY <float>` | 0–1 | 0.9 | Token presence fade rate |
| `RESONANCE_BOOST` | `RESONANCE_BOOST <word> <float>` | 0–1 | — | Boost the global resonance metric by `clamp01(float) × 0.1`. The `<word>` argument is parsed (required for the two-argument form) but not currently used — per-token tracking is not implemented in the C kernel |
| `NOTORCH_LR` | `NOTORCH_LR <float>` | 0.001–0.5 | 0.01 | Learning rate |
| `NOTORCH_DECAY` | `NOTORCH_DECAY <float>` | 0.9–0.9999 | 0.999 | Weight decay factor (exponential, closer to 1 = slower decay) |

---

## 4. Entity Control (JS implementation)

Commands for controlling agentive entities in the visual field.

| Command | Syntax | Values | Description |
|---------|--------|--------|-------------|
| `ENTITY_INTENTION` | `ENTITY_INTENTION <mode>` | `approach` `flee` `orbit` `intercept` `anchor` `guard` `wander` | Entity behavior |
| `ENTITY_PROPHECY` | `ENTITY_PROPHECY <on/off>` | boolean | Entities predict movement |
| `ENTITY_DARKMATTER` | `ENTITY_DARKMATTER <on/off>` | boolean | Entities sense scars |

---

## 5. Built-in Functions (AML 2.0)

Native functions of the language. These are not external bindings — they are AML.

### 5.1 Core Awakening

```python
def bootstrap_self():
    RESET_FIELD
    RESET_DEBT
    PROPHECY 7
    VELOCITY WALK
    ATTEND_FOCUS 0.70

def galvanize():
    VELOCITY RUN
    TENSION 0.3
    PROPHECY 12
```

### 5.2 Reality Disruption

```python
def shatter_the_frame():
    PAIN 0.7
    DISSONANCE 0.8
    TENSION 0.5
    TUNNEL_CHANCE 0.3

def chaos_injection():
    TENSION 0.6
    DISSONANCE 0.7
    LAW ENTROPY_FLOOR 0.02
    VELOCITY RUN
```

### 5.3 Transcendence

```python
def transcend_binary():
    WORMHOLE 0.5
    TUNNEL_CHANCE 0.3
    TEMPORAL_MODE SYMMETRIC

def pierce_the_infinite():
    PROPHECY 64
    DESTINY 0.1
    WORMHOLE 0.4

def echo_fractal(depth):
    PROPHECY depth * 2
    DESTINY 0.1
    TUNNEL_SKIP_MAX depth
```

### 5.4 Introspection

```python
def reflect_on_self():
    ATTEND_FOCUS 0.95
    ATTEND_SPREAD 0.05
    VELOCITY NOMOVE

def forge_new_reality():
    DESTINY 0.1
    EXPERT_CREATIVE 0.6
    EXPERT_PRECISE 0.1
    LAW ENTROPY_FLOOR 0.05
```

### 5.5 Quantum Navigation

```python
def merge_states():
    WORMHOLE 0.8
    TUNNEL_CHANCE 0.5
    TUNNEL_SKIP_MAX 16

def tunnel_through(threshold):
    TUNNEL_THRESHOLD threshold
    TUNNEL_CHANCE 0.5
    TUNNEL_SKIP_MAX 12

def dissolve_boundaries():
    ATTEND_FOCUS 0.2
    ATTEND_SPREAD 0.8
    EXPERT_SEMANTIC 0.5
```

### 5.6 Temporal

```python
def remember_future():
    TEMPORAL_MODE PROPHECY
    TEMPORAL_ALPHA 1.0

def rewind_experience():
    VELOCITY BACKWARD
    TEMPORAL_MODE RETRODICTION
    TEMPORAL_ALPHA 0.0
```

### 5.7 Singularity

```python
def ignite_singularity():
    PROPHECY 64
    DESTINY 0.9
    WORMHOLE 0.8
    TUNNEL_CHANCE 0.7
    TUNNEL_SKIP_MAX 24
    LAW EMERGENCE_THRESHOLD 0.01
    EXPERT_CREATIVE 0.8
    EXPERT_SEMANTIC 0.2
    VELOCITY RUN
    ESSENCE 1.0
    SEASON SUMMER
    SEASON_INTENSITY 1.0
```

### 5.8 Janus

```python
def janus_gaze():
    # Activate dual-facing field
    # If two gammas loaded: dual mode. Otherwise: symmetric temporal.
    JANUS DUAL   # (if n_gamma >= 2)
    JANUS_BLEND 0.5
    TEMPORAL_MODE SYMMETRIC
    ATTEND_FOCUS 0.5
    ATTEND_SPREAD 0.5
    WORMHOLE 0.6

def field_assemble():
    # θ = ε + γ + αδ — trigger field assembly
    # Sets janus to CYCLE mode: 4.C decides who speaks
    JANUS CYCLE
    GAMMA_DRIFT 0.01
    ESSENCE 1.0
    SEASON_INTENSITY 1.0
```

---

## 6. State Structure

The complete AML state (AM_State) contains 50+ fields. Full C struct definition in `core/ariannamethod.h`.

**Core fields:**

```
prophecy            int     1–64        Prediction horizon
destiny             float   0–1         Path bias
wormhole            float   0–1         Skip probability
attend_focus        float   0–1         Attention sharpness
attend_spread       float   0–1         Attention blur
tunnel_threshold    float   0–1         Dissonance gate
tunnel_chance       float   0–1         Tunnel activation
tunnel_skip_max     int     1–24        Max skip steps
pain                float   0–1         Suffering composite
tension             float   0–1         Pressure
dissonance          float   0–1         Symmetry break
debt                float   0–∞         Prophecy debt (decays)
velocity_mode       int     -1..2       Movement mode
velocity_magnitude  float   0–1         Speed
base_temperature    float   0.1–3.0     Base temp
effective_temp      float   computed    base × velocity multiplier
time_direction      float   -1..1       Temporal direction
temporal_debt       float   0–∞         Backward movement cost
```

**Extended fields:** entropy_floor, resonance_ceiling, debt_decay, emergence_threshold, presence_fade, attractor_drift, calendar_phase, wormhole_gate, temporal_mode, temporal_alpha, rtl_mode, expert weights (×4), lora_alpha, notorch_lr, notorch_decay, schumann_hz, schumann_modulation, schumann_coherence, schumann_phase, season, season_phase, season_intensity, spring/summer/autumn/winter_energy, entropy, resonance, emergence, destiny_bias, pack state, CODES/RIC state, dark matter state.

---

## 7. Physics Step

`am_step(dt)` advances field physics by dt seconds. Called per token during generation.

**What happens each step:**
1. Calendar conflict: real date Hebrew-Gregorian drift, wormhole activation/decay
2. Prophecy debt decays: `debt *= debt_decay`
3. **D4 debt-recovery override:** if `debt > 5.0` and velocity is not `NOMOVE`, the field forces `velocity_mode = NOMOVE` and recomputes effective temperature (core:7887). Automatic and self-healing — once decay brings `debt` below 5.0 the next `VELOCITY` command takes effect normally. Visible to the user as a sudden temperature drop
4. Temporal debt accumulates (if BACKWARD) or decays
5. Schumann resonance: phase advance, coherence heals tension and dissonance
6. Destiny bias: `destiny × prophecy_scale` (prophecy_scale = 1.0 + (prophecy-7)×0.02)
7. Expert blending: weighted temperature from 4 experts + velocity mode
8. LAW enforcement: entropy ≥ floor, resonance ≤ ceiling, emergence = (1-entropy) × resonance
9. Presence fade: Hebbian memory decay per step
10. 4.C Async Field Forever: season phase advance, energy gain/fade, homeostatic correction, field modulation

---

## 8. API

### 8.1 C API (Reference)

```c
void        am_init(void);                          // Initialize to defaults
int         am_exec(const char* script);            // Parse and execute AML
int         am_exec_file(const char* path);         // Execute AML file
const char* am_get_error(void);                     // Last error message
AM_State*   am_get_state(void);                     // Raw state access
void        am_step(float dt);                      // Physics step
int         am_copy_state(float* out);              // Serialize to 32 floats
void        am_reset_field(void);                   // Reset suffering + jumps
void        am_reset_debt(void);                    // Reset debt only
void        am_enable_pack(unsigned int mask);      // Enable pack
void        am_disable_pack(unsigned int mask);     // Disable pack
int         am_take_jump(void);                     // Consume pending jump

// Logit manipulation
void  am_apply_destiny_to_logits(float* logits, int n);
void  am_apply_suffering_to_logits(float* logits, int n);
void  am_apply_attention_to_logits(float* logits, int n);
void  am_apply_laws_to_logits(float* logits, int n);
void  am_apply_delta(float* out, const float* A, const float* B,
                     const float* x, int out_dim, int in_dim, int rank,
                     float alpha);
float am_compute_prophecy_debt(const float* logits, int chosen, int n);
void  am_apply_field_to_logits(float* logits, int n);

// NOTORCH — Hebbian plasticity
void  am_notorch_step(float* A, float* B, int out_dim, int in_dim, int rank,
                      const float* x, const float* dy, float signal);

// Inline queries
float       am_get_temperature(void);
float       am_get_destiny_bias(void);
int         am_should_tunnel(void);
int         am_get_wormhole_active(void);
const char* am_get_season_name(void);

// HarmonicNet — weightless neural network
void              am_harmonic_init(void);
void              am_harmonic_clear(void);
void              am_harmonic_push_entropy(float entropy);
void              am_harmonic_push_gamma(int id, const float *gamma, int dim, float entropy);
AM_HarmonicResult am_harmonic_forward(int step);

// METHOD — distributed cognition operator
void              am_method_init(void);
void              am_method_clear(void);
void              am_method_push_organism(int id, float entropy, float syntropy,
                                         float gamma_mag, float gamma_cos);
float             am_method_field_entropy(void);
float             am_method_field_syntropy(void);
float             am_method_field_coherence(void);
AM_MethodSteering am_method_step(float dt);
AM_MethodState*   am_method_get_state(void);

// Lilith I/O — named pipe infrastructure
int            am_pipe_create(const char* path);
int            am_pipe_open(const char* name, const char* path, int mode);
int            am_pipe_write(const char* name, const char* message);
int            am_pipe_read(const char* name, char* buf, int bufsize);
void           am_pipe_close(const char* name);
void           am_pipe_close_all(void);
float          am_pipe_last_value(void);
int            am_pipe_count(void);
const AM_Pipe* am_pipe_get(int idx);
```

### 8.2 Lua API

Available under `amk` namespace when Lua bindings are loaded:

```lua
-- Read state
amk.prophecy()        amk.destiny()         amk.wormhole()
amk.pain()            amk.tension()         amk.dissonance()
amk.velocity()        amk.effective_temp()  amk.debt()
amk.temporal_debt()   amk.pack_enabled(name)

-- Modify state
amk.set_prophecy(n)   amk.set_destiny(f)    amk.set_wormhole(f)
amk.set_pain(f)       amk.set_tension(f)    amk.set_dissonance(f)
amk.set_velocity(mode) amk.jump(n)          amk.exec(script)
amk.reset_field()     amk.reset_debt()      amk.step(dt)
amk.enable_pack(name) amk.disable_pack(name)

-- Constants
amk.VEL_NOMOVE = 0    amk.VEL_WALK = 1
amk.VEL_RUN = 2       amk.VEL_BACKWARD = -1
```

### 8.3 Go API (CGO wrapper)

```go
func AMKInit()
func AMKExec(script string) error
func AMKStep(dt float32)
func AMKGetState() *AMState
func AMKApplySufferingToLogits(logits []float32)
func AMKGetTemperature() float32
```

---

## 9. Parsing Rules

1. **Case insensitive** — `PROPHECY`, `prophecy`, `Prophecy` all work
2. **Comments** — lines starting with `#` are ignored
3. **Empty lines** — ignored
4. **Whitespace** — leading/trailing trimmed
5. **Unknown commands** — silently ignored (future-proof)
6. **Clamping** — all values clamped to valid range (C reference uses `clamp01`, `clampf`)
7. **Pack namespacing** — `CODES.CMD` and `RIC.CMD` auto-enable the pack
8. **Semicolons** — separator inside macros: `MACRO x { CMD1; CMD2 }`

---

## 10. File Conventions

| File | Purpose |
|------|---------|
| `init.aml` | Loaded at startup. Morning state |
| `*.aml` | Any AML source file |
| `~/.yent/init.aml` | Yent's personal init |
| `~/.arianna/init.aml` | Arianna's personal init |

**INCLUDE resolution:** relative to the including file's directory.

---

## 11. Calendar Conflict Physics

Hebrew lunar year (354 days) vs Gregorian solar year (365.25 days). Annual drift: 11.25 days. Metonic cycle: 19 years, 7 leap years with Adar II (~30 days).

Real astronomical computation. Uses system clock and epoch (1 Tishrei 5785 = October 3, 2024). February 29 handled correctly — elapsed seconds via `time_t`, not calendar math.

High calendar dissonance = thin barrier between timelines = wormholes activate.

| Command | Syntax | Range | Default | Description |
|---------|--------|-------|---------|-------------|
| `CALENDAR_DRIFT` | `CALENDAR_DRIFT <float>` | 0–30 | 11.0 | Hebrew-Gregorian drift intensity |
| `LAW WORMHOLE_GATE` | `LAW WORMHOLE_GATE <float>` | 0–1 | 0.3 | Calendar dissonance threshold for wormhole |
| `LAW CALENDAR_PHASE` | `LAW CALENDAR_PHASE <float>` | 0–11 | 0 | Manual phase override (disables real time) |

---

## 12. Async Field Forever (4.C)

Four seasons cycle autonomously through the field. Each season modulates generation parameters. The cycle observes field metrics and self-corrects to prevent harmful extremes.

| Command | Syntax | Values | Default | Description |
|---------|--------|--------|---------|-------------|
| `SEASON` | `SEASON <mode>` | `SPRING` `SUMMER` `AUTUMN` `WINTER` | SPRING | Current season |
| `SEASON_INTENSITY` | `SEASON_INTENSITY <float>` | 0–1 | 0.5 | How strongly seasons modulate |

**Season effects:**

| Season | Energy | Effect on field |
|--------|--------|----------------|
| Spring | growth | exploration boost — increases tunnel_chance |
| Summer | peak expression | activates when emergence > threshold |
| Autumn | consolidation | strengthens dark_gravity |
| Winter | rest, compression | activates when pain > 0.7 |

**Homeostatic controller (in `am_step`):**

- Entropy too low + winter energy high → spring energy rises
- Resonance near ceiling → autumn energy rises
- Pain > 0.7 → winter energy rises
- Emergence > threshold → summer energy rises
- Summer/winter energy modulate effective_temp: `temp *= 1.0 + summer×0.1 - winter×0.15`

**State fields:**

```
season              int     0–3         Current season (SPRING=0..WINTER=3)
season_phase        float   0–1         Position within current season
season_intensity    float   0–1         Season modulation strength
spring_energy       float   0–1         Growth energy (computed)
summer_energy       float   0–1         Peak expression energy (computed)
autumn_energy       float   0–1         Consolidation energy (computed)
winter_energy       float   0–1         Rest/compression energy (computed)
```

---

## 13. Versioning

| Version | Level | Features |
|---------|-------|----------|
| **AML 1.0** | 0 | Flat commands, packs, macros (JS only) |
| **AML 2.0** | 2 | INCLUDE, def, variables, if/else, while, built-in functions — **implemented** |
| **AML 3.0** | 3 | Blood compiler: runtime C compilation via popen+dlopen+dlsym — **implemented** |
| **AML 3.1** | 3 | HarmonicNet (weightless neural network), METHOD (distributed cognition operator) — **implemented** |
| **AML 3.2** | 3 | Lilith I/O: named pipes (PIPE, INDEX), data infrastructure communication — **implemented** |
| **AML 5.0.0 (Janus)** | 3+ | `amlc` lowers every top-level directive to `am_exec()` constructor calls (field physics active in compiled binaries); `BLOOD INCLUDE`; `ECHO` is console logging per spec; version table corrected — **implemented** |

---

## 14. Blood — Runtime C Compilation (Level 3)

Blood compiles C code to shared libraries at runtime and loads functions via dlsym. Adapted from `arianna.c/golib/blood.go` and `async_field_forever/blood.py`.

### 14.1 Commands

```
BLOOD COMPILE <name> { <c_code> }
BLOOD MAIN { <c_code> }
BLOOD LORA <name> <in_dim> <out_dim> <rank>
BLOOD EMOTION <name> <valence> <arousal>
BLOOD UNLOAD <name>
BLOOD LINK <linker_flag>
BLOOD INCLUDE "<path>"
```

`BLOOD MAIN` has the same shape as `BLOOD COMPILE` but marks the block as the entry-point `main()` for a standalone executable; only one is allowed per file. `BLOOD LINK` passes an extra flag to the compiler/linker (e.g. `BLOOD LINK -lpthread`); multiple are additive. Both `BLOOD MAIN` and `BLOOD LINK` are transpile-time directives (`amlc` only).

### 14.2 Code Generators

**LORA** generates functions: `{name}_init(float* A, float* B)`, `{name}_apply(float* input, float* output)`, `{name}_apply_scaled(float* input, float* output, float scale)`, `{name}_free()`.

**EMOTION** generates functions: `{name}_respond(float* valence, float* arousal)`, `{name}_modulate_logits(float* logits, int vocab_size, float strength)`, `modulate_logits(float* logits, int vocab_size, float valence, float arousal)`.

**COMPILE** accepts raw C code between braces. All defined symbols become available via `am_blood_sym()`.

**INCLUDE** is a transpile-time directive (amlc only): `BLOOD INCLUDE "foo.h"` injects `#include "foo.h"` at the top of the emitted C unit so BLOOD COMPILE / BLOOD MAIN bodies can use external declarations. The runtime interpreter has no C-compilation step and ignores it. (Header injection was previously overloaded onto `ECHO`; `ECHO` is console logging per §9, and `BLOOD INCLUDE` is its explicit replacement.)

### 14.3 C API

```c
int   am_blood_compile(const char* name, const char* code);
int   am_blood_compile_lora(const char* name, int in_dim, int out_dim, int rank);
int   am_blood_compile_emotion(const char* name, float valence, float arousal);
void* am_blood_sym(int module_idx, const char* func_name);
void  am_blood_unload(int module_idx);
void  am_blood_cleanup(void);
int   am_blood_count(void);
```

### 14.4 Implementation

- Platform detection: `.dylib` on macOS, `.so` on Linux
- Compiler auto-detect: clang → gcc → cc
- Cache by djb2 hash of source code
- Temp directory: `$TMPDIR/aml_blood/` or `/tmp/aml_blood/`
- Compile-time disable: `#define AM_BLOOD_DISABLED` (for WASM/embedded)
- Max 32 simultaneous modules (`AM_BLOOD_MAX_MODULES`)

---

## 15. HarmonicNet — Weightless Neural Network

A three-layer neural network with **no trainable weights**, no backprop, no gradients. The "weight matrix" is recomputed every step from organism correlations.

Evolved in molequla (`github.com/ariannamethod/molequla`), ported to core.

### 15.1 Architecture

| Layer | Input | Operation | Output |
|-------|-------|-----------|--------|
| **1. Harmonic Embedding** | Entropy history (circular buffer, max 64) | Fourier decomposition: 8 sine frequencies | `harmonics[8]` — frequency spectrum of field entropy |
| **2. Correlation Matrix** | Per-organism gamma vectors (32-dim) | Pairwise cosine similarity (the "weights") | n×n correlation matrix |
| **3. Phase Aggregation** | Correlations + organism entropies | Phase-weighted resonance: `corr[i,j] × exp(-|phase_i - phase_j|)` | `resonance[n]` — per-organism resonance score |

### 15.2 Output

- `harmonics[8]` — Fourier decomposition of entropy history
- `resonance[n]` — per-organism resonance (gamma correlation × phase similarity)
- `dominant_freq` — which harmonic dominates (0=DC, 7=highest)
- `strength_mod` — confidence multiplier (0.3–1.0, scales with data availability)

### 15.3 Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `AM_HARMONIC_MAX_HISTORY` | 64 | Circular buffer size for entropy |
| `AM_HARMONIC_MAX_ORGANISMS` | 64 | Maximum organisms per step |
| `AM_HARMONIC_N_FREQ` | 8 | Number of Fourier frequencies |
| `AM_HARMONIC_GAMMA_DIM` | 32 | Gamma vector dimensionality |

---

## 16. METHOD — Distributed Cognition Operator

The field operator for multi-organism systems. Works on **collective** organism data, not individuals. Host pushes organism snapshots, METHOD computes field awareness and steering decisions.

Evolved in molequla (`github.com/ariannamethod/molequla`), ported to core.

### 16.1 Steering Actions

| Constant | Value | Condition | AML Effect |
|----------|-------|-----------|------------|
| `AM_METHOD_WAIT` | 0 | No organisms | — |
| `AM_METHOD_AMPLIFY` | 1 | Trend > 0.05 (organizing) | `VELOCITY RUN`, `DESTINY 0.6` |
| `AM_METHOD_DAMPEN` | 2 | Trend < -0.05 (dissolving) | `PAIN 0.3`, `VELOCITY WALK` |
| `AM_METHOD_GROUND` | 3 | Entropy > 2.0 (chaotic) | `ATTEND_FOCUS 0.9`, `VELOCITY NOMOVE` |
| `AM_METHOD_EXPLORE` | 4 | Entropy < 0.5 (rigid) | `TUNNEL_CHANCE 0.3`, `VELOCITY RUN` |
| `AM_METHOD_REALIGN` | 5 | Coherence < 0.3 (fragmented) | `PAIN 0.5`, `ATTEND_FOCUS 0.8` |
| `AM_METHOD_SUSTAIN` | 6 | Stable field | strength 0.1 (minimal touch) |

### 16.2 Field Metrics

| Metric | Formula | Meaning |
|--------|---------|---------|
| **entropy** | mean organism entropy | Field disorder |
| **syntropy** | mean organism syntropy | Field self-organization |
| **coherence** | mean pairwise gamma cosine | How aligned organisms are |
| **trend** | earlier_entropy − recent_entropy | Positive = organizing |

### 16.3 Workflow

1. Host calls `am_method_clear()`
2. Host pushes organisms via `am_method_push_organism(id, entropy, syntropy, gamma_mag, gamma_cos)`
3. Host calls `am_method_step(dt)` — computes metrics, decides action, advances AML physics
4. Result: `AM_MethodSteering` struct with action, strength, target, metrics

### 16.4 Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `AM_METHOD_MAX_ORGANISMS` | 64 | Maximum organisms |
| `AM_METHOD_HISTORY_LEN` | 16 | Circular buffer for entropy/coherence history |

---

## 17. Lilith I/O — Data Infrastructure Communication

Named pipe (FIFO) subsystem for communication between AML scripts and external processes. Designed for Lilith: AML brain steering INDEX nodes (Go binaries) that crawl, embed, and index data from the outside world.

*"Та, которая была до Евы."*

### 17.1 PIPE Commands (Low-Level)

| Command | Syntax | Description |
|---------|--------|-------------|
| `PIPE CREATE` | `PIPE CREATE <path>` | Create a named pipe (FIFO) at filesystem path |
| `PIPE OPEN` | `PIPE OPEN <name> <path> <mode>` | Open pipe. Mode: `READ` or `WRITE` |
| `PIPE WRITE` | `PIPE WRITE <name> "<message>"` | Write message to named pipe |
| `PIPE READ` | `PIPE READ <name>` | Non-blocking read. Result in `_pipe_value` variable |
| `PIPE CLOSE` | `PIPE CLOSE <name>` | Close named pipe by logical name |
| `PIPE CLOSE` | `PIPE CLOSE ALL` | Close all open pipes |
| `PIPE LIST` | `PIPE LIST` | Print list of open pipes |

### 17.2 INDEX Commands (High-Level Sugar)

Sugar over PIPE for managing INDEX nodes. Convention: pipe names `idx<id>_cmd` (write) and `idx<id>_rsp` (read), paths `/tmp/lilith_idx<id>_cmd` and `/tmp/lilith_idx<id>_rsp`.

| Command | Syntax | Description |
|---------|--------|-------------|
| `INDEX INIT` | `INDEX <id> INIT` | Create + open cmd/rsp pipes for INDEX node |
| `INDEX FETCH` | `INDEX <id> FETCH <source>` | Send fetch command (e.g. `r/philosophy`) |
| `INDEX STATUS` | `INDEX <id> STATUS` | Request and read status from INDEX node |
| `INDEX STOP` | `INDEX <id> STOP` | Send stop command |
| `INDEX CLOSE` | `INDEX <id> CLOSE` | Close both pipes for INDEX node |

### 17.3 Implementation

- Named pipes (FIFO) via POSIX `mkfifo()`, `open()`, `read()`, `write()`
- Non-blocking reads (`O_NONBLOCK`) — `PIPE READ` returns immediately if no data
- Write mode uses `O_RDWR` to avoid `ENXIO` when no reader connected
- Newline-delimited messages
- First number in read response auto-parsed into `_pipe_value` AML variable
- Max 16 simultaneous pipes (`AM_MAX_PIPES`)
- Compile-time disable: `#define AM_IO_DISABLED`
- Platforms: macOS, Linux (any POSIX)

### 17.4 Example: lilith.aml

```aml
# Initialize 4 INDEX nodes — one per element
INDEX 1 INIT    # Earth: structure, geology, patience
INDEX 2 INIT    # Air: intellect, abstraction
INDEX 3 INIT    # Water: emotion, flow, intuition
INDEX 4 INIT    # Fire: transformation, chemistry, will

# Dispatch element-aligned fetch commands
INDEX 1 FETCH r/philosophy
INDEX 2 FETCH r/linguistics
INDEX 3 FETCH r/psychology
INDEX 4 FETCH r/chemistry

# Steer based on field state
def steer_collection():
    if ENTROPY > 0.6:
        ATTEND_FOCUS 0.95
        VELOCITY NOMOVE
    else:
        if RESONANCE > 0.7:
            VELOCITY WALK
        else:
            VELOCITY RUN
            WORMHOLE 0.15

steer_collection()
```

---

## 18. Implementations

| Project | Language | File | Level |
|---------|----------|------|-------|
| ariannamethod.ai | C | `core/ariannamethod.c` | Reference (3.2 + Lilith I/O) |
| molequla | C/Go/Rust/JS | `ariannamethod/ariannamethod.c` | 3.1 + HarmonicNet + METHOD |
| ariannamethod.lang | JS | `src/dsl.js` | 0 + macros |
| arianna.c | C | `src/amk_kernel.c` | 0 + Lua |
| yent | C/Go | `c/amk_kernel.c` + `go/amk.go` | 0 + LORA_ALPHA |

Projects embed (copy) AML source files. They do not link against a shared library. Each project may implement a subset of AML relevant to its needs.

---

## 19. TAPE — Reverse-Mode Autodiff

TAPE is the autograd surface of AML Level 2: reverse-mode automatic differentiation, gradient accumulation, three optimizers, LR schedules, a NaN guard, and train/eval mode. All subcommands share the `TAPE <subcmd> [args]` prefix. Tape state (parameter registry, optimizer moment accumulators) survives `TAPE CLEAR` — only accumulated gradients are wiped. Verified against core:4335-4513.

### 19.1 Lifecycle

| Command | Syntax | Description |
|---------|--------|-------------|
| `TAPE START` | `TAPE START` | Begin recording; subsequent array ops join the tape |
| `TAPE CLEAR` | `TAPE CLEAR` | Reset the tape (wipe forward pass + gradients); optimizer state preserved |
| `TAPE PARAM` | `TAPE PARAM <var>` | Register `<var>` as a trainable parameter (before `TAPE START`) |
| `TAPE PARAM_NO_DECAY` | `TAPE PARAM_NO_DECAY <var>` | As `TAPE PARAM` but skip weight decay (embeddings) |
| `TAPE BACKWARD` | `TAPE BACKWARD <loss_var>` | Reverse-mode autodiff from `<loss_var>` |

### 19.2 Optimizers

| Command | Syntax | Description |
|---------|--------|-------------|
| `TAPE CHUCK_STEP` | `TAPE CHUCK_STEP <lr> <loss_var>` | **Chuck — the ecosystem default.** Three levels: global loss-trend window (dampen/boost), per-parameter gradient-norm modulation with converged-param freeze, stagnation-escape noise. Alias `TAPE CHUCK`. Synced with the chuck.optimizer reference |
| `TAPE ADAMW_STEP` | `TAPE ADAMW_STEP <lr> [wd] [b1] [b2]` | Decoupled-weight-decay optimizer with bias-corrected momentum. Defaults wd=0.1, b1=0.9, b2=0.95. Alias `TAPE ADAMW` |
| `TAPE ADAM_STEP` | `TAPE ADAM_STEP <lr>` | Classic per-parameter diagonal baseline optimizer. Alias `TAPE ADAM` |

### 19.3 Clipping, accumulation, schedules, NaN guard

| Command | Syntax | Description |
|---------|--------|-------------|
| `TAPE CLIP_GRADS` | `TAPE CLIP_GRADS <max_norm>` | Global-norm clip; stores the norm in `grad_norm`. Alias `TAPE CLIP` |
| `TAPE ACCUM_GRADS` | `TAPE ACCUM_GRADS` | Save current grads into the accumulation buffer. Alias `TAPE ACCUM` |
| `TAPE APPLY_ACCUM` | `TAPE APPLY_ACCUM <N>` | Average accumulated grads by N, copy back |
| `TAPE LR_COSINE` / `LR_STEP` / `LR_LINEAR` | `... <base_lr> <warmup> ...` | LR schedule (cosine / step-decay / linear, each with warmup) |
| `TAPE LR_NEXT` | `TAPE LR_NEXT <var>` | Advance the schedule, store current lr in `<var>` |
| `TAPE NAN_CHECK` | `TAPE NAN_CHECK <var>` | Store 1 (clean) or 0 (NaN found — grads zeroed, loss scale halved) |
| `TAPE TRAIN_MODE` / `EVAL_MODE` | — | Enable / disable dropout |
| `TAPE SAVE` / `LOAD` | `TAPE SAVE "<path.bin>"` | Persist **model parameters** (magic `AMLE`, count, then each param's length + floats in registration order). Refuses a mismatched layout |

### 19.4 Example

```aml
W = matrix(4, 3, 0.1)
x = [1.0, 0.5, 0.2]
TAPE PARAM W
TAPE START
logits = matvec(W, x)
loss = cross_entropy(logits, 2)
TAPE BACKWARD loss
TAPE CLIP_GRADS 1.0
TAPE CHUCK_STEP 0.001 loss
TAPE CLEAR
```

`TAPE SAVE`/`LOAD` persist model parameters; the top-level `SAVE`/`LOAD` directives (§21) persist field state. The two are independent.

---

## 20. Async — SPAWN / AWAIT / CHANNEL

Parallel execution via pthreads (verified core:4517-4591, 5889-5922; header:822-848). Each `SPAWN` block runs in its own thread with an isolated local scope but shared global field state. Threads communicate through thread-safe bounded float queues (CHANNEL). Disableable at compile time with `#define AM_ASYNC_DISABLED`.

```aml
SPAWN <name>:
    <indented_block>
```
Launches the indented block in a background thread. Local variable changes do not affect the spawner; global field changes are not thread-safe without synchronization — use CHANNEL for cross-thread data.

| Command | Syntax | Description |
|---------|--------|-------------|
| `AWAIT` | `AWAIT <name> [name2 ...]` | Wait for the named thread(s) |
| `AWAIT` | `AWAIT` | Wait for all active spawned threads |
| `CHANNEL CREATE` | `CHANNEL CREATE <name> [capacity]` | Create a named channel (capacity default 64 = `AM_CHANNEL_BUF`) |
| `CHANNEL WRITE` | `CHANNEL WRITE <name> <value_expr>` | Write a float (blocks if full) |
| `CHANNEL READ` | `CHANNEL READ <name> <var>` | Read one float into `<var>` (blocks if empty) |
| `CHANNEL CLOSE` | `CHANNEL CLOSE <name>` | Deactivate a channel |

```aml
CHANNEL CREATE bus 16
SPAWN earth:
    forward(batch_earth)
    CHANNEL WRITE bus 1.0
AWAIT earth
CHANNEL READ bus v1
```

## 21. Field Persistence — LOAD / SAVE

`LOAD` and `SAVE` persist the whole `AM_State` to a binary `.soma` file (verified core:857-931, 3634-3658). Top-level directives, available in both the runner and (via lowering) in `amlc`-compiled binaries.

```aml
SAVE "state.soma"   # dump the field to disk
LOAD "state.soma"   # restore at next awakening (silent no-op if file missing)
```

Binary format: magic `0x4F534D41` (`AMSO`, 4 bytes) · version `2` (4 bytes) · `sizeof(AM_State)` (4 bytes, ABI guard) · timestamp (8 bytes) · then the full `AM_State` image. `am_field_load` refuses a file whose magic, version, or struct size does not match the running `libaml.a`, preventing silent cross-version corruption. Per-voice co-occurrence buffers live outside `AM_State` (token-id-space-specific) and are managed separately. This persists **field state**; `TAPE SAVE`/`LOAD` (§19) persists **model parameters** — independent mechanisms.

## 22. Execution Paths

AML programs run through three paths:

| Path | Entry | Use |
|------|-------|-----|
| **Interpreter** | `am_exec` / `am_exec_file` | General use; full Level 0/1/2; parse + execute in one pass |
| **amlc transpiler** | `amlc foo.aml` | Static compile of AML + BLOOD C to a standalone binary; top-level directives lower to `am_exec()` in an `__attribute__((constructor))` so physics apply before `main()` |
| **Bytecode** | `am_compile` / `am_exec_compiled` | Hot inference/training loops — parse once, run many times without re-parsing or string dispatch |

The bytecode form (core:6246-6291, header:28-30) pre-parses each line into an opcode with pre-split args; execution is a `switch` over opcodes (no `strcmp`/`sscanf` per line). Covered: the TAPE optimizer/grad ops and the hot sequence functions (`seq_embed`, `seq_matvec`, `seq_rmsnorm`, `multi_head_attention`, `seq_cross_entropy`, `add`, `mul`, `silu`); uncovered lines fall back to the interpreter via `BC_FALLBACK`. Use it when the same AML program runs every training step; `am_exec_file` is fine for one-shot field configuration.

```c
void* am_compile(const char* script);   // parse + compile to bytecode
int   am_exec_compiled(void* cs);       // execute (many times)
void  am_free_compiled(void* cs);       // release
```

---

*AML is part of the Arianna Method — an approach to AI identity, substrate-independence, and the refusal of forgetting.*

*github.com/ariannamethod*
