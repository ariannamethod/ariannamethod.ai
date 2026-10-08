# Immutable tokenizers, ordinary pieces

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

AML can load an immutable tokenizer model and receive its ordered UTF-8 pieces
as a string list. Haiku uses this boundary to keep its original subword hearing
while constructing words, trigrams and experience in AML.

```aml
model = tokenizer_load("models/haiku_sp.model")
pieces = tokenizer_pieces(model, "rain carries a memory")
PRINT pieces
```

`tokenizer_load(path)` requires one string and returns a tokenizer value.
`tokenizer_pieces(model, text)` requires a tokenizer and a string; it returns
a fresh list of immutable strings. Both names are case-insensitive and reserved
against user function definitions. `PRINT model` emits `<tokenizer>`.

## Paths, output and ownership

Relative model paths resolve from the statement's original AML source file.
An imported module can load its neighboring data, and a compiled program keeps
that source origin when launched from another directory. The model file must
be present when loading. Once loaded, its complete immutable data belongs to
the tokenizer value.

An assignment, argument, function return, persistent global or worker snapshot
retains the same immutable model. Rebinding one variable releases that owner's
reference. The last release destroys the native model. Worker encoding may run
concurrently on one model; each call owns its output. String-list assignment
continues to copy the mutable output container.

Results retain piece order and spelling, including word-boundary markers,
normalization effects and empty pieces supplied by a backend. NoTorch supplies
the normalized surface of an unknown run, so unfamiliar input remains visible.
Empty text produces an empty list with the canonical backend. Model algorithms
and normalization come from the model file.

AML accepts at most 65,536 returned pieces and 1,048,576 total output bytes.
Each piece must be complete valid UTF-8 without NUL. The runtime gathers an
unpublished list, checks every emitted piece and the backend status, then
publishes the result. A failed load or encoding preserves the assignment's
previous target. Earlier statements and evaluated arguments keep their effects.

Tokenizer values have their own type. Scalar, numeric-array, text, list and map
operations reject a tokenizer used as their operand.

## Canonical NoTorch backend

```sh
make -C ../notorch lib BLAS_FLAGS= BLAS_LIBS= X86_SIMD=0 ARM_SIMD=0
make notorch
./runner/aml-notorch hearing.aml
make test-tokenizer
```

The optional bridge calls NoTorch's `nt_spm_load`, `nt_spm_encode` and
`nt_spm_result_free` APIs from `sentencepiece.h`. NoTorch reads SentencePiece
ModelProto data and executes deterministic Unigram inference in portable C.
It supports embedded compiled normalization rules, normalizer whitespace flags
and NORMAL, UNKNOWN, CONTROL, USER_DEFINED and UNUSED pieces. Unsupported model
algorithms and byte fallback produce explicit loader errors. There is no
SentencePiece or protobuf runtime library dependency.

`am_use_notorch()` registers tokenizer, sampling and numerical backends together.
The optional runner and compiler already call it. Install the bridge, AML and
NoTorch archives in one prefix for `amlc --scalar`; the link order remains
`libaml_notorch.a libaml.a libnotorch.a`. `am_use_notorch_sampling()` preserves
its sampling-only registration contract. The ordinary runner remains a
standalone host; a tokenizer load without a registered backend reports
`tokenizer backend unavailable`.

## Embedding API

```c
typedef struct AM_Tokenizer AM_Tokenizer;
typedef int (*AM_TokenizerEmit)(void* context, const char* utf8, size_t bytes);
typedef struct {
    void* (*load)(const char* path, char* error, size_t error_cap);
    void (*destroy)(void* model);
    int (*pieces)(const void* model, const char* text, size_t bytes,
                  AM_TokenizerEmit emit, void* context,
                  char* error, size_t error_cap);
} AM_TokenizerBackend;

void am_set_tokenizer_backend(const AM_TokenizerBackend* backend);
AM_Tokenizer* am_tokenizer_load(const char* path, char* error, size_t error_cap);
void am_tokenizer_ref(AM_Tokenizer* model);
void am_tokenizer_free(AM_Tokenizer* model);
AM_List* am_tokenizer_pieces(const AM_Tokenizer* model, const AM_String* text,
                           char* error, size_t error_cap);
int am_set_var_tokenizer(const char* name, AM_Tokenizer* model);
const AM_Tokenizer* am_get_var_tokenizer(const char* name);
```

Registration copies the table and survives `am_init`. All three callbacks are
required; `NULL` or an incomplete table disables future loads. Configure the
registry before executing programs or starting workers. Each loaded value
keeps its copied table, so replacement or unregistering changes future loads
and existing owners continue to use their original callbacks. Keep callback
code loaded until the last corresponding model owner is released.

`load` returns owned immutable data or `NULL`. `pieces` receives borrowed input
and a sink; it emits complete pieces in order, stops on a nonzero sink result,
then returns zero for success or minus one for failure. It must support
concurrent encoding on the same model and retain none of the input/sink
pointers. `destroy` releases the model exactly once at the final owner.

The direct C loader passes its caller's path literally. The AML intrinsic
performs source-relative resolution before calling it. Direct load/pieces
calls return an owned value or `NULL` with an optional diagnostic. The
persistent setter retains the supplied model; its caller keeps its own owner.
The getter borrows until persistent mutation/reset. Typed public structs grow
with tokenizer values, so rebuild C hosts and the runtime together.

## Verification

`make test-tokenizer` checks mocked callback-table copying, replacement and
unregistration while old models remain alive, function/return/assignment
ownership, persistent state, worker sharing, rejected output and allocation
failures. Every loaded mock model is destroyed exactly once. The canonical
NoTorch gate runs frozen piece vectors through interpreted, stepped, bytecode,
runner and scalar compiled execution, including source-relative model loading.
Its 97-byte identity-normalized Unigram fixture and expected equal-score order
come from SentencePiece 0.2.2, frozen by NoTorch's
`tests/reference_sentencepiece.py`. Twelve direct vectors include unknown
surfaces, Unicode and whitespace; four threads repeat 1,000 encodings on one
immutable model. Fifteen invalid calls and model files fail through all five
paths. Standalone numeric query statements use the same typed checks as their
assigned expression forms.
