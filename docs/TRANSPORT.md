# Hashes, journals, file slices and processes

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

An organism that keeps a hash-chained append-only journal and drives external
engines needs four things from its runtime: an exact digest of bytes, one
durable line at a time, a way to read the journal back, and a child process
whose results return as values. Six intrinsics provide them. Chaining, entry
schema, retry policy, line splitting and the meaning of an engine's output stay
in AML.

```aml
head = text_hash_sha256('')
entry = text_concat(head, ' turn 1')
status = file_append_line('journal.log', entry)
head = text_hash_sha256(entry)

offset = 0
piece = file_read_slice('journal.log', offset, 65536)
while text_bytes(piece) > 0:
    offset = offset + text_bytes(piece)
    piece = file_read_slice('journal.log', offset, 65536)
assert(offset == file_size('journal.log'), 'journal read to its end')

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
| `file_size(path)` | A regular file's size in bytes, an exact scalar up to 16,777,216 |
| `file_read_slice(path, offset, max_bytes)` | Fresh text: at most `max_bytes` bytes of a regular file from byte `offset`, ending on a whole character; `''` at end of file |
| `process_run(argv)` / `process_run(argv, env)` / `process_run(argv, env, timeout)` | New record `{status, stdout, stderr}` |

Names are case-insensitive, reserved against user function definitions and
consume no function slot. Wrong arity or argument types stop execution before
any file is opened or any process starts, and a failed assignment keeps its
previous value. Paths given to `file_hash_sha256`, `file_append_line`,
`file_size` and `file_read_slice` resolve from the statement's original source
file, exactly as checkpoint paths do; an absolute path stays absolute.

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

## File size and slices

`file_size(path)` opens the path read-only and returns the size `fstat`
reports. `file_read_slice(path, offset, max_bytes)` opens it the same way and
reads with `pread`. No descriptor, position or buffer outlives a call, so two
identical calls on an unchanged file return identical text. Both accept regular
files only: as with hashing, `O_NONBLOCK` refuses a FIFO without waiting for a
writer, and missing paths, directories and open, stat, read or close failures
are errors.

Sizes and offsets count bytes. AML numbers are float32 and exact for integers
through 2^24, so `file_size` refuses a file larger than 16,777,216 bytes,
`offset` must be an integer in `[0, 16777216]`, and a slice that would end
beyond byte 16,777,216 is an error instead of a cursor that rounds. `max_bytes`
is an integer in `[0, 1048576]`, the text limit.

| Case | Result |
|---|---|
| `max_bytes` is 0 | `''`; the file is still opened and checked |
| `offset` at or past the end of the file | `''`: end of file, not an error |
| otherwise | the longest run of whole characters that starts at `offset` and fits within `max_bytes` bytes and the current end of the file |

A slice cuts bytes, not codepoints, while a text value holds only whole UTF-8
characters. A character cut by the `max_bytes` edge or by the current end of
the file is left out of the slice; it begins the slice that starts at the next
offset. A slice can therefore be up to three bytes shorter than requested, and
the cursor advances by `text_bytes(slice)`, never by `max_bytes` or
`text_len`. The runtime buffers and joins nothing: assembling lines across
slices is AML's work. An `offset` inside a character, invalid UTF-8, and a
`max_bytes` smaller than the character at `offset` are errors, so an empty
slice never hides a cursor that cannot advance. A NUL byte inside the slice is
an error: binary data does not pass as text. Only the slice's own bytes are
checked, so the bytes before a NUL or an invalid byte remain readable.

The read is bounded by the size `fstat` reported when the call opened the
file. A file that its single writer is still appending is legal input: each
slice comes from the prefix present at that moment, and a later call sees what
was appended since. A writer that has written part of a character leaves an
incomplete sequence at the end of the file; a slice starting there returns
`''` until the rest arrives, so a reader that follows the file polls without
an error. On a finished file, `''` with `offset < file_size(path)` means the
file ends with up to three bytes of an incomplete sequence. A file truncated
during the call yields what `pread` still finds.

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
missing-directory refusals. For slices it reads a journal written by
`file_append_line` at every offset with every `max_bytes` up to two past the
end, against the longest-whole-character rule; empty files, offsets at and
past the end, zero `max_bytes`, both edges of the exact range on a sparse
16 MiB file, cuts through two- and four-byte characters, an offset inside a
character, NUL, invalid UTF-8, FIFO, directory and missing paths. A
1.2 MB file of Hebrew, Cyrillic and emoji lines is read by an AML cursor
with 65,537-byte slices and rebuilt line by line through `file_append_line`;
the copy's `file_hash_sha256` equals the source's. A child process appends
lines in two writes split inside a four-byte character while AML follows the
file with seven-byte slices; the slices join to the final file without an
error. For processes it covers exit codes 0/1/3/137,
deaths by signals 9 and 15, inherited/overridden/added environment, a `PATH`
override that hides `echo`, single argv entries, `/dev/null` stdin, closed host
stdin/stdout, invalid UTF-8 and NUL output, missing and non-executable
programs, `SIGTERM` and `SIGKILL` timeouts with partial output, exactly 1 MiB on
both streams, the first refused byte on each, and four workers running twenty
processes concurrently. After each call the harness confirms no child remains
and no descriptor leaked.

A second build interposes `write`, `read`, `fsync`, `ftruncate`, `open`,
`fstat` and `pread` beneath the same suite: fsync failure returns 2 with the
complete line, a partial write followed by `ENOSPC` restores the previous
bytes, a partial write that continues completes the line, a failed restore
names its torn bytes, and seven-byte reads still produce the million-`a`
digest. Failed `open`, `fstat` and `pread` refuse slices and sizes with the
system reason and no leaked descriptor; three-byte and `EINTR`-interrupted
`pread` calls still return the exact slice. A vector file runs through the
interpreter, resumable host, bytecode, runner and compiled program, reads its
own bytes back through five-byte slices, and sixteen invalid calls stop in all
five before later AML, C `main()` or journal effects.
