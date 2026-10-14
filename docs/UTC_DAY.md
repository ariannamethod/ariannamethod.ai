# UTC civil day values

`utc_day()` gives an AML program one explicit civil-day value from the system
clock. It takes no arguments and returns `floor(POSIX_seconds / 86400)`.

| UTC instant | Day |
| --- | ---: |
| 1970-01-01 00:00:00 | 0 |
| 1970-01-01 23:59:59 | 0 |
| 1970-01-02 00:00:00 | 1 |
| 2024-02-29 00:00:00 | 19782 |
| 2024-03-01 00:00:00 | 19783 |
| 2024-10-03 00:00:00 | 19999 |
| 2024-10-03 12:00:00 | 19999 |

The boundary is midnight UTC. Host timezone and daylight saving settings have
no effect. POSIX seconds are divided at double precision; the final day is an
exact float32 integer. The inclusive day domain is `[-16777216, 16777216]`.
Negative seconds use floor, so `-2` and `-86400` both belong to day `-1`, and
`-86401` belongs to day `-2`, on hosts with signed pre-epoch time.

Each call reads `time(NULL)` once. A `(time_t)-1` return stops execution with
`utc_day could not read the system clock`; this sentinel includes POSIX second
`-1`. An ordinal outside the exact domain stops execution with
`utc_day exceeds the exact scalar day range`. Arguments produce
`utc_day requires no arguments`. Errors preserve the assignment destination;
earlier statements and eagerly evaluated arguments retain their effects.

The intrinsic leaves the complete `AM_State` and global calendar controls
unchanged. Existing Janus field calculations retain their 2024-10-03 **noon**
UTC epoch; an owner using another calendar origin defines its conversion from
the Unix civil ordinal explicitly.

Capture a day when accepting an experience, then carry the scalar with that
experience:

```aml
request = record_new()
record_set(request, "day", utc_day())
SPAWN experience:
    return request
AWAIT experience INTO received
assert(record_get(received, "day") == record_get(request, "day"), "same day")
```

Workers receive the captured value through ordinary snapshot ownership. A
checkpoint can retain the scalar using the existing record encoding. Programs
choose their own capture, replay and publication policy.

The name is reserved case-insensitively. The intrinsic uses expression dispatch,
preserving all 239 user function slots, 8,192 expanded lines and 512 record fields.
Preparing or compiling a program reads no clock; its execution evaluates the
call. The core archive supplies it directly, with no optional backend or new
public C API.

`make test-utc-day` covers the three C execution APIs, runner and compiled CLI.
On Linux, linker `--wrap=time` supplies fixed boundary/failure fixtures entirely
inside the tests. The production runtime exposes no clock override.
