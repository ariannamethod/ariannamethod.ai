# Numerical values — learners keep their own arrays

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

AML learner modules compose linear layers, tanh, explicit reverse derivatives,
mean squared error and SGD through canonical
[NoTorch](https://github.com/ariannamethod/notorch) numerical functions. Each call
returns a fresh AML array and preserves its inputs. Parameters, hidden states,
gradients and random streams remain ordinary AML values: separate learners can
advance independently, survive host calls and travel into worker snapshots.

## Build and registration

Build the scalar NoTorch archive and the optional AML binding:

```sh
make -C ../notorch lib BLAS_FLAGS= BLAS_LIBS= X86_SIMD=0 ARM_SIMD=0
make notorch
./runner/aml-notorch learner.aml
make test-numerical
```

`NOTORCH_ROOT`, `NOTORCH_INCLUDE`, `NOTORCH_LIB` and `NOTORCH_LDFLAGS` have the
same meanings as in [owned sampling](SAMPLING.md). NoTorch must provide the
`nt_linear_values`, `nt_linear_vjp_values`, `nt_tanh_values`,
`nt_tanh_vjp_values`, `nt_mse_grad_values`, `nt_sgd_values` and
`nt_rng_normal_values` APIs. The runner registers both numerical and sampling
backends with `am_use_notorch()`.
The required kernels are in [NoTorch PR #162](https://github.com/ariannamethod/notorch/pull/162),
published as `8c39e3e6d47e11bad30d3f64f45c1a863929a2a2`.

`make install-notorch PREFIX=/your/prefix` installs the optional runner and
binding with the ordinary toolchain. Install NoTorch in the same prefix.
`AML_PREFIX=/your/prefix amlc learner.aml --scalar` discovers the three archives,
registers both backends before directives or BLOOD `main`, and links in order:

```text
libaml_notorch.a  libaml.a  libnotorch.a
```

For manually compiled `amlc --emit-c` output, define `AML_LINK_NOTORCH`, link
those archives, then `-lm -lpthread` and any NoTorch acceleration libraries.
The previous `AML_LINK_NOTORCH_SAMPLING` macro remains accepted by newly emitted
C and now registers the complete optional backend. Existing C hosts can keep
calling `am_use_notorch_sampling()` when they want sampling registration alone.

`make` and `runner/aml` remain usable with the standalone two-file core.
Numerical calls without registration fail with `numerical backend unavailable;
use NoTorch-enabled AML`. A prefix missing the bridge or NoTorch has the same
explicit behavior.

## AML operations

Matrices use row-major flat storage. All arrays are nonempty; all input elements,
dimensions and learning rates must be finite. Dimensions are positive integers.
Input lengths must match the specified dimensions, and every returned array
must fit the 1,048,576-element AML array bound.

| Call | Result |
|------|--------|
| `nt_linear(w, b, x, rows, cols)` | `rows` values: `b[r] + sum(w[r*cols+c] * x[c])`. Required lengths are `rows*cols`, `rows`, `cols`. |
| `nt_linear_vjp(w, x, dy, rows, cols)` | Packed `[dW, db, dx]`, with lengths `rows*cols`, `rows`, `cols`. `dW[r,c]=dy[r]*x[c]`, `db=dy`, `dx[c]=sum(w[r,c]*dy[r])`. |
| `nt_tanh(x)` | Elementwise tanh. |
| `nt_tanh_vjp(y, dy)` | Elementwise `dy*(1-y*y)`, using saved tanh output `y` in `[-1,1]`; both arrays have equal length. |
| `nt_mse_grad(pred, target)` | Packed `[mean_loss, dpred]`, where loss is the mean squared residual and `dpred=2*(pred-target)/n`. Inputs have equal length. |
| `nt_sgd(params, grad, lr)` | `params-lr*grad`; equal input lengths and finite `lr>=0`. Zero rate returns a fresh copy. |
| `rng_normal(stream, n)` | `n` standard normal values from the owned stream; integer `n` from 1 through 1,048,576. |

`nt_tanh`, `nt_tanh_vjp` and `nt_sgd` retain their first input's matrix shape.
Linear, packed derivative/loss and normal outputs are one-dimensional arrays.
An elementwise partner needs the same element count; its shape metadata does
not change the primary input's result shape.

The standalone scalar intrinsic `isfinite(value)` returns `1` for a finite AML
number and `0` for NaN or either infinity. Wrong type or arity raises an error.
It lets learner modules validate state before applying a numerical operation.

```aml
w = [0.2, -0.3, 0.4, 0.1]
b = [0.05, -0.1]
x = [0.7, -0.2]
target = [0.3, -0.5]
y = nt_tanh(nt_linear(w, b, x, 2, 2))
loss = nt_mse_grad(y, target)
dy = zeros(2)
dy[0] = loss[1]
dy[1] = loss[2]
dz = nt_tanh_vjp(y, dy)
back = nt_linear_vjp(w, x, dz, 2, 2)
dw = zeros(4)
i = 0
while i < 4:
    dw[i] = back[i]
    i = i + 1
db = zeros(2)
db[0] = back[4]
db[1] = back[5]
w = nt_sgd(w, dw, 0.125)
b = nt_sgd(b, db, 0.125)
PRINT loss[0]
```

## Normal stream and publication

`rng_normal` shares the version-1 map and PCG32 stream described in
[sampling](SAMPLING.md). Each output consumes exactly two unsigned words:

```text
u1 = (word1 + 1) / 4294967297
u2 = word2 / 4294967296
normal = float(sqrt(-2 * log(u1)) * cos(2 * pi * u2))
```

There is no cached spare. Splitting a request into consecutive batches preserves
the sequence and final map exactly. Integer state is portable; normal values
also use the platform's double-precision `log`, `sqrt` and `cos`, followed by
binary32 rounding.

The runtime validates arguments, allocates the complete result, calls NoTorch
with a local state, checks every output for finiteness, then publishes the stream
limbs. Invalid arguments, refused allocation, rejected backend calls and
nonfinite backend results cannot publish a partial normal stream. A failed
assignment leaves the previous target value intact. Argument expressions obey
ordinary AML evaluation and persistent-save rules.

Numerical calls do not append to, clear, or otherwise change AML's or NoTorch's
legacy autograd tape. Reverse derivatives are explicit array values. NoTorch's
existing global initializer stream and C's `rand` also remain separate from the
owned normal stream.

## C host contract

`am_set_numerical_backend(const AM_NumericalBackend*)` copies the complete
callback table. It survives `am_init` and worker startup. Register before
executing programs or launching workers; callback code stays loaded until
unregistered. Passing `NULL`, or a table with a missing callback, disables the
numerical backend.

Callbacks receive borrowed, read-only input buffers and a separate caller-owned
output buffer. They must not retain these pointers. Every callback returns `0`
on success and `-1` on rejection. AML validates types, bounds and complete finite
outputs before exposing the result. `am_use_notorch()` in the optional bridge
registers the canonical sampling and numerical tables together.

A persistent host can copy `am_get_var_array` values and clone its RNG with
`am_map_clone`, then restore them using `am_set_var_array` and `am_set_var_map`.
Those setters copy values. `SPAWN` snapshots learner arrays and stream maps at
launch; worker updates remain owned by that worker. Field-state `SAVE` continues
to serialize field state; a host chooses storage for learner arrays and maps.

## Verification

`make test-numerical` checks callback-table lifetime, argument validation,
fresh output ownership, failed backend publication, and both output allocation
sites. An independent Python scalar oracle authors the committed fixture:

```sh
python3 tests/numerical_reference.py > tests/numerical_vectors.aml
```

The fixture exercises unequal matrix shapes, analytic derivatives, tanh's
central and saturated regions, MSE, SGD, twelve consecutive learning updates,
and PCG/normal batches with exact stream-state checks. It runs through
interpreter, stepped host, bytecode, optional runner and compiled C executable.
The test command itself uses C, shell and the two project libraries.

Runtime gates also cover errors and overflow before publication, host
save/restore, matrix dimension overflow before allocation, exact-cap matrix
shapes, learner workers, and live legacy tapes whose backward results survive
interleaved numerical calls. Compiler gates exercise BLOOD-only registration,
both manual macros, incomplete installation prefixes, and failed AML calls
stopping before continuation or C `main`.
