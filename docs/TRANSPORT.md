# Hashes, journals and processes

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

An organism that keeps a hash-chained append-only journal and drives external
engines needs three things from its runtime: an exact digest of bytes, one
durable line at a time, and a child process whose results return as values.
Four intrinsics provide them. Chaining, entry schema, retry policy and the
meaning of an engine's output stay in AML.

```aml
head = text_hash_sha256('')
entry = text_concat(head, ' turn 1')
status = file_append_line('journal.log', entry)
head = text_hash_sha256(entry)

argv = list_new()
list_push(argv, '/opt/homebrew/bin/aml')
list_push(argv, '/srv/haiku/step.aml')
env = record_new()
record_set(env, 'HAIKU_TURN', '1')
run = process_run(argv, env, 30)
assert(record_get(run, 'status') == 0, 'engine step')
```

| Intrinsic | Result |
|---|---|
| `text_hash_sha256(text)` | 64 lowercase hex digits: SHA-256 of the text's UTF-8 bytes |
| `file_hash_sha256(path)` | The same digest over a regular file's bytes, streamed in 64 KiB chunks |
| `file_append_line(path, text)` | Scalar 1 after write and fsync; 2 when written but fsync/close failed; text containing LF is an error before the file is opened |
| `process_run(argv)` / `process_run(argv, env)` / `process_run(argv, env, timeout)` | New record `{status, stdout, stderr}` |

Names are case-insensitive, reserved against user function definitions and
consume no function slot. Wrong arity or argument types stop execution before
any file is opened or any process starts, and a failed assignment keeps its
previous value. Paths given to `file_hash_sha256` and `file_append_line`
resolve from the statement's original source file, exactly as checkpoint
paths do; an absolute path stays absolute.

## Content hashes

SHA-256 is implemented in the core from FIPS 180-4 and needs no backend. The
text form hashes the stored bytes exactly: no normalization, no terminator.
The file form accepts regular files only. A FIFO is refused without waiting
for a writer; missing paths, directories and read or close errors are errors.
Memory stays constant for any file size. A file that grows while it is being
read hashes the bytes read through end of file.

Both forms return fresh immutable text, so a digest can enter records, lists,
map keys, checkpoints and worker results like any other string.

## Line journal

`file_append_line` writes `text` followed by exactly one LF. Text containing
LF is refused before the file is opened: an existing journal stays
byte-identical and an absent path stays absent. CR and every other UTF-8
character are ordinary line content; empty text appends an empty line.

The file opens with `O_WRONLY | O_APPEND | O_CREAT` and mode 0644, subject to
the process umask. `O_NONBLOCK` turns a FIFO without a reader into an
immediate error, and `O_CLOEXEC` keeps the journal descriptor out of
`process_run` children. Only regular files are accepted.

The complete line goes to one `write` call. A partial write continues while
it makes progress, so an interrupted write still completes its line. Any write
failure truncates the file back to the length observed before writing: an
error means the journal ends where it ended before the call. If that restore
itself fails, the diagnostic names how many bytes reached the file. A file
created by a failed call remains, empty.

After the line is written the file is synchronized and closed:

| Status | Meaning |
|---:|---|
| 1 | Line written and `fsync` and `close` succeeded |
| 2 | Line written; `fsync` or `close` failed, durability unconfirmed |

Treat 2 as written: appending the same entry again would duplicate the line.
When the call itself creates the file, the containing directory is not
synchronized, so status 1 confirms the file's contents, not its directory
entry. A journal that must survive power loss from its first line is created
and its directory synchronized by its owner beforehand, for example with a
first `checkpoint_save` beside it.

One writer per file is the contract. `O_APPEND` places each write at the
current end, but continuing a partial write and restoring the previous length
both assume no concurrent appender. A hash chain assumes the same.

## External processes

`argv` is a nonempty string list with a nonempty program name. There is no
shell anywhere: each list item reaches the program as exactly one argument, so
spaces, `;`, `$` and `*` are ordinary bytes. The program is found by execvp's
rule, prepared before the fork so the child calls only `execve`. A name
containing `/` is used as given, relative to the process working directory
(not the source file). Otherwise each `PATH` entry of the child's environment
is tried in order, an empty entry meaning `.`, with `/usr/bin:/bin` when `PATH`
is unset. Unlike execvp, an executable without `#!` is not retried through
`/bin/sh`; it fails with `Exec format error`.

`env` is a record whose every field is text, with nonempty names that contain
no `=`. The child receives the inherited environment with those names replaced
or appended; the parent's environment never changes. Program lookup uses the
child's resulting `PATH`. Pass `record_new()` to set a timeout without
environment changes.

`timeout` is a positive finite number of seconds. The child reads `/dev/null`,
so it cannot consume the program's own input, and inherits the working
directory, process group and any descriptor its owner did not mark
close-on-exec. Both output pipes are drained together through `poll`: a child
may fill either stream in any order without blocking the other.

| `status` | Meaning |
|---:|---|
| 0 to 255 | The child exited with this code |
| −1 to −127 | The child was killed by signal N, reported as −N |
| −256 | The timeout expired and the runtime terminated the child |

The timeout covers the child's exit and the close of both streams. At the
deadline the child receives `SIGTERM`, then `SIGKILL` after a one-second grace
period. Reading stops, the child is reaped, and the record carries status −256
with the output captured so far. Only the direct child is signalled. Without a
timeout, a descendant that keeps a stream open keeps the call waiting.

Each captured stream holds at most **1 MiB**, the AML text limit. Output
beyond it, output that is not valid UTF-8, or output containing NUL stops the
child with `SIGKILL`, reaps it and raises an error. A program that cannot be
executed (missing, not permitted, wrong format) raises an error naming the
program and the system reason. Failures before the fork start nothing; failures
after it occur once the child is reaped, and the child's own effects remain.
Every path reaps its child and closes its descriptors.

Workers may run processes concurrently. Descriptor creation and `fork` share
one lock, so no `process_run` child inherits another call's pipes. Other
runtime forks, such as `BLOOD` compilation, do not take that lock.

## Evidence

`make test-transport` builds a C harness that is also the child program. It
checks the NIST vectors, the 56-byte two-block vector, UTF-8 text, and lengths
1/55/56/63/64/65/119/120/128 and 1,000,000 against independent `shasum`
digests, both as text and as files. It checks journal order, mode, empty and
CR lines, LF refusal on existing and absent paths, and FIFO, directory and
missing-directory refusals. For processes it covers exit codes 0/1/3/137,
deaths by signals 9 and 15, inherited/overridden/added environment, a `PATH`
override that hides `echo`, single argv entries, `/dev/null` stdin, closed host
stdin/stdout, invalid UTF-8 and NUL output, missing and non-executable
programs, `SIGTERM` and `SIGKILL` timeouts with partial output, exactly 1 MiB on
both streams, the first refused byte on each, and four workers running twenty
processes concurrently. After each call the harness confirms no child remains
and no descriptor leaked.

A second build interposes `write`, `read`, `fsync` and `ftruncate` beneath the
same suite: fsync failure returns 2 with the complete line, a partial write
followed by `ENOSPC` restores the previous bytes, a partial write that
continues completes the line, a failed restore names its torn bytes, and
seven-byte reads still produce the million-`a` digest. A vector file runs
through the interpreter, resumable host, bytecode, runner and compiled program,
and eleven invalid calls stop in all five before later AML, C `main()` or
journal effects.
