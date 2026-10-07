# Text in AML

Text is an immutable, validated UTF-8 value. Variables, function parameters,
and returns carry scalars, arrays, or strings without converting between them.
The first consumer is Haiku's word and line boundary module.

```aml
def frame(name):
    return text_concat("[", text_concat(name, "]"))

owl = "сова 🦉"
copy = owl
owl = frame(owl)
PRINT owl
PRINT copy
PRINT text_len(copy)
```

This prints `[сова 🦉]`, `сова 🦉`, and `6`, each on its own line.
Rebinding a variable preserves the value held by its aliases.

## Literals and output

Both single and double quotes are accepted. Supported escapes are `\\`, `\"`,
`\'`, `\n`, `\r`, and `\t`. UTF-8 characters may appear directly in source.
Unknown escapes, unterminated literals, invalid UTF-8, and embedded NUL are
outside the text contract and produce errors. `#` starts a trailing expression
comment outside quotes; inside a string it is ordinary content.

`PRINT expression` evaluates and prints one value followed by LF:

- strings: their UTF-8 bytes, preserving any internal line breaks;
- scalars: nine significant decimal digits;
- arrays: comma-separated scalars inside brackets.

`ECHO` retains its existing literal output and `[AML]` prefix.
Use `text_concat` and `text_equal` for string concatenation and equality.
Arithmetic, comparisons, and conditions use scalar expressions.

## Expression intrinsics

Names are case-insensitive and reserved against user definitions. These nine
intrinsics do not consume slots in the user/builtin function table.

| Expression | Result |
| --- | --- |
| `text_len(s)` | Unicode codepoint count |
| `text_bytes(s)` | UTF-8 byte count |
| `text_equal(a, b)` | One for exact byte equality, otherwise zero |
| `text_find(s, needle)` | First matching codepoint index, or -1; empty needle returns zero |
| `text_slice(s, start, end)` | Text between codepoint indices; end is exclusive |
| `text_concat(a, b)` | Concatenation |
| `text_codepoint(s, index)` | Unicode scalar value at a codepoint index |
| `text_from_codepoint(cp)` | One-codepoint string |
| `text_lower(s)` | Unicode 15 default lowercase, including expansions and contextual sigma |

Negative indices count from the end. Slice bounds clamp to `[0, text_len(s)]`;
an end before the start gives empty text. `text_codepoint` fails for an
out-of-range index. Indices and codepoints must be finite integer-valued AML
scalars in the signed 32-bit range. Codepoint construction accepts
`1..0x10FFFF` excluding surrogates. NUL cannot be represented.

Codepoint count differs from grapheme count: a base letter and combining mark
count separately, as do the parts of an emoji sequence. No normalization,
case folding, whitespace classification, or language-specific tokenization
happens in these primitives. Haiku expresses its whitespace table in AML.
`text_lower` applies default lowercase without normalization or locale tailoring.
[TEXT_LOWER.md](TEXT_LOWER.md) records its context rules, table
provenance, output expansion, and verification.

## Ownership and limits

Each string owner holds one atomic reference. Literals and producing intrinsics
allocate owned values; variable reads and parameter binding retain references.
Function returns transfer an owned value to the caller. Reassignment, completed
calls, discarded expressions, closed programs, and cleared persistent globals
release references. Arrays also pass through typed parameters and returns;
the existing bare array assignment continues to make a copy.

User functions require exactly their declared number of arguments. Nested calls
parse commas, quotes, and brackets within string arguments correctly.

| Resource | Bound |
| --- | --- |
| UTF-8 bytes per string | 1 MiB (`AM_MAX_STRING_BYTES`) |
| Executable source line after indentation | 255 bytes |
| Function parameters | 8 |
| Function nesting | 16 |
| Iterations per loop | 10,000 |

Build larger strings with concatenation or supply them through the host API.
The source-line budget also applies to literals. A loop whose condition remains
true after its 10,000th iteration fails explicitly, including a text-scanning
loop. Allocation failures and exceeded string limits propagate as runtime errors.

## C API

`AM_String` exposes `data`, `byte_len`, `len`, and `refcount`. Callers treat
`data` as immutable. `am_string_new`, `am_string_concat`, `am_string_slice`,
`am_string_from_codepoint`, and `am_string_lower` return an owned reference or NULL on failure.
`am_string_ref` retains it; `am_string_free` releases it. The query functions
`am_string_find` and `am_string_codepoint` return codepoint indices/values.
NULL release is valid.

Rebuild C hosts against the updated header and library: the variable and
execution-context structures now include string fields.

After `am_init()`, `am_set_var_text(name, utf8)` validates the identifier and
text, copies the bytes into persistent globals, and enables persistent mode.
The persistent table belongs to the calling thread. It returns zero on success
and nonzero on failure. `am_get_var_text(name)`
returns borrowed UTF-8 data, or NULL for a missing/nontext variable. Copy that
data before mutating/resetting persistent state. `am_persistent_clear()` and
`am_persistent_mode(0)` release retained strings along with arrays.

`SPAWN` takes a launch-time snapshot of AML globals: mutable arrays are copied,
immutable strings retain atomic references, and scalars are copied. The C
`am_spawn_launch` entry point snapshots its caller's persistent table. Workers
own their persistent stores and release them at exit; later parent rebindings
and worker assignments do not overwrite each other. Field state and channels
retain their shared behavior. Function-local variables and definitions are not
closures; a worker can define functions or import its own modules.

Interpreter, resumable, bytecode, and `amlc --scalar` programs use the same
value and ownership rules. Text-valued channels and collections are subsequent
language work; the current channels carry floats.

## Verification

```sh
make test-text
```

The suite exercises UTF-8 rejection, codepoint boundaries, negative slicing,
literal parsing, nested mixed values, refcount ownership, allocation failures,
persistent host values, execution paths, and exact compiled output.

Verified on Linux x86_64: 483 low-level API checks, 515 allocation-fault checks,
and 1122 runtime checks. Forty-one invalid programs fail across interpreter,
resumable, bytecode, runner, and compiled scalar paths. Snapshot barriers and
12 workers performing 120,000 alias rebindings exercise concurrent ownership.
ASan/UBSan pass the API and runtime suites; LeakSanitizer is unavailable under
the execution environment's ptrace restriction. Allocation-fault tests account
for every allocation they track and finish with zero live blocks.
