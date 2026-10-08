# Text arriving in AML

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

`read_line()` brings one line from standard input into ordinary AML text.
`codepoint_isalnum(cp)` supplies Unicode letter/number classification for
word-building modules. Both belong to the standalone two-file core.

```aml
PRINT "Haiku is listening"
line = read_line()
if list_len(line) == 1:
    text = list_get(line, 0)
    PRINT text
    if text_len(text) > 0:
        PRINT codepoint_isalnum(text_codepoint(text, 0))
```

## Line input

`read_line()` takes no arguments and returns a fresh string list:

| Input event | Result |
| --- | --- |
| `rain` followed by LF | `["rain"]` |
| LF alone | `[""]` |
| `rain` followed by CR LF | `["rain\r"]` |
| `rain` followed by EOF | `["rain"]` |
| EOF with no remaining bytes | `[]` |

The call removes only the final LF. It preserves CR, spaces, tabs, Unicode
spelling and an unterminated final line. Each line may contain at most
1,048,576 bytes after removing LF. Embedded NUL, malformed UTF-8, an exceeded
limit, an input error or an allocation failure stops execution with a diagnostic.
A failed assignment preserves its previous value. Bytes already consumed from
the input stream remain consumed.

The intrinsic flushes standard output before waiting for input, so a prompt
reaches a terminal or pipe reader before the next line is supplied. Native
threads lock the input stream for the complete read; concurrent callers receive
whole lines. Each caller owns its returned list and immutable string.

The C API takes an explicit stream:

```c
AM_List* am_read_line(FILE* input, char* error, size_t error_cap);
```

Success returns one owned list, released with `am_list_free`. Failure returns
`NULL`. The optional error buffer receives a NUL-terminated diagnostic.
`am_read_line` reads the given stream directly; the AML intrinsic supplies
`stdin` and handles the prompt flush.

## Unicode classification

`codepoint_isalnum(cp)` accepts one finite integer Unicode scalar, including
U+0000. It returns one for Unicode 15.0.0 Letter or Number categories and zero
for every other scalar. Surrogates, negative values, values above U+10FFFF,
fractional values, nonfinite values and other AML types raise an error.
The operation is independent of the process locale.

Letters across scripts, decimal digits, superscript numbers, fractions and
Roman numerals qualify. Combining marks, punctuation, whitespace and symbols
do not. A word-building module decides how to handle those boundaries and
whether to lowercase its text with [text_lower](TEXT_LOWER.md).

```c
int am_codepoint_isalnum(int codepoint); /* 1/0, or -1 for invalid scalar */
```

The embedded table contains 137,935 members in 747 ranges, occupying 5,976
bytes. Its source is Unicode 15 `UnicodeData.txt`, SHA-256
`806e9aed65037197f1ec85e12be6e8cd870fc5608b4de0fffd990f689f376a73`.
The Unicode data license is included beside the core tables. Regenerate with
Python 3.12 / Unicode 15 after obtaining the pinned file:

```sh
python3 tools/generate_alnum_tables.py /path/to/UnicodeData.txt > /tmp/alnum.inc
python3 tests/text_input_reference.py --write
```

## Verification

```sh
make test-text-input
```

The frozen reference comes independently from Python `str.isalnum()`. The C
gate checks all 1,114,112 codepoint positions against its result count and
64-bit digest, plus curated script, category and Unicode-15 additions. Direct
API tests cover blank/CR/final lines, repeated EOF, exact and exceeded byte
limits, malformed UTF-8, NUL, stream errors, whole-line concurrent reads and
refused allocations.

The same line values and property cases run through interpreted, stepped,
bytecode, runner and compiled scalar execution. Eleven malformed calls and
four invalid input streams stop through all five paths. A fork-and-pipe gate
checks that runner and compiled programs expose their prompt before the test
sends the requested input. Fresh startup stays silent on stderr when there
are no active pipes to close. Test execution needs C and shell; Python authors
the committed Unicode reference.
