# Owned workers and returned experience

Governed by the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

A worker carries its launch-time values and code. Its result becomes the
awaiting context's value after the worker has finished. This supports one live
owner publishing a completed experience while the work happens in another
thread.

```aml
def consolidate(state, increment):
    record_set(state, 'count', record_get(state, 'count') + increment)
    return state

def complete_in_worker(candidate, increment):
    SPAWN tail:
        return consolidate(candidate, increment)
    AWAIT tail INTO completed
    return completed

live = record_new()
record_set(live, 'count', 1)
completed = complete_in_worker(live, 2)
assert(record_get(live, 'count') == 1, 'live owner remains independent')
assert(record_get(completed, 'count') == 3, 'worker result survived teardown')
record_swap(live, completed)
```

## Launch boundary

`SPAWN name:` copies the prepared program, including imported function bodies
and their original file/line locations. The worker can call every helper that
was available to its parent. Copies of the source and function table remain
valid after a launching function returns or a resumable host closes its program.

The worker snapshots globals and, inside a function, the currently visible
local frame. These remain separate scopes: the block sees a captured local
argument; a called helper resolves its own arguments and the original global
bindings. Earlier caller frames remain outside the worker's visible scope.
Arrays, lists, maps and records receive independent mutable owners. Immutable
strings, tokenizers and inference models retain atomic references. The parent can mutate
or release its own values immediately after launch.

Source, value-copy, function-preparation and thread-creation failures report an
error without publishing a worker slot. Names contain 1–31 non-whitespace
characters, excluding `:`; they are case-sensitive and may be called `INTO`.
One owner cannot reuse a name until its pending job has been joined. Different
program owners and nested workers can use the same name independently.

The runtime holds at most **16 outstanding jobs** at once. Completed jobs still
occupy their slots until joined; a successful or failed join releases the slot.
Sequential jobs can reuse a name and slot for the lifetime of a conversation.

## Return and publication

A `return value` executed by the worker block supplies one result. It supports
all AML value types: float, array/matrix, UTF-8 string, string list, numeric map,
immutable tokenizer, immutable inference model and flat record. Returns from a helper retain their usual
function meaning; the block returns the helper's result explicitly.

`AWAIT name INTO variable` waits, joins and transfers that result into the
parent's current scope. A return alias into a worker record or local container
survives release of the worker's globals, local frame and persistent store.
Float bits, array shape and record/list/map ordering travel unchanged. The
runtime transfers the completed value without an additional mutable clone.
Only the awaiting owner receives it, exactly once.

A missing worker, duplicate await, worker execution error or absent explicit
return leaves the destination unchanged and stops the AML program. A slot is
consumed after a completed join, including an execution error or missing return.
Syntax and destination-capacity validation happen before that join.

With exactly three arguments and a case-insensitive `INTO` in the middle,
`AWAIT` always transfers one result. The worker and destination may themselves
be named `INTO`: `AWAIT INTO INTO completed` and `AWAIT tail INTO INTO` are
valid. This form takes precedence even if workers named `INTO` and `completed`
are also pending.

Every other nonempty form is a discard list: `AWAIT name1 name2`, `AWAIT INTO`,
`AWAIT first INTO` and `AWAIT first second INTO` join the named jobs and discard
their results. All names must be valid, distinct and pending under this owner.
The runtime checks the entire list before reserving any slot, then reserves
every named job together. An invalid, absent, foreign or repeated name leaves
all pending results untouched. To discard a three-name list whose middle name
is `INTO`, split it into `AWAIT first` and `AWAIT INTO second`.

Bare `AWAIT` drains this program's pending jobs. A validated discard list and
a bare drain both preserve the first execution failure while joining every
requested child. A program's normal completion, error completion
and `am_program_close` also drain that program's children before releasing
parent storage. A worker drains its own children before publishing its result.
A parent error keeps precedence over a later drain error.

The interpreter, bytecode executor, resumable host and `amlc` runtime lowering
use this same ownership boundary. Two interleaved resumable programs have
separate child sets; closing one leaves the other's pending jobs alone. A step
still yields only between top-level statements. An `AWAIT` or a containing
function/loop completes within its current statement.

An application decides when a returned record becomes live. Validation,
checkpoint saving and `record_swap` stay with its publisher. Workers perform
no automatic checkpoint publication.

## Shared process facilities

The process field `G`, field commands, global autodiff tape and global random
facilities retain their existing shared ownership. `SPAWN` isolates AML value
state. Organisms carry private dynamics in records, arrays and owned sampling
maps; NoTorch's numerical value operations use caller-owned buffers. Hosts
configure backend tables and initialize the runtime before starting work.

Channels are process-wide named float queues. Creation, lookup, read, write,
depth and closure all use the registry mutex. A close wakes blocked readers and
writers. A new channel may reuse the slot, and its generation prevents an old
waiter from consuming or writing the replacement queue.

- `CHANNEL CREATE name [capacity]` accepts capacities 1–64, default 64, and
  rejects an existing live name or a full 16-channel registry.
- `CHANNEL WRITE` and `CHANNEL READ` wait on a condition variable with a
  one-second deadline. Missing/closed channels and timeouts become AML errors.
- `CHANNEL TRY` keeps its destination unchanged when a live queue is empty;
  missing or closed queues are errors.
- `CHANNEL DEPTH` retains its query contract: `-1` for a missing queue.
- `CHANNEL CLOSE name` closes one queue; bare `CHANNEL CLOSE` closes all.
  Individual closure rejects a missing queue.

Full `am_init` first closes channels and joins outstanding workers, then resets
field and tape storage. Applications must finish or drain jobs before replacing
runtime configuration. `am_program_close` drains its jobs; it does not close
process channels owned by other contexts.

## C host API

```c
AML_Var result = {0};
if (am_spawn_launch("calculation", "return [2, 3, 5]\n") < 0) {
    /* am_get_error() contains the launch diagnostic. */
}
if (am_spawn_await_value("calculation", &result) == 0) {
    /* result owns the returned array, independent of the worker context. */
}
am_value_clear(&result);
```

`am_spawn_launch` prepares and owns a copy of its script and snapshots the
calling thread's persistent globals. AML-block launch additionally captures
its prepared functions and visible local frame. Directly launched C jobs belong
to the calling C thread; another C thread has its own names and drain scope.

`am_spawn_await_value(name, out)` requires a zero-initialized or valid owned
`AML_Var`. Success releases its former value and replaces it. Failure preserves
it. `am_value_clear` releases and zeroes any returned value.

`am_spawn_await(name)` discards a result. `am_spawn_drain()` joins all jobs
launched directly by this C thread and returns the first failure;
`am_spawn_await_all()` keeps its existing void signature and leaves that
failure in `am_get_error()`. `am_spawn_count()` reports running jobs throughout
the process. Completed unjoined results still occupy slots.

Channel C APIs return zero on a successful transfer. `am_channel_try_read`
returns `-1` for an empty live queue, `-2` for missing/closed queues or invalid
arguments. Blocking transfer failures return `-2`, with a diagnostic.
`am_channel_close(name)` returns zero or `-1`. Queue creation returns a slot
index or `-1`. Output pointers remain unchanged on failed reads.

## Evidence

`make test-workers` checks the interpreter, bytecode, resumable host, runner
and compiled scalar program. The fixture completes 80 repeated typed jobs,
then exercises nested owners, departed launching frames, global/local scope,
UTF-8, scalar signed zero, matrix shape, maps and returned record leaves.
Imported helpers retain their defining source origin inside workers.

The C suite checks destination preservation on errors, missing returns,
duplicate awaits/names, automatic drain and early close, same-named jobs in
two resumable programs and four C host threads, 96 repeated C jobs, all 16
outstanding slots, tokenizer reference lifetime, channel generation reuse
while blocked and reset with an outstanding worker. Linux linker injection
covers failed launch allocations and `pthread_create`, retaining the parent's
original values and leaving the slot reusable.

The expression fixture explicitly closes its channel before being run again
in the next host mode. Duplicate live-channel creation now reports an error.
