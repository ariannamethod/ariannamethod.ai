# Unicode lowercase in AML

`text_lower(s)` returns a fresh immutable UTF-8 value using the Unicode 15.0.0
default lowercase operation. Haiku uses it in the RAE feature path while
MathBrain keeps the original case.

```aml
source = "İ ΑΣ AΣ'A"
lowered = text_lower(source)
PRINT lowered
PRINT source
```

Output:

```text
i̇ ας aσ'a
İ ΑΣ AΣ'A
```

The operation accepts one string. Other types and arities fail explicitly.
Its name is case-insensitive and reserved against user definitions. Nested
calls, parameters, returns, collection values, persistent globals, bytecode,
resumable execution, and scalar compiled programs share the same operation.

## Mapping and context

Simple mappings cover 1,432 Unicode scalars. U+0130, capital I with dot, expands
to U+0069 followed by U+0307. This changes both the codepoint count and UTF-8
byte count. A simple mapping can also grow in bytes: U+023A becomes U+2C65.
Kelvin sign U+212A becomes ASCII `k`.

Capital sigma U+03A3 uses the original input on both sides. The immediately
preceding non-Case_Ignorable scalar must be Cased, and the following
non-Case_Ignorable scalar must be absent or uncased, for the output to be final
sigma U+03C2. Other positions produce U+03C3. Exact Unicode properties control
this context: a scalar such as U+0345 belongs to both Cased and Case_Ignorable,
and the ignorable property takes precedence while scanning.

The mapping is independent of the process locale. It applies Unicode's
language-neutral lowercase rules directly to the input's codepoints.

## Ownership and size

The C API is:

```c
AM_String* am_string_lower(const AM_String* text);
```

A successful call returns one new owner, including empty or already lowercase
text. Source owners and their bytes remain unchanged. NULL reports invalid
input, an output exceeding `AM_MAX_STRING_BYTES`, or allocation failure. AML
propagates failure before continuing the program.

The first pass validates the source, counts output bytes and codepoints, and
enforces the 1 MiB output bound. The second pass writes the result. The two
allocations are the usual `AM_String` owner and its UTF-8 buffer. Final sigma
uses a running preceding context and forward lookahead; each ignorable run is
visited by at most one sigma lookahead. Work is linear in the input length.
All Unicode tables are immutable and shared.

## Data and regeneration

Tables are embedded in `core/ariannamethod.c`, alongside the Unicode data
license. The core remains two source files. The tables contain 181 simple
mapping ranges, 157 Cased intervals, and 437 Case_Ignorable intervals: 7,648
bytes of static data. Stride-two ranges compact alternating upper/lower pairs.

The generator reads these exact Unicode 15.0.0 files:

| File under `https://www.unicode.org/Public/15.0.0/ucd/` | SHA-256 |
| --- | --- |
| `UnicodeData.txt` | `806e9aed65037197f1ec85e12be6e8cd870fc5608b4de0fffd990f689f376a73` |
| `SpecialCasing.txt` | `78b29c64b5840d25c11a9f31b665ee551b8a499eca6c70d770fcad7dd710f494` |
| `DerivedCoreProperties.txt` | `d367290bc0867e6b484c68370530bdd1a08b6b32404601b8c7accaf83e05628d` |

After downloading them into a directory:

```sh
python3 tools/generate_text_lower_tables.py --ucd-dir /path/to/ucd-15.0.0 \
    --verify-python > /tmp/text_lower_tables.inc
```

The generator validates the input hashes. `--verify-python` compares the full
lowercase mapping and both properties for every codepoint from U+0000 through
U+10FFFF with CPython's Unicode 15.0.0 data. The property check uses the
development interpreter's `_PyUnicode_IsCased` and
`_PyUnicode_IsCaseIgnorable` exports. The runtime has no Python dependency.

The context algorithm was checked against CPython 3.12.14
[`handle_capital_sigma` and `lower_ucs4`](https://github.com/python/cpython/blob/v3.12.14/Objects/unicodeobject.c).
Unicode's [SpecialCasing data](https://www.unicode.org/Public/15.0.0/ucd/SpecialCasing.txt)
defines the expansion and conditional mapping; its
[DerivedCoreProperties data](https://www.unicode.org/Public/15.0.0/ucd/DerivedCoreProperties.txt)
defines the two context properties.

## Verification

```sh
make test-text-lower
```

The frozen fixtures come directly from Python 3.12.14 `str.lower()` with
Unicode 15.0.0. They cover 62 language, expansion, sigma, punctuation,
combining-mark, supplementary-plane, and uncased examples. API tests also
exercise fresh ownership, aliases, idempotence, both forms of UTF-8 growth,
exact-cap success, cap-plus-one failure, shrinkage, and allocation failures.
Runtime tests compare exact output through interpreter, resumable, bytecode,
runner, and `amlc --scalar` paths. Ten invalid programs fail before continued
execution or C main.

The direct Python oracle covers every AML-valid scalar in five contexts:
standalone; before sigma; between `A` and sigma; after `AΣ`; and between
`AΣ` and `A`. Each record ends with LF to delimit its context. This is
5,560,315 scalar-context cases. It also checks the 62 frozen examples, four
long ignorable-run inputs, and 200 deterministic random context strings.
Every resulting byte and codepoint count is compared to `str.lower()`.

To repeat that oracle with Python 3.12 and Unicode 15.0.0:

```sh
cc -std=c11 -O2 -fPIC -shared -Icore core/ariannamethod.c \
    -lm -lpthread -o /tmp/libaml-lower.so
python3 tools/verify_text_lower.py --library /tmp/libaml-lower.so
```

The `.so` command is the Linux invocation. Pass the platform's shared-library
output path to `--library` on other hosts. Add
`--write-fixtures tests/fixtures/text_lower_fixture.h` to regenerate the frozen
C examples from that Python interpreter.

Verified on Linux x86_64: 783 API checks, 1,203 allocation-wrapped checks with
zero tracked live blocks at exit, 220 runtime checks, all five output/error
paths, and the complete direct Python oracle. ASan/UBSan pass the API suite.
The five exhaustive output-stream SHA-256 values,
in the context order above, are:

```text
0fd52b670bc397db29b61a6c7472d017db8c761db2e65691a2fae559d55f1e00
42e0da3905312d2130158088ab6d9e0c60b904e662efb8beeafaece68964508b
23c60741579d6f75d204cf583a593331c74547d7b1156830d808a4b115bc7a41
7ecf13e2dafcd4317d36ed809461c4e2e368e90194555098fac9b04c73075859
cb0643501214fdc057baf28894721faf20d6ee3fd7471a619912c5ad649ab766
```
