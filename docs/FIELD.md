# The field organ in AML

A field is a dense numeric array in which words leave signed traces. The organ
reproduces WOLFE's lexical field (`project`, `normalize` and `dot` in
`wolfe.c`): each word excites three units chosen by a 64-bit hash. That hash
needs exact unsigned 64-bit arithmetic, which an AML float32 scalar with its
24-bit significand cannot carry, so it lives inside the organ. LALO's
`aml/wolfe.aml` builds its whole selection field on these calls.

```aml
read = field_project(zeros(8), "read", 1)
journal = field_project(zeros(8), "journal", 1)
context = field_project(read, "journal", 0.25)
PRINT read
PRINT context
PRINT field_dot(field_normalize(context), field_normalize(journal))
PRINT 1 / (1 + exp(0 - 8 * (0.61 - 0.53)))
PRINT 1 + log((47 + 1) / (3 + 1))
```

Output:

```text
[0, 0, 0, 0, 1, -1, 0, 1]
[0.25, 0, 0.25, -0.25, 1, -1, 0, 1]
0.242535636
0.654753506
3.48490667
```

## Intrinsics

Names are case-insensitive and reserved against user definitions. They occupy
no function-table slots and require exactly the shown arguments.

| Expression | Result |
| --- | --- |
| `field_project(vec, word, scale)` | New array: `vec` with `word`'s three signed units moved by `scale` |
| `field_normalize(vec)` | New array: `vec` divided by its Euclidean norm, unchanged when the norm is at most 1e-12 |
| `field_dot(a, b)` | Scalar inner product of two arrays of equal length |

Fields are numeric arrays, because AML lists hold strings only. Every array
argument must be nonempty with finite values. Results are fresh arrays with the
input's length and shape; the arguments remain unchanged. All three intrinsics
also work inside scalar expressions, function arguments and standalone calls.

## Projection

`field_project` hashes the word's exact UTF-8 bytes with FNV-1a 64: start at
`1469598103934665603`, then for each byte XOR it in and multiply by
`1099511628211`, modulo 2^64. Three taps follow. Each tap selects unit
`h mod len(vec)`, adds `+scale` when bit 20 of `h` is set and `-scale`
otherwise, then advances `h = h * 6364136223846793005 + 1` modulo 2^64.

Taps that land on one unit accumulate in double precision in tap order, starting
from that unit's stored value; each touched unit then rounds once to float32.
A rounded result outside the float32 range is an error and publishes nothing.
Scale must be a finite scalar; the word may be any string, including empty text.

With a zero vector and scale 1, every value is a small integer, so the float32
result equals WOLFE's double field exactly. Accumulated projections at other
scales round each touched unit once per call.

## Norm and dot product

`field_normalize` and `field_dot` accumulate products of the float32 inputs in
double precision. Normalization divides each element by the double norm and
rounds once. The dot product rounds its double sum once; a sum outside the
float32 range is an error. Arrays of different lengths are refused by
`field_dot`, unlike the legacy `dot`, which reads the shorter length.

The scalar `exp`, `log` and `tanh` that WOLFE's sigmoid and IDF formulas need
are core scalar intrinsics (since the scalar-math release): they compute in
float32 and refuse a nonfinite result loudly. The organ adds no scalar math of
its own.

## Verification

```sh
make test-field
```

The suite checks 26 lexical projections taken from WOLFE over the LALO selector
field bit for bit; projections of UTF-8, empty and short words into lengths 1,
2, 3, 7 and 96 at five scales against an independent statement of the
double-sum rule, with the argument unchanged; normalization and dot products of
40 vectors; the 1e-12 norm floor; and exp/log values, including inputs where
float-only `expf`/`logf` round differently. A fixture with nested calls,
standalone calls and a worker agrees in all five execution paths, and 24
invalid programs stop in all five paths before later effects.

Verified on macOS (Apple clang): **3,501 C/runtime checks** across interpreter,
resumable and bytecode modes, with zero compiler warnings. Seven mutations of the
organ (sign bit, FNV prime, per-tap float rounding, norm floor, float `dot`,
`expf`, `logf`) each fail the suite at a named check.
