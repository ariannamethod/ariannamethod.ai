# JANUS.CALENDAR consumer contract / 1

AML owns calendar computation. LALO owns factual provenance. AMOS owns its
acquired perspective. The [calendar API](JANUS_CALENDAR.md) is implemented in
this repository. Consumer changes are performed in their own reviewed work.

## LALO

LALO `validFrom`, `validTo` and `recordedAt` are integer Unix milliseconds.
Keep the fact journal's assertion IDs, source, supersedes links, occurrence
interval and knowledge time. Attach temporal annotations alongside those
receipts, identifying the source assertion ID and calendar schema/model IDs.
For a revised occurrence time, annotate the revised assertion separately.
Historical queries select the assertion known then and its annotation.

JavaScript's `splitUnixMilliseconds` and `joinUnixMilliseconds` preserve the
original integer timestamps, including −1 ms. In AML carry Unix day,
second-of-day and millisecond separately. `janus_event` accepts occurrence and
knowledge splits plus the subject's birth ordinal, returning a flat
`janus.event/1` record. Its date/subject view belongs to the occurrence time;
`known_unix_day`, `known_second` and `known_millisecond` preserve the second
clock explicitly. Occurrence and knowledge are allowed in either order.
The consumer retains the original assertion's validity end separately.

```aml
IMPORT "../janus/calendar.aml"
note = janus_event(20730, 43200, 123, 20730, 43205, 456, 498)
record_set(note, "assertion_id", "quake:revision:2")
```

The returned annotation is an ordinary AML record. It can be captured by a
worker or checkpointed with the existing ownership/wire contract. Several
subjects can view the same source assertion by supplying distinct birth days.

## AMOS 2 integration points

Inspection baseline: `ariannamethod/amos` at
`251c6971bb9fb3c0da27fd11e683de7990a976c5`, world format 6.

| Existing point | Proposed consumer integration |
|---|---|
| `worlds/lalo/engine.js`: source facts → `_commit(ledger,events)` | Obtain calendar annotation before accepting an encounter; retain source IDs and both original clocks |
| `worlds/lalo/relations.js`: `context` and `RelationField.observe` | Present selected signed distances, recurrence and calendar-gap values with their receipt anchors |
| `FactMind`, phase predictors and somatic loop | Let their existing acquired mechanisms encounter the new values |
| Engine dump/load, validation, world version and organ birth counters | Version optional temporal state and preserve the acquired pre-integration life |
| `Amos-2.html` and optional `worlds/` directory | Bundle the transport client when enabling integration; preserve standalone execution with integration disabled or unavailable |

A calendar origin is explicitly chosen from an existing historical receipt,
with its policy and provenance retained. An organ's installation time and an
organism's prior origin are separately identified. Calendar enrichment retains
acquired memory, glyph identities, recurrent states, forecasts, random streams
and historical receipts. Calendar fields arrive as structured observations;
the consumer's acquired learning determines what they mean to the subject.

The browser adapter is `janus/calendar.mjs`. Its `createCalendar(transport)`
contract is asynchronous and versioned. A host may provide the same-origin
HTTP handler from `calendar_native.mjs`, or inject another transport to the
canonical C API. The current browser path requires that explicit host. A
standalone AMOS file continues without temporal enrichment when the adapter
is unavailable. Preserve any already acquired temporal observations across
that continuation.

Proposed consumer verification: unchanged disabled behavior; same fact read
with distinct subject origins; correction and historical-query receipts;
acquired relation changes under matched observations; checkpoint/restart with
pending encounters; format migration retaining prior learned state; and
standalone HTML continuation after its optional transport/world folder is
removed. These are AMOS integration requirements; the tests in this AML PR
verify the published computations and adapters.

## Wider temporal organs

actually.janus keeps expectations, witnesses, reread seams and temporal debt.
LALO keeps sourced assertions and their revisions. AML exposes the calendar
coordinates and existing prophecy/destiny field operations. Consumers can
relate an expectation and its later witness through these shared coordinates,
retaining each receipt and their own update law.

This change modifies only the AML repository.

RESONANCE UNBROKEN.
