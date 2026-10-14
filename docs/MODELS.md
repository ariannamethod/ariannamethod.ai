# Immutable bodies, explicit outcomes

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

AML owns an immutable inference model as an ordinary typed value. The optional
NoTorch bridge supplies PEN7 loading, canonical BPE, forward arithmetic and
parameter mixtures. Vocabulary policy, experience, learning and publication
remain in the organism's AML modules.

```aml
body = model_try_load("weights/haiku1.gguf")
if model_ready(body):
    ids = model_encode(body, "rain remembers", 32)
    result = model_forward(body, ids)
    if record_get(result, "ok"):
        logits = record_get(result, "logits")
        hidden = record_get(result, "hidden")
```

## Values and outcomes

| Intrinsic | Result |
| --- | --- |
| `model_try_load(path)` | Immutable owned model attempt, including an unavailable result |
| `model_ready(model)` | 1 when loaded and validated, otherwise 0 |
| `model_error(model)` | Immutable load diagnostic; empty on success |
| `model_identity(model)` | SHA-256 of the exact loaded snapshot; empty when unavailable |
| `model_tokenizer_identity(model)` | SHA-256 of canonical tokenizer data; empty when unavailable |
| `model_info(model)` | Fresh flat record of readiness, geometry, identities and error |
| `model_encode(model, text, max_tokens)` | Fresh numeric vector of integral token IDs |
| `model_piece(model, id)` | Fresh UTF-8 surface, or empty for NUL/partial UTF-8 bytes |
| `model_forward(model, ids)` | Fresh `{ok, error, logits, hidden}` on success |
| `model_mix_forward(left, right, alpha, ids)` | Same outcome, with parameter mixture `alpha*left + (1-alpha)*right` |

A failed forward returns a record containing `ok = 0` and nonempty `error`,
with neither `logits` nor `hidden`. Missing bodies, incompatible geometry or
BPE, invalid numeric IDs/alpha, and failed/nonfinite backend output become
these explicit outcomes. AML checks every result before constructing its
published record. The detached outcome is published as one value; caller-owned
inputs stay unchanged.
A program can choose either available body or its own quiet fallback.

A nonempty load path resolves from its original AML source statement, including
imported functions, workers and compiled executables. Empty text means an
unconfigured body: it returns an unavailable owner without calling the backend.
A missing/corrupt file and an absent backend likewise return unavailable owners
without printing diagnostics. Load readiness and diagnostics never change.
Wrong argument types, invalid arity, encoding/piece errors and AML allocation
failure stop the statement and preserve its previous destination. Load outcome
text is available for the application's own reporting policy.

`model_info` contains `ready`, `vocab`, `width`, `context`, `layers`, `ffn`,
`heads`, `identity`, `tokenizer_identity`, and `error`. Unavailable attempts have
zero geometry and empty identities. Ready models have positive bounded geometry
and two validated lowercase 64-hex SHA-256 identities. Dimensions and token IDs
fit exactly in AML's float32 scalar domain. `PRINT model` emits `<model>`.
Names are case-insensitive intrinsics reserved against user definitions.

Assignments, function arguments and returns, persistent globals, worker
snapshots and `AWAIT ... INTO` retain the same immutable model. References are
atomic; the final release destroys native data exactly once. Rebinding any
one owner leaves the others usable. Registration copies a separate
`AM_ModelBackend` table; every owner retains its original callbacks after
replacement or unregistration. Configure registries before starting execution
or workers and keep callback code loaded for the corresponding owner lifetime.

Scalar, array, text, list and map operators reject a model operand. Flat records
and checkpoints reject model handles. The organism can save identity strings
and its private experience, reopen its sources, and verify their identities
before publishing restored state.

## Canonical PEN7 behavior

NoTorch's `pen7.h` is part of the ordinary `libnotorch.a` archive. Its loader
accepts the declared F32/Q8_0 PEN7 GGUF schema and holds a private snapshot of
the bytes it hashes and parses. Replacing or deleting a source file therefore
cannot change a loaded model. AML exposes no writable parameter or training
operation on that owner.

Encoding uses canonical `byte-rank-pass-v1` BPE with ASCII lowercase, no BOS/EOS
and no pre-tokenizer. Input is bounded at 8,191 bytes. AML requires nonempty
valid UTF-8 text and an integer capacity in `1..context`. Exceeding either bound
fails without truncation or partial token IDs. The caller chooses its context
window. `model_piece` preserves valid UTF-8 byte surfaces exactly; a byte piece
containing NUL or an incomplete UTF-8 sequence has an empty AML text surface.
This lets the organism select complete words in its own language.

Every forward starts at learned position zero with exactly the supplied vector
of IDs; vector length is `1..context`, IDs are integral in `0..vocab-1`.
`logits` has `vocab` entries. `hidden` has `width` entries and is the final
last-token RMS-normalized contextual representation consumed by the output
head. Outputs must all be finite. Each bridge call allocates and releases its
own bounded NoTorch workspace. Concurrent owners share model data and carry
independent sequence workspaces.

Mixtures require matching geometry, tokenizer identity and backend family.
Every parameter participates in `alpha*left + (1-alpha)*right` before the
forward pass. Alpha is finite in `[0,1]`; endpoints use the exact source body.
The two immutable sources keep their identities and weights. Repeated calls
can use different alpha values without a permanent mixed model.

## Embedding and building

```sh
make -C ../notorch lib BLAS_FLAGS= BLAS_LIBS= X86_SIMD=0 ARM_SIMD=0
make notorch
./runner/aml-notorch bodies.aml
make test-model
```

Use a NoTorch revision containing the immutable `pen7.h` API and rebuild C hosts
against the current AML header. `am_use_notorch()` registers models together
with sampling, numerical, parliament and tokenizer tables. The original
numerical table and `am_use_notorch_sampling()` contract stay unchanged. The
link order remains `libaml_notorch.a libaml.a libnotorch.a`, with libc/libm and
pthreads; no separate harness archive is required. The standalone `libaml.a`
contains the value runtime and supports a host's custom model backend.

The public header defines `AM_ModelInfo`, `AM_ModelBackend` and these functions:

```c
void am_set_model_backend(const AM_ModelBackend* backend);
AM_Model* am_model_try_load(const char* path, char* error, size_t error_cap);
void am_model_ref(AM_Model* model);
void am_model_free(AM_Model* model);
int am_model_ready(const AM_Model* model);
const char* am_model_error(const AM_Model* model);
const AM_ModelInfo* am_model_get_info(const AM_Model* model);
AM_Record* am_model_info(const AM_Model* model);
AM_Array* am_model_encode(const AM_Model* model, const AM_String* text,
                          int max_tokens, char* error, size_t error_cap);
AM_String* am_model_piece(const AM_Model* model, int id, char* error, size_t cap);
AM_Record* am_model_forward(const AM_Model* left, const AM_Model* right,
                            float alpha, const AM_Array* ids);
int am_set_var_model(const char* name, AM_Model* model);
const AM_Model* am_get_var_model(const char* name);
```

The C loader uses its path literally and returns an owned attempt or `NULL` on
AML owner allocation failure. Its optional error buffer receives the load
outcome. `am_model_error` and `am_model_get_info` borrow immutable data until the
owner's final release. Info, encode, piece and forward return fresh owned values.
`am_model_forward(..., NULL, ...)` performs a single-source pass; supplied alpha
still has the finite `[0,1]` domain. `am_set_var_model` retains the caller's
owner; the getter borrows until persistent mutation/reset.

Backend load/destroy/info/encode/forward callbacks are required for future
loads; piece is optional and an absent callback is an explicit piece error.
The backend's load returns private immutable data; info fills the fixed
metadata struct, encode writes into a bounded integer staging buffer, and
forward writes into private staging vectors. Callbacks report zero for success
and nonzero for failure. Diagnostic text is bounded and copied. Both encode and
forward must support concurrent calls with borrowed inputs; they retain none
of those input/output pointers. The piece callback borrows exact native bytes
until model destruction.

## Verification

`make test-model` uses NoTorch's deterministic tiny F32 fixture writer
(`test_pen7 --fixtures DIR`), with `MODEL_FIXTURE_DIR` available for an existing
fixture directory. Its API gate exercises quiet loads, invalid metadata,
registry removal, typed ownership, record rejection, failed backend output and
allocation failures. Repeated worker returns transfer both models and forward
records through interpreter, stepping and bytecode. The canonical fixture runs
through those three paths, the runner and scalar compiled execution from a
different working directory, including sixteen-argument inherited helpers.
Four native threads compare bridge output exactly against direct NoTorch IDs,
logits and hidden vectors on their own workspaces. Fourteen invalid expressions
stop all five paths before later AML or C-main effects. A standalone runner and
an incomplete compiled prefix preserve explicit quiet unavailable outcomes.

`MODEL_REAL_PATH=/path/to/haiku1.gguf make test-model` adds the same direct native
parity gate for an actual body. No model weights are checked into this repository.
