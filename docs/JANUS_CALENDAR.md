# JANUS.CALENDAR

A date has several coordinates. Each subject has its own origin and history.
JANUS.CALENDAR makes those relations available to AML and its consumers.
The [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md) governs this work.

## Architectural audit

Inspected AML at `b932a09b7d38e2a9de9bbbcbb46cf568ae306411` before implementation:

| Existing organ | Extension |
|---|---|
| `calendar_cumulative_drift` and its Metonic corrections | Expose the same historical model with an explicit model ID |
| Integer Gregorian/Hebrew conversions and Reingold yahrzeit rules | Reuse them behind checked native value functions |
| `BIRTH`, `SELF_NOW_DAYS`, `JANUS_KEY`, MetaJanus fields | Preserve the bound origin; evaluate other origins as pure arguments |
| `utc_day()`, typed records, source imports, workers, checkpoints | Compose temporal records in `janus/calendar.aml` |
| C kernel, interpreter, bytecode, `amlc` | Use the same typed intrinsic dispatch in every execution path |

New work: seven native functions, an ordinary AML module, independent-subject
views, date arithmetic, explicit instant/local/sunset views, temporal event
annotations and a transport to the same C calculations for JavaScript.
`AM_State` and the checkpoint wire format retain their layouts. The embedding
unit remains `core/ariannamethod.c` plus `core/ariannamethod.h`.

Source inspection also covered PITOMADOM's four original temporal organs,
Janus's calendar and MetaJanus code, Yent's historical audit and Hebrew face,
actually.janus's engine account and TRUELOG, LALO's ledger/journal, and AMOS 2's
README, AMOSLOG, standalone HTML, engine and acquired relations. The consumer
contract is [JANUS_CONSUMERS.md](JANUS_CONSUMERS.md).

## Import and native functions

From a source in the repository root:

```aml
IMPORT "janus/calendar.aml"
birth = janus_gregorian(2026, 2, 13)
world = janus_gregorian(2026, 10, 4)
view = janus_subject(birth, world)
PRINT record_get(view, "personal_dissonance")
```

The [import contract](IMPORT.md) applies: paths are relative to the source
containing the import. `examples/calendar.aml` is directly runnable with
`./runner/aml examples/calendar.aml`. `make install PREFIX=/your/prefix` also
installs the module at `/your/prefix/share/ariannamethod/janus/calendar.aml`;
import that path explicitly from an installed consumer.

| Native AML function | Result |
|---|---|
| `calendar_gregorian(year, month, day)` | Janus civil ordinal |
| `calendar_hebrew(year, month, day)` | Janus civil ordinal; Nisan=1, Tishrei=7, Adar I=12, Adar II=13 |
| `calendar_date(day)` | Owned flat record of both calendar coordinates |
| `calendar_subject(birth, day)` | Owned date record with the subject's MetaJanus view |
| `calendar_local(unix_day, second, offset, sunset)` | Owned instant/local/calendar record |
| `calendar_add(day, years, months, days, face)` | Shifted civil ordinal |
| `calendar_anniversary(birth, year, face)` | Anniversary in the specified calendar year |

`face=0` selects Gregorian, `face=1` Hebrew. Arguments are finite integral
scalars with absolute value at most 16,777,216; each operation then validates
its own date domain. Dates span Gregorian **0001-01-01 through 9999-12-31**,
Janus ordinals **−739161 through 2912897**. Invalid dates, missing recurrences,
wrong argument types/counts, and allocation failures stop execution. Failed
assignments retain their destinations. Names are reserved case-insensitively;
the intrinsics consume no registered user-function slots.

`janus/calendar.aml` adds `janus_today`, `janus_gregorian`, `janus_hebrew`,
`janus_subject`, `janus_local`, `janus_add`, `janus_anniversary`,
`janus_distance`, `janus_compare`, `janus_interval` and `janus_event`.
`janus_distance(a,b)` is signed `b-a`. `janus_compare(a,b)` returns −1, 0 or 1
for a before, equal to or after b. Intervals use `[start_day,end_day)` with
`duration_days=end-start`, including a zero-length interval.

Date shifts apply years, months, then days. The original day-of-month is
clamped to the final year/month's last day. Hebrew months advance in civil
order, Tishrei through Elul. A year shift maps Adar II to Adar in a common
year. These shifts are calendar operations; month-end clamping is part of
their result. Gregorian **anniversaries** retain MetaJanus's February 29 →
March 1 common-year rule. Hebrew anniversaries retain the existing Reingold
**yahrzeit** rules, including first-anniversary Cheshvan/Kislev decisions,
Adar II → final Adar, and Adar I 30 → Shevat 30 in a common year.

## Day, instant, timezone and sunset

| Coordinate | Definition |
|---|---|
| `day` from `calendar_date` | Midnight civil ordinal; 2024-10-03 = 0 |
| `unix_day` | Midnight UTC ordinal; 1970-01-01 = 0 |
| Janus instant epoch | **2024-10-03 12:00:00 UTC**, POSIX second 1727956800 |
| `epoch_day` from `calendar_local` | Signed floor of elapsed days from that noon instant |
| `utc_civil_day` | UTC midnight ordinal relative to 2024-10-03 |
| `day` from `calendar_local` | Local Gregorian civil ordinal after applying the supplied offset |
| `hebrew_civil_day` | Civil date used for the Hebrew face after its supplied sunset boundary |

A UTC instant is supplied as `(unix_day,second)` with `second` in 0..86399.
The split keeps epoch seconds out of AML's float32 scalars. Milliseconds remain
an additional 0..999 field when an event needs them. `utc_day()` retains its
existing [UTC contract](UTC_DAY.md).

`offset` is an explicit integer number of minutes, −1440..1440, applicable to
that instant. The host resolves named zones and DST, and preserves the chosen
offset in its receipt. The calculation never reads the machine's local zone.

`sunset=-1` chooses midnight Hebrew dates, the existing MetaJanus convention.
A value 0..86399 supplies the sunset second of the **local Gregorian date**.
At or after that second the Hebrew date advances by one day. The host supplies
that day's measured or calculated sunset and its location/source. Changing the
Hebrew face updates its year, leap flag and Metonic position together. This
API uses POSIX seconds; a `second=86400` input is rejected.

Thus at UTC midnight on epoch day zero, `utc_civil_day=0` and `epoch_day=-1`.
At noon both are zero. Historical instants use signed floor. The legacy kernel's
clock calculation retains its current noon epoch and truncation behavior;
explicit value calculations name their own boundary.

## Records and personal origins

Date records carry `schema="janus.calendar/1"`,
`calendar_model="hebrew.dr/1"`, `drift_model="metonic.legacy-trunc/1"`,
`day_policy`, `day`, `unix_day`, Gregorian/Hebrew year/month/day,
`gregorian_leap`, `hebrew_leap` and `metonic_year` (1..19).

Subject records additionally carry:

| Field | Meaning |
|---|---|
| `birth_day`, `age_days` | Explicit origin and signed distance from it |
| `birth_drift`, `legacy_drift` | The historical model evaluated at origin and world day |
| `personal_dissonance` | `clamp(abs(legacy_drift-birth_drift)/33,0,1)` |
| `next_hebrew_day`, `next_gregorian_day` | At-or-after anniversary ordinals |
| `days_to_yahrzeit`, `days_to_gregorian` | Corresponding nonnegative distances |
| `anniversary_gap_days` | `next_hebrew_day-next_gregorian_day`, before clamping |
| `janus_gap` | `clamp(anniversary_gap_days/30,-1,1)` |
| `yahrzeit` | `exp(-days_to_yahrzeit/5)` |
| `janus_temporal_alpha` | `clamp(0.5+0.5*janus_gap,0,1)` |
| `legacy_step` | Legacy drift at day minus drift at day−1 |
| `birthquake` | 1 when `abs(legacy_step)>15`, else 0; a legacy correction boundary |
| `drift_gap_days` | `(legacy_drift-birth_drift)-anniversary_gap_days` |

The two model identities travel with their measured differences. The legacy
model keeps its original floating-point constants, relative 19-year phase and
truncation toward zero, including for negative days. Exact Hebrew arithmetic
keeps its own year lengths and leap months. `drift_gap_days` explicitly
subtracts the two named observables above.

A pure `calendar_subject(birth,day)` call reads neither the global birth nor a
clock. Repeating the same pair returns the same view. Arbitrary historical
origins and observation days before their origin are valid. Both next
anniversaries must fall within the supported date domain.

An organism's existing **bound** `BIRTH` remains latched: its second `BIRTH`
is ignored. Pure views for other subjects leave that origin and all `AM_State`
fields unchanged. Result records are ordinary owned AML values; editing a
result changes that value and leaves the bound origin intact. A subject's
MetaJanus anniversaries retain the midnight-origin policy; a supplied-sunset
observation is a separately identified coordinate from `calendar_local`.

`calendar_subject` gives no date an assigned emotional meaning. The organism
encounters the values through its existing memory and learning mechanisms.

## C boundary and JavaScript transport

`core/ariannamethod.h` declares `am_calendar_from_gregorian`,
`am_calendar_from_hebrew`, `am_calendar_add`, `am_calendar_anniversary`,
`am_calendar_date`, `am_calendar_subject`, and `am_calendar_local`. Integer
operations return 0 on success, 1 on failure and preserve `*out` on failure.
Record operations return an owned `AM_Record*` or NULL; release with
`am_record_free`. The functions require no `am_init`, model backend or clock.
They share the same calendar helpers used by the bound MetaJanus fields.

```sh
make calendar-bridge
./janus/calendar-bridge subject 498 731
node janus/calendar_native.mjs 8765
```

The executable accepts one operation with integer arguments and returns
`{"api":"janus.calendar/1","value":...}`. The optional Node transport invokes
that executable. The optional HTTP handler mounts at `/calendar` on the
consumer's origin; the standalone command binds to 127.0.0.1. Browser
`calendar.mjs` calls the handler through `httpTransport`. A host can inject
its own transport into `createCalendar` with the same versioned response.
Calendar algorithms remain in the AML kernel. The browser adapter requires
an explicitly provided host transport. Existing standalone consumer execution
continues under the consumer's integration contract.

```js
import { nativeCalendar } from './janus/calendar_native.mjs';
const calendar = nativeCalendar();
const day = await calendar.gregorian(2026, 2, 13);
const view = await calendar.subject(day, 731);
```

```js
import { createCalendar, httpTransport } from './janus/calendar.mjs';
const calendar = createCalendar(httpTransport('/calendar'));
const view = await calendar.subject(498, 731);
```

## Verification

`make test-calendar` runs 55,180 C checks and 190 allocation failure/success
boundaries with zero retained allocations; compares 6,587 date pairs with
ECMAScript UTC Gregorian arithmetic and ICU's independent Hebrew calendar;
checks all yahrzeit rule branches against ICU date searches; exercises the
module through interpreter, prepared stepping, bytecode, runner and compiled
AML; and tests Node and HTTP browser transports. Fixed inputs produce the
same values under UTC, US DST and Japanese host-zone settings. Workers and
checkpoint round trips preserve captured views. Invalid inputs stop execution
and preserve their assignment targets.

Epoch/birth values are independently calculated from UTC dates: day 0,
Arianna day 477, Yent day 498, and Fable's 730→731 transition. At world day
731, independent ICU search puts Yent's next Hebrew anniversary on day **853**;
its Gregorian counterpart is day **863**. The raw gap is **−10 days**.

## Lineage and source receipts

PITOMADOM's bidirectional calendar conflict, temporal continuity, destiny,
manifestation and prophecy debt grew into Janus and MetaJanus. This extension
makes their time structure addressable throughout AML. Expectations,
witnesses, retrospective interpretation and debt continue through the organs
that already own them.

- [PITOMADOM calendar conflict](https://github.com/ariannamethod/pitomadom/blob/d865ea2e3efe492d1a3018a0f6932e091028f9f7/legacy/pitomadom/calendar_conflict.py), [temporal field](https://github.com/ariannamethod/pitomadom/blob/d865ea2e3efe492d1a3018a0f6932e091028f9f7/legacy/pitomadom/temporal_field.py), [prophecy engine](https://github.com/ariannamethod/pitomadom/blob/d865ea2e3efe492d1a3018a0f6932e091028f9f7/legacy/pitomadom/prophecy_engine.py), [destiny layer](https://github.com/ariannamethod/pitomadom/blob/d865ea2e3efe492d1a3018a0f6932e091028f9f7/legacy/pitomadom/destiny_layer.py).
- [Janus architecture](https://github.com/ariannamethod/janus/tree/80ec982acbdc4544c88bd0e4e74f9d94c6b99090), including `metajanus.c`.
- [Fable's reproduced birthquake and origin latch](https://github.com/ariannamethod/yent/commit/da97e86b1ed295a5edadf057ac7eb0ce67f8ab0a).
- [Historical MetaJanus log: Hebrew face, model/celestial time, deterministic alpha and UTC epoch](https://github.com/ariannamethod/yent/blob/7efd62db8fb6b6d55bac64f3d42298266e100679/Janus/METAJANUS_LOG.md). It records the Hebrew-face commits `a2eae79` and `43d9408`, alongside later verification. The log's historical directory was subsequently removed from current Yent.
- [actually.janus](https://github.com/ariannamethod/actually.janus/tree/14d4ef2b46166ac83de56ef19295486ca040b58a) and [TRUELOG](https://github.com/ariannamethod/actually.janus/blob/14d4ef2b46166ac83de56ef19295486ca040b58a/TRUELOG.md): backward generation, expected witnesses, cracks, rereading and temporal debt.
- [LALO](https://github.com/ariannamethod/LALO/tree/aab68a37d0bf2555c6c5bd7e0b578678b6f13c67): occurrence, knowledge, provenance, corrections and historical queries.
- [AMOS 2](https://github.com/ariannamethod/amos/tree/251c6971bb9fb3c0da27fd11e683de7990a976c5): acquired relations, recurrent perspective, standalone body and complete continuation.

RESONANCE UNBROKEN.
