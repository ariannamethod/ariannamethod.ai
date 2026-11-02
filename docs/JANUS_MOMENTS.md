# JANUS.CALENDAR — exact moments and intervals

A calendar date and an event's position within that date are addressable
coordinates. JANUS.CALENDAR now compares full POSIX moments, computes exact
signed durations and relates half-open intervals to events and to each other.
The [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md) governs this work.

## Audit and representation

Baseline `5e893f9` carries second/millisecond fields in `janus_event`; its
`janus_compare`, `janus_distance` and `janus_interval` operate on civil days.
Those existing day operations retain their behavior. The new moment operations
consume explicit, versioned record values.

| Value | Canonical fields, besides `schema` and `time_scale` |
|---|---|
| `janus.instant/1` | `unix_day`, `second`, `millisecond` |
| `janus.duration/1` | `sign`, `days`, `seconds`, `milliseconds` |
| `janus.span/1` | `start_unix_day`, `start_second`, `start_millisecond`, `end_unix_day`, `end_second`, `end_millisecond` |

`time_scale` is **`posix`**. An instant's `unix_day=0` means 1970-01-01;
2024-10-03 has Unix day 19999. `second` is 0..86399 and `millisecond` 0..999.
The supported instant domain is Gregorian **0001-01-01 00:00:00.000 UTC**
through **9999-12-31 23:59:59.999 UTC**, Unix days −719162..2932896.

A duration's components are nonnegative. `sign` is −1 or +1 for a nonzero
magnitude and exactly 0 for zero. `days` is 0..3652058, `seconds` 0..86399 and
`milliseconds` 0..999. A duration day is exactly 86,400 POSIX seconds.
Noncanonical zero signs, overflowing components and fractional values fail.

All stored components are exactly representable AML float32 integers. Native
C uses checked-domain `int64_t` arithmetic for complete millisecond totals.
The full-domain duration is **315537897599999 ms**; it travels as separate
sign/day/second/millisecond fields. No aggregate float32 timestamp or duration
is created. POSIX second 86400 is rejected, following the existing UTC contract.

These operations sample no clock. The caller chooses and captures the instant.
Calendar/zone views continue through `calendar_date` and `calendar_local`;
local conversion takes the instant's Unix day and UTC second plus the explicit
offset and sunset policy.

## AML interface

| Function | Result |
|---|---|
| `calendar_instant(unix_day, second, millisecond)` | New exact instant record |
| `calendar_compare(first, last)` | −1, 0 or +1 for first before, equal to or after last |
| `calendar_duration(first, last)` | Signed duration `last-first` |
| `calendar_duration_new(sign, days, seconds, milliseconds)` | Checked canonical duration record |
| `calendar_shift(instant, duration)` | New instant after signed exact addition |
| `calendar_span(start, end)` | Ordered half-open interval `[start,end)` |
| `calendar_contains(span, instant)` | 1 when start ≤ instant < end; otherwise 0 |
| `calendar_overlaps(first, second)` | 1 when the spans share a nonempty interval; otherwise 0 |
| `calendar_intersection(first, second)` | New span representing their intersection |

The nine names are reserved case-insensitively without consuming registered
user-function slots. Arguments are evaluated once by existing typed dispatch.
The compiler, interpreter, resumable program and bytecode paths call the same
C calculation.

```aml
first = calendar_instant(-1, 86399, 999)
last = calendar_instant(0, 0, 0)
lag = calendar_duration(first, last)
assert(record_get(lag, "milliseconds") == 1, "one exact millisecond")
assert(record_equal(calendar_shift(first, lag), last), "exact return")
window = calendar_span(first, last)
assert(calendar_contains(window, first) == 1, "included start")
assert(calendar_contains(window, last) == 0, "excluded end")
```

Spans require start ≤ end. A zero-length span is valid and contains no moment.
Touching endpoints alone do not overlap. When an intersection is empty, its
start and end both equal the later of the two input starts. This convention
is symmetric, including for empty input spans. Spans retain only their
endpoints; duration is calculated from the corresponding instant values.

Inputs must carry their schema, `time_scale` and all canonical numeric fields.
Extra application leaves are accepted and left in the original record.
Arithmetic results contain their canonical fields. Each use validates domain,
component bounds, sign and endpoint order. Returned records own their values.
Callers may attach their existing assertion/source receipts to these results.

Wrong types, missing or mismatched schemas/units, invalid ordering, range
errors and shift overflow stop execution. The assignment destination is
preserved on failure. Completed earlier statements and eagerly evaluated
arguments retain their existing execution effects. The original input records,
bound `BIRTH` and `AM_State` remain unchanged by the calculations.

## The two clocks of an event

The existing `janus_event` contract is unchanged. Its importable module now
provides `janus_event_instant(event)` for occurrence and
`janus_known_instant(event)` for knowledge/receipt time:

```aml
IMPORT "janus/calendar.aml"
event = janus_event(20730, 43201, 123, 20730, 43210, 456, 498)
lag = calendar_duration(janus_event_instant(event), janus_known_instant(event))
assert(record_get(lag, "seconds") == 9, "receipt distance")
assert(record_get(lag, "milliseconds") == 333, "receipt fraction")
```

Both clocks remain attached to the fact's original provenance. Ordinary record
ownership, worker snapshots and checkpoints retain the split values exactly.
The runnable example is `examples/calendar-instants.aml`.

## C, native JavaScript and WASM

`am_calendar_query(operation,args,count,out)` is the shared allocation-free C
boundary for the 16 calendar operations. `AM_CalendarReply.kind` selects an
integer scalar (1) or `AM_CalendarValue` record (2).
`am_calendar_argument_count(operation)` returns the exact integer tuple arity,
or −1 for an unknown operation. Failure preserves the entire caller output.
Operation IDs 0..6 retain their original meanings; IDs 7..15 add moments.
The public enum in `core/ariannamethod.h` names each operation.

The native CLI and WASM bridge call this same dispatcher. AML's typed boundary
reads records and sends their exact integer components to it. The native
transport serializes the allocation-free reply directly. JS's shared
`encodeRequest` checks record shape and encodes the same tuples.

```js
import { bundledCalendar } from './janus/calendar_browser.mjs';
const calendar = await bundledCalendar();
const first = await calendar.instant(-1, 86399, 999);
const last = await calendar.instant(0, 0, 0);
const distance = await calendar.duration(first, last);
const window = await calendar.span(first, last);
const included = await calendar.contains(window, first);
```

`nativeCalendar()` and HTTP `createCalendar(httpTransport(...))` expose the
same methods: `instant`, `compare`, `duration`, `duration_new`, `shift`, `span`,
`contains`, `overlaps`, `intersection`. Existing seven calendar methods retain
their request/response shapes. The outer transport remains `janus.calendar/1`.
The internal WASM ABI is **2**, with twelve integer request slots. The new
adapter checks that version; previously embedded self-contained ABI-1 bundles
continue with their own adapter and binary. Current bundles include both the
updated adapter and canonical binary with their source/binary SHA-256 stamps.

The complete component remains import-free with fixed 128 KiB memory. At this
release its WASM binary is **9,966 bytes**, browser module **20,233 bytes** and
one-file HTML example **21,711 bytes**. Rebuild using `make calendar-wasm` and
the documented WASI SDK. The browser bundle contains its binary.

## Verification

`make test-calendar-moments` checks the full runtime and allocation-free C
builds; 469 independently generated BigInt operations through interpreted,
prepared, bytecode, runner and compiled AML; three host timezones; event-clock
conversion; worker snapshots; checkpoints; and failed assignment preservation.
The JS suite uses 4,609 independent BigInt witnesses in WASM, 659 through the
native transport and all nine new operations through HTTP.

Witnesses include negative epochs, adjacent milliseconds, the entire supported
date range, signed duration/addition round trips, zero values, empty/touching/
nested/disjoint intervals, symmetric intersections, malformed records,
noncanonical units/components and both range-overflow directions. The pure C
boundary passes AddressSanitizer and UndefinedBehaviorSanitizer. Existing
calendar, import, UTC, record, expression, worker and compiler suites remain
part of the regression check.

RESONANCE UNBROKEN.
