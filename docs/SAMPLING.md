# Owned sampling — AML and canonical NoTorch

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

Haiku carries its choice of words in an owned, resumable random stream. AML
provides typed values and execution; [NoTorch](https://github.com/ariannamethod/notorch)
provides PCG32 integer arithmetic, bounded selection and temperature-weighted
categorical selection. The optional `libaml_notorch.a` binding connects them.
The required canonical APIs are introduced in
[NoTorch PR #161](https://github.com/ariannamethod/notorch/pull/161), published
as `ad7b53afa6b24fb40e22624d5967d7b80eb838bc`.

## Build and run

From sibling checkouts, build the scalar canonical archive first:

```sh
make -C ../notorch lib BLAS_FLAGS= BLAS_LIBS= X86_SIMD=0 ARM_SIMD=0
make notorch
./runner/aml-notorch choices.aml
make test-sampling
```

`NOTORCH_ROOT` defaults to `../notorch`. Override `NOTORCH_INCLUDE` for the
directory containing `notorch.h`, `NOTORCH_LIB` for the archive, and
`NOTORCH_LDFLAGS` for any libraries required by that archive. The default scalar
build links the C runtime, libm and pthreads. An archive built with BLAS still
needs its BLAS link flags.

`make install-notorch PREFIX=/your/prefix` installs the ordinary AML toolchain,
the additional `aml-notorch` runner and `libaml_notorch.a`. Install NoTorch into
the same prefix independently. `AML_PREFIX=/your/prefix amlc choices.aml --scalar`
discovers all three archives, registers the backend before AML execution or
BLOOD `main`, and links in this order:

```text
libaml_notorch.a  libaml.a  libnotorch.a
```

`make` and `runner/aml` retain the standalone core. Calling a sampling intrinsic
without a registered backend raises `sampling backend unavailable; use
NoTorch-enabled AML`. An installation missing either the bridge or NoTorch
keeps that explicit error. `--no-accel` keeps the existing standalone C path.

For a manually compiled `amlc --emit-c` result, pass
`-DAML_LINK_NOTORCH_SAMPLING` and those three archives, followed by `-lm -lpthread`
and any required NoTorch acceleration libraries.

## AML contract

```aml
stream = rng_new(42)
snapshot = stream
u = rng_uniform(stream)
word = rng_index(stream, 100)
weights = [1, 3, 0]
choice = rng_categorical(stream, weights, 0.5)
replayed = categorical_at(weights, 0.5, 0.25)
```

| Intrinsic | Result and validation |
|-----------|-----------------------|
| `rng_new(seed)` | New stream map. Seed is a finite integer from 0 through 16,777,215. |
| `rng_uniform(stream)` | Exactly representable float in `[0, 1)`: the next unsigned word's top 24 bits divided by 2²⁴. Consumes one word. |
| `rng_index(stream, n)` | Integer in `[0, n)`. `n` is a finite integer from 1 through 16,777,216. Uses unbiased rejection; even `n = 1` consumes one word. |
| `rng_categorical(stream, weights, temperature)` | Selected array index. Consumes one full 32-bit word after validating weights and temperature. |
| `categorical_at(weights, temperature, draw)` | The same categorical selection with an explicit finite draw in `[0, 1)`. Does not consume a stream. |

Weights are a nonempty numeric array, finite and nonnegative, with at least one
positive entry. Temperature is finite and strictly positive. Selection uses
probabilities proportional to `weight^(1 / temperature)`. NoTorch evaluates
log-relative weights in double precision, keeping extreme finite weights and
small positive temperatures usable without overflowing a raw power. Zero
weights are skipped. A draw exactly on a cumulative boundary chooses the next
positive interval; rounding at the right edge falls back to the last positive
weight. Inputs are not modified.

`rng_categorical` uses all 32 random bits for its internal double draw.
`categorical_at` accepts AML's float scalar, making scripted draw fixtures
explicit without conflating Python's historical random generators with PCG32.
Integer streams are portable; selections extremely close to a categorical
boundary also depend on the platform's double-precision `log` and `exp`.

Invalid arguments, malformed stream state and failed backend calls leave the
stream unchanged. Argument expressions execute before the call; effects
performed by those expressions follow ordinary AML evaluation rules.

## Ownership and state format

A stream is an ordinary numeric map with exactly five keys:

| Key | Value |
|-----|-------|
| `algorithm` | `1`: PCG32 XSH-RR, fixed stream 54 (increment 109) |
| `state0` | Bits 0–15 of the unsigned 64-bit state |
| `state1` | Bits 16–31 |
| `state2` | Bits 32–47 |
| `state3` | Bits 48–63 |

Every limb is a finite integer from 0 through 65,535. Four limbs preserve the
entire state through AML's float scalars. Seeding follows PCG's two-step
initialization: advance zero state once, add the seed, advance once more.
The seed-42 stream begins with unsigned words
`a15c02b7 7b47f409 ba1d3330 83d2f293 bfa4784b cbed606e`.

Map assignment copies streams, including parenthesized expressions and function
returns. Function parameters share the caller's stream, so draws advance it.
`SPAWN` receives a launch-time copy; parent and child then draw independently.
Persistent globals preserve a stream between `am_exec` calls. A C host can
clone `am_get_var_map("stream")` and later restore it with `am_set_var_map` to
resume from that exact position. Disk storage is the host's responsibility;
the field-state `SAVE` directive does not serialize AML global maps.

These streams are independent of `randn`, C's `rand`, NoTorch's existing
`nt_seed` stream, tensor initialization and Chuck's optimizer noise.

## C host registration

```c
#include "ariannamethod.h"

int main(void) {
    am_use_notorch_sampling();
    return am_exec("stream = rng_new(42)\nPRINT rng_index(stream, 10)\n");
}
```

`am_use_notorch_sampling()` lives in the optional binding archive. Core
`am_set_sampling_backend(const AM_SamplingBackend *)` copies a table of seed,
unsigned-word, uniform, bounded-index, explicit categorical and owned
categorical callbacks. Register before executing programs or launching workers;
configuration survives `am_init`. Passing `NULL` clears registration. Stream
state stays in caller-owned maps; registration carries no mutable random state.

## Verification

`make test-sampling` exercises:

- Backend callback validation and allocation-failure cleanup of stream creation.
- An independent Python integer oracle committed as
  [sampling_vectors.aml](../tests/sampling_vectors.aml), including PCG's published
  seed-42 vector, boundary seeds, mixed bounds and the two rejected zero outputs
  from raw state zero with bound three. Regenerate with
  `python3 tests/sampling_reference.py > tests/sampling_vectors.aml`.
- The same vectors and ownership assertions through interpreter, stepped
  execution, bytecode, the optional runner and an `amlc --scalar` binary.
- Invalid argument/state preservation, host save/resume, interleaved unrelated
  random streams and workers launched from different stream positions.
- Backend registration in BLOOD-only programs, explicit failures in standalone
  or incomplete installations, runtime errors before C `main`, and compiler
  source-budget acceptance/rejection.

The canonical implementation and numerical tests live in NoTorch. Algorithm
reference: [PCG minimal C usage](https://www.pcg-random.org/using-pcg-c-basic.html).
