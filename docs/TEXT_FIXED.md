# Fixed decimal text in AML

`text_fixed(value, digits)` gives an AML scalar a reproducible decimal
spelling. Haiku uses it to identify the climate states remembered by Phase4.
State composition, precision choices and transition memory remain AML code.

```aml
PRINT text_fixed(1.25, 1)   # 1.2: exact halfway value, even last digit
PRINT text_fixed(1.75, 1)   # 1.8
PRINT text_fixed(0.15, 1)   # 0.2: the stored float32 is above 0.15
PRINT text_fixed(0.45, 1)   # 0.4: the stored float32 is below 0.45
PRINT text_fixed(-0.0, 3)   # -0.000
```

## Contract

Both arguments must be scalar values. `value` must be finite; `digits` must
be an integer from 0 through 9. Wrong arity, strings, containers, nonfinite
values and invalid precisions fail explicitly. The name is case-insensitive,
reserved against user definitions, and consumes no function-table slot.

The result is fresh immutable ASCII text containing:

- at least one integer digit, without a leading plus or grouping separators;
- a literal `.` and exactly `digits` fractional digits when precision is nonzero;
- no decimal point at precision zero and no exponent notation;
- a minus sign for every negative input, including negative zero and negative
  values whose magnitude rounds to zero.

Rounding is to the nearest decimal multiple at the requested precision; an
exact tie selects the even scaled integer. The source is the actual stored
IEEE-754 binary32 value. For example, float32 `0.15` is exactly
`0.1500000059604644775390625`, so one fractional digit gives `0.2`.
At the other extreme, formatting the maximum finite float32 at precision zero
gives `340282346638528859811704183484516925440`.

Formatting uses no floating arithmetic or decimal conversion routine. It does
not read or modify locale state, change the process rounding mode, or raise or
clear floating-point exception flags. Evaluation of the argument expression
still follows AML's scalar expression rules; this guarantee begins with the
finite value passed to the formatter.

## Integer implementation

Decode the sign, significand and exponent from binary32. Multiplying the
significand by `5^digits` uses at most 45 bits. The remaining factor is a power
of two. A negative exponent is divided with exact remainder/tie inspection;
a nonnegative exponent shifts into five 32-bit limbs. The maximum scaled
integer occupies 158 bits, and division by ten emits at most 48 decimal
digits. Sign and decimal point give a maximum output of 50 bytes.

The only allocations are the string owner and its output bytes. Refusing
either allocation releases unpublished storage. A failed AML assignment
preserves its previous value; ordinary immutable references carry successful
results through aliases, functions, lists, records and persistent globals.

## C API

```c
AM_String* am_string_fixed(float value, int digits);
```

Returns one owned reference, released with `am_string_free`, or NULL for a
nonfinite value, precision outside 0–9, unsupported float representation or
allocation failure. The core requires the IEEE-754 binary32 float layout for
this operation, matching its checkpoint representation.

## Verification

```sh
make test-text-fixed
cc -shared -fPIC -O2 -Icore core/ariannamethod.c -lm -lpthread -o /tmp/aml-fixed.so
python3 tools/verify_text_fixed.py --library /tmp/aml-fixed.so
python3 tools/verify_text_fixed.py --write-fixtures /tmp/text_fixed_fixture.h
cmp tests/fixtures/text_fixed_fixture.h /tmp/text_fixed_fixture.h
```

Python is a development oracle. Expected values come directly from Python's
fixed formatting after exact binary32 promotion. The 620 frozen cases cover
signed zero, subnormals, the normal/subnormal boundary, neighbouring decimal
thresholds, exact halfway values, integer carry, large integer spacing and
both finite extrema. The direct oracle checks 107,970 cases spanning every
finite exponent, both signs, all ten precisions and seeded random bit patterns.

The C gate runs frozen cases under all four standard rounding modes and all
available test locales. When locale sources are installed, the shell gate
builds a private German locale and requires its comma decimal separator while
output remains `.`. Exception flags and rounding mode remain unchanged.

Interpreter, prepared stepping, bytecode, runner and compiled scalar programs
produce the same fixture bytes. Twenty-six malformed calls stop before later
AML effects or C main; persistent aliases and failed destinations survive.
Allocation refusal covers both string allocations and each preparation,
execution and publication boundary through the three C execution APIs.

The suite also calls every one of 112 and 239 user functions, rejects the
240th definition before effects in all five paths, and runs a 239-function
worker. The shared runtime now admits 8,192 expanded lines; the import suite
checks exact capacity and overflow before effects.
