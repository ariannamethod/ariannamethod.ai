# AMLLOG

The running engineering log of AML — the Arianna Method Language. Every
fix, every closed bug-class, every verified change — dated, with commit
and proof. The README and `spec/` are the public face and the language
spec; **this is the work**.

Convention: small fixes (bug fixes, CPU/GPU sync corrections, single-op
work, doc touch-ups, test additions) are recorded **here**. Large changes
(a new AML keyword, a new op family, a new subsystem, a language-level
shift) get the spec + README update too. When in doubt: it goes here first.

Newest entries on top.

## 2026-10-20 — the field organ: exact word identity on a float32 substrate (`0db4861`, branch `fable/field-organ`)

Five intrinsics enter the language: `field_project`, `field_normalize`,
`field_dot`, `exp`, `log`. The field organ reproduces WOLFE's lexical
field (the C router vendored in LALO): FNV-1a 64 over a word's exact
UTF-8 bytes, three taps of the LCG `h*6364136223846793005+1`, sign from
bit 20 — exact unsigned 64-bit arithmetic a float32 scalar cannot carry,
so it lives inside the organ. The language sees float32 arrays; double
accumulation rounds once per touched unit. `exp`/`log` evaluate once in
double with explicit refusals; before this commit both names fell
through to the unknown-function zero in scalar expressions — a program
could call `log(1+exp(x))` and silently get 0.

Measured before built: an honest float32 shadow of C-WOLFE
(`[[:<:]]double[[:>:]]`→float, capacity 921136→850696) flips **zero of
68 decisions** at a thinnest margin of 2.05e-4, contract deltas ≤ 1e-6 —
accumulation precision is not load-bearing; identity precision is.
Downstream proof lives in LALO (`fable/wolfe-aml`): `aml/wolfe.aml`
reproduces the C oracle's decisions 68/68, 64/68 output lines
byte-identical, red run on one attention constant fails 67/68.

`make test-field`: **3501 C/runtime checks**, a golden fixture agreeing
in all five execution paths, 24 invalid programs refused in all five,
seven organ mutations (sign bit, FNV prime, per-tap rounding, norm
floor, float dot, `expf`, `logf`) each caught at a named check. Spec
§1.4 + README + [docs/FIELD.md](docs/FIELD.md) updated. Branch pushed,
**not merged** — awaits Oleg's word.

## 2026-10-15 — a journal reads back by byte offset

Two stateless transport organs read a regular file back. `file_size(path)`
opens read-only with `O_NONBLOCK|O_CLOEXEC`, returns the size `fstat`
reports and refuses FIFOs, directories and missing paths.
`file_read_slice(path, offset, max_bytes)` opens the same way and `pread`s at
most `max_bytes` from `offset`, bounded by the size `fstat` saw, so a file still
being appended yields the prefix present at that moment. An offset at or past
the end and `max_bytes` 0 give `''`.

AML numbers are float32. Sizes, offsets and slice ends are therefore held to
the exact integer range: `file_size` refuses files above 16,777,216 bytes,
`offset` must be an integer in `[0, 16777216]`, and a slice ending past byte
16,777,216 is an error, so a cursor never rounds. `max_bytes` lies in
`[0, 1048576]`, the text limit.

Slices cut bytes, but AML text holds only whole UTF-8 characters, so a cut
character cannot become a value. A character cut by the `max_bytes` edge or by
the current end of file is left for the slice that starts at it: a slice can
be up to three bytes shorter than requested, and the caller advances by
`text_bytes`. The runtime keeps no buffer and joins nothing. An offset inside
a character, invalid UTF-8, NUL, and `max_bytes` smaller than the character at
the offset are errors, so `''` means end of file or a writer stopped
mid-character, never a cursor that cannot advance.

Proof on macOS 26.4.1 arm64 / Apple clang 21.0.0:

- `make clean && make`: zero warnings. `make test`: **550/550**.
- `make test-transport`: **11,985** harness checks (3,294 before on this host)
  across interpreter, resumable host and bytecode. A journal written by
  `file_append_line` is sliced at every offset with every `max_bytes` up to two
  past the end and checked against the longest-whole-character rule. Further
  cases cover empty files, offsets at and past the end, zero `max_bytes`, a
  sparse file on both sides of byte 16,777,216, cuts through two- and four-byte
  characters, offsets inside a character, NUL, invalid and overlong bytes,
  FIFO, directory, missing path and wrong arities. An AML cursor with
  65,537-byte slices reads a 1,201,125-byte file of Hebrew, Cyrillic and emoji
  lines and rebuilds it line by line through `file_append_line`. The copy's
  `file_hash_sha256` equals the source's, and the offset ends at `file_size`.
  A child process appends 80 lines in two writes split inside 🌧 while AML
  follows the file with seven-byte slices. The slices join to the final file
  without an error. In one instrumented run, 290–326 polls per path returned
  `''` while bytes already stood past the offset: the reader met the writer
  between the halves. The test itself asserts only the exact join.
- Interposed `open`, `fstat` and `pread` alongside the existing write/read
  faults: **12,326** checks (3,478 before). Failed open/stat/read refuse with
  the system reason and no leaked descriptor. Three-byte and `EINTR` reads
  still return the exact slice.
- The vector file reads its own bytes back through five-byte slices in all
  five paths. **16** invalid calls (11 before) stop before later AML, C
  `main()` or journal effects.
- Eight mutations, each compiled from a copy of the source, fail their named
  test: an offset at the end raised as an error (`slice_journal_bytes`),
  no NUL guard (`slice_bounds`), no cut-character trim
  (`slice_journal_bytes`), no small-`max_bytes` guard
  (`slice_journal_bytes`), a single `pread` (`slice_faults`), no `close` on
  the non-regular path (`slice_refusals`), no slice-end range check
  (`slice_bounds`), and no `file_size` range check (`slice_bounds`).
- utc-day, expressions, text, text-lower, text-fixed, text-input, lists and
  maps pass. workers, imports and amlc end in `SIGBUS` and records fails to
  build (`mkdtemp`), with logs identical to the run before this change. Linux
  was not run.

```sh
make clean && make
make test
make test-transport
```

## 2026-10-15 — a journal line is whole or absent

A hash-chained append-only journal and engine orchestration need four transport
organs. `text_hash_sha256(text)` and `file_hash_sha256(path)` return lowercase
SHA-256 hex from an in-tree FIPS 180-4 implementation; the file form streams a
regular file through a 64 KiB buffer. `file_append_line(path, text)` refuses
text containing LF before opening the file, writes the line and one LF through
`O_APPEND|O_CREAT` (0644), synchronizes, and returns 1, or 2 when the line is
written but `fsync`/`close` failed. A write failure truncates back to the
length seen before the call, so one writer never leaves a torn line. When the
call creates the file its directory is not synchronized; the guide states it.

`process_run(argv, env?, timeout?)` forks and execs a string list with no
shell. The `PATH` search follows execvp's rule over the child's environment,
prepared before the fork so the child calls only `dup2`, `execve`, `write` and
`_exit`; there is no `/bin/sh` fallback for files without `#!`. A text record
replaces or appends environment names. The child reads `/dev/null`. Both pipes
drain through `poll`, each bounded by the 1 MiB text limit and required to be
UTF-8 without NUL. The result is `{status, stdout, stderr}`: exit code 0–255,
`-N` for signal N, and `-256` once a timeout has sent `SIGTERM`, then `SIGKILL`
after one second. Exec failures arrive through a close-on-exec report pipe as
errors naming the program. Every path reaps its child; descriptor creation and
`fork` share one lock across workers. The names are reserved intrinsics and
consume no function slot: 495 user definitions remain.

Proof on macOS 26.4.1 arm64 / Apple clang 21.0.0:

- `make clean && make`: zero warnings. `make test`: **550/550**.
- `make test-transport`: **3,273** harness checks across interpreter, resumable
  host and bytecode. Text and file digests match NIST and independent `shasum`
  values for 0, 1, 55, 56, 63, 64, 65, 119, 120, 128 and 1,000,000 bytes. Two
  appends give exactly `first\nsecond\n`; LF leaves an existing file
  byte-identical and an absent path absent. Processes cover exit 0/1/3/137,
  signals 9/15, environment override, `a;b c` as one argument, missing/non-
  executable/`#!`-less programs, timeouts at 0.2–0.3 s with `SIGKILL`
  escalation, 1 MiB on both streams and the first refused byte, closed host
  stdin/stdout, and four workers running twenty processes. After every call no
  child remains and no descriptor leaked.
- The same suite under `write`/`read`/`fsync`/`ftruncate` interposition:
  **3,457** checks. fsync failure returns 2 with the complete line, partial
  write + `ENOSPC` restores the previous bytes, a continued partial write
  completes, a failed restore names its torn bytes, seven-byte reads keep the
  million-`a` digest. Interposition is symbol override plus `RTLD_NEXT`
  because Darwin ld has no `--wrap`.
- The vector file passes through interpreter, resumable host, bytecode, runner
  and compiled program from another directory; **11** invalid calls stop all
  five before later AML, C `main()` or journal creation.
- Six mutations each fail their named test, then restore byte-identically: a
  changed round constant (`sha256_vectors`), no `O_APPEND`
  (`append_ordering`), `environ` instead of the built environment
  (`process_environment`), no LF guard (`append_newline_rejected`), no restore
  `ftruncate` (`append_faults`), and no descriptor move above the standard
  streams (`process_closed_stdio`).
- Existing suites keep their exact verdict lines on this host. utc-day,
  expressions, text, text-lower, text-fixed, text-input, lists and maps pass,
  and records passes when its checkpoint harness gets `_DARWIN_C_SOURCE` for
  `mkdtemp`. workers, imports and amlc stop with `SIGBUS` at the same points
  before and after this change. NoTorch suites were not run without a NoTorch
  build.

```sh
make clean && make
make test
make test-transport
```

Quote: “Every line is whole or absent.”
Method: refuse the dirty line before the file opens, restore the length after
a failed write, and reap every child the call creates.

## 2026-10-14 — complete graphs keep their named state

Haiku's recursive-symbol work needs its helper graph, explicit per-owner leaves
and complete publication receipt to travel together. Three shared bounds now
admit that graph: **512 function slots** (17 registered builtins and 495 user
definitions), **1,024 flat record fields**, and **16,384 expanded source lines**.
The `amlc` root-directive budget matches the runtime. Names, argument counts,
call/import depths, per-file limits and individual value bounds retain their
existing contracts. Overflow still fails explicitly; preparation errors precede
all statements and compiled C `main()` effects.

The function audit found one `AML_Functab` used by imported definitions, typed
lookup, saved bytecode and worker snapshots. All indices/counts are `int`;
scalar generated executables prepare through the same runtime. The separate
256 `BLOOD COMPILE` block budget counts C blocks. A compile-time assertion also
keeps the registered native table within the shared capacity.

Measured on Linux x86_64: `AML_Func` remains 560 bytes; `AML_Functab` grows from
143,364 to 286,724 bytes and `AML_ExecCtx` from 561,912 to 705,272 bytes. Rebuild
C hosts with the updated public header. Resumable/worker contexts live on the
heap; ordinary and bytecode execution/preparation retain their stack context.
The 16-worker table increase totals 2,293,760 bytes. Source reservation grows
from 4,259,840 to 8,519,680 heap bytes; inherited workers copy their actual line
count. Bytecode uses 4,140 bytes per actual line, at most 67,829,760 bytes. The
compiler's directive table grows from 4 to 8 MiB in static storage.

`AM_Record` remains opaque with the same layout. Its entries grow on demand;
raising the bound adds no storage to existing records of at most 512 fields.
An entry is 104 bytes, so a 512-to-1,024 reservation adds 53,248 bytes plus the
owned leaf payloads. Clone/replace/snapshot still copy mutable leaves. The
version-1 checkpoint header already carries a 32-bit count: wire encoding,
checksums, 64 MiB file bound and aggregate item limit remain unchanged. Older
runtimes retain their own admitted field counts.

Proof on Linux x86_64 / GCC 13.3, scalar runtime and compiler:

- `test_aml_imports.sh --line-capacity`: **16,439** focused C checks against
  the existing build. The final worker changes the prefix's `0.875` to `0.625`;
  scalar C `main()` checks that field before its success marker. A control with
  the final four lines removed must fail that check. Exact 16,384-line programs
  execute through all five paths; runtime and frontend reject line 16,385.
  The full import graph and **208** opened-file checks also passed. Imported helper
  495 remains callable through a captured local and worker, including after
  source removal from prepared and repeated bytecode programs. Definition 496
  reports its imported source and runs no preceding AML or C-main effect.
- `test_aml_text_fixed.sh`: **2,893** runtime checks. Every helper is called in
  112/239/240/495-definition programs; all five paths refuse definition 496.
  Direct C-owned workers admit 495 and refuse 496 before effects. The existing
  fixed-text value, allocation and 26-invalid-program gates also pass.
- `test_aml_records.sh`: **3,900** API checks (**5,809** with allocation
  injection), **967** runtime checks and 62 rejected calls through five paths.
  Exact 1,024-field clones and worker returns retain independent mutable leaves
  and publish through `record_swap`; field 1,025 preserves the complete owner.
  Checkpoints pass **11,082** checks (**13,386** allocation and **11,270** I/O).
  Independent encoders verify complete 512- and 1,024-field bytes, and a
  recomputed-checksum 1,025-field header is refused.
- A 512-field file written by the previous runtime and the rebuilt runtime
  is byte-identical: SHA-256
  `37a6211cebe44f2fc7981564d399b49440af06098a7655f126e1fc2d58f09d02`.
- `test_aml_utc_day.sh`: **2,141** real-clock and **4,259** wrapped-clock
  checks, retaining all 495 user slots. `make test`: **550/550**. The existing
  include-depth/cycle gate passes with the larger stack context.

The language spec and focused module/record guides state the bounds and ABI
costs; the README receives only its current record-limit correction. Main was
refreshed at `b05e845` before implementation. No new engine operation is added.

Quote: “Keep the whole graph, and keep its edge visible.”
Method: measure the owned storage, exercise the last admitted item, refuse the
next item before publication, and compare the existing bytes.

## 2026-10-14 — an imported source keeps its opened identity

The three open CodeQL findings on `main@f434a8a` were inspected directly:
[#20](https://github.com/ariannamethod/ariannamethod.ai/security/code-scanning/20)
reported the IMPORT `stat`/`fopen` race;
[#22](https://github.com/ariannamethod/ariannamethod.ai/security/code-scanning/22)
and [#23](https://github.com/ariannamethod/ariannamethod.ai/security/code-scanning/23)
reported unchecked `snprintf` cursor advancement in the worker test fixture.

IMPORT now opens once with `O_NONBLOCK`, validates regular-file type and the
1 MiB bound through `fstat`, and reads through `fdopen` on that same descriptor.
A pathname replacement after the check cannot replace the reader. FIFO sources
are refused without waiting, and every refusal releases the descriptor.
Canonical deduplication, module origins and the exact-length/NUL checks remain.
The worker fixture checks capacity before pointer arithmetic and checks both
negative and truncated formatting results before advancing its cursor.

Proof on Linux x86_64 / GCC 13.3:

- A deterministic equal-size path replacement makes the old IMPORT execute
  the replacement's value. The fixed loader retains the opened value. The new
  **208-check** gate covers regular/FIFO replacement, FIFO/directory/oversize/
  metadata refusals and descriptor release across three C execution paths.
- `make test-imports`: **9,230** existing checks plus the **208** new checks;
  exact 8,192-line capacity and interpreter/compiled source-origin gates pass.
- `bash tests/test_aml_workers.sh`: **1,998** normal and **2,018** allocation
  checks, five execution paths and 80 repeated typed jobs pass.
- A 64-byte worker-fixture control reproduces the original stack overflow
  under AddressSanitizer. The fixed helper refuses truncation with no ASan or
  UBSan finding. Independent review reproduced the 208-check gate with
  `-Werror`; `git diff --check` is clean.

Quote: “The source stays with the reader.”
Method: validate the object being used, and advance only by bytes actually held.

## 2026-10-14 — a day belongs to its experience (v5.13.0)

`utc_day()` gives an organism an explicit UTC civil ordinal to capture with a
foreground request. One `time(NULL)` call becomes `floor(POSIX_seconds / 86400)`;
day zero is 1970-01-01 and the boundary is midnight UTC. Seconds stay at double
precision until the bounded, exactly representable float32 day is published.
The inclusive domain is `[-16777216, 16777216]`. Clock failure and exceeded
range stop execution with named diagnostics while preserving the destination.
The host's `-1` time sentinel remains a failure; other signed pre-epoch seconds
use floor. No global field, calendar control, provider registry or public C API
changes. Existing Janus field calculations keep their noon-UTC epoch.

Expression dispatch reserves the name case-insensitively without taking a
function slot. Preparation reads no clock; execution samples the current day.
Ordinary scalar/record ownership carries a captured day through workers and
checkpoints. The organism decides when to capture, replay and publish it.

Proof on Linux x86_64 / GCC 13.3:

- `make test-utc-day`: **1,117** real-clock C/runtime checks and **3,235** with
  linker-wrapped `time`. Frozen fixtures cover midnight, 29 February, the Janus
  noon, negative days, both exact-domain endpoints and refused neighbours.
- Three timezone settings give the same ordinals. Whole-`AM_State` comparison,
  unchanged birth/key/epoch controls, one-read counters, preparation/reexecution
  boundaries, detached worker receipts and all **239** user functions pass.
- Interpreter, resumable host, bytecode, runner and scalar compiled CLI agree.
  Five invalid call/declaration cases, clock failure and out-of-domain values
  stop before later AML and C-main effects. Clock substitution lives in test
  linker fixtures only; the production runtime exposes no clock override.
- `make test`: **550/550**. `make test-expressions`: **1,201** checks and **48**
  rejected programs in five paths. `make test-imports`: **9,230** checks,
  including exact **8,192** lines and overflow. The **512**-field record limit
  and checkpoint encoding remain unchanged.

README, `spec/AML_SPEC.md` and `docs/UTC_DAY.md` define the epoch, errors and
capture contract. The spec's directive-lowering section now states the existing
8,192-line frontend/runtime bounds. Main was refreshed at `745fdb6` before commit.

## 2026-10-14 — immutable bodies enter the language (v5.12.0)

Haiku's frozen PEN7 sides now enter AML through an owned immutable model value.
`model_try_load` returns explicit readiness, load outcome and SHA-256 identities;
empty configuration, missing/corrupt files and absent backends stay quiet.
Assignments, typed calls, nested returns, persistent globals, worker snapshots
and `AWAIT ... INTO` retain atomic owners. Every value keeps its copied backend
callbacks through registry replacement or removal. No model handle is a flat
record/checkpoint leaf, and numerical/tape operations reject the new type.

The separate `AM_ModelBackend` table keeps existing numerical callbacks intact.
The NoTorch bridge calls the public `pen7.h` API from the ordinary archive:
canonical bounded BPE, exact loaded-byte/tokenizer identities, token surfaces,
fresh final logits and the final last-token contextual representation. Each
forward owns its bounded workspace. `model_mix_forward` mixes all parameters
with explicit alpha and exact endpoints while both source bodies stay frozen.
The detached success record contains `ok`, `error`, `logits`, `hidden`; a refused
forward carries `ok = 0` and a diagnostic with neither partial array. Experience,
proposal selection, learning and publication remain the organism's AML policy.

The complete Haiku program also reached three concrete language bounds. Shared
programs and the compiler frontend now admit 8,192 source lines; typed functions
admit sixteen arguments; flat records admit 512 fields. The larger record carries
a completed organism plus its exact pre-work receipt in a single worker result.
Checkpoint version-1 bytes are unchanged: old golden files still round-trip,
and the new bound admits larger flat files. Public typed structs have grown;
C hosts must rebuild with the current header.

Proof on Linux x86_64 / GCC 13.3, scalar AML and NoTorch:

- `make test-model`: **726** C host/runtime checks; **1,580** with allocation
  failure injection. Loaded mock native bodies are destroyed exactly once.
  Each host mode completes 24 sequential model-backed worker returns after
  transfer of a model whose launching owner was rebound.
- The canonical tiny F32 fixture passes **149** direct bridge checks, including
  byte-exact IDs/logits/hidden vectors against NoTorch and four concurrent
  callers with separate workspaces. The actual Haiku1 Q8 body passes **147**
  direct parity/concurrency checks through the same gate.
- Interpreter, resumable host, bytecode, runner and scalar compiled execution
  cover zero/one/two available sources, relative imported model paths, ranked
  BPE, exact mixture endpoints, sixteen-argument inherited helpers and owned
  worker transfers. **14** invalid calls fail in all five paths before later
  AML or C-main effects. Standalone/incomplete-prefix unavailable cases emit
  only the fixture's expected output.
- ASan and UBSan pass the **726**-check owner/runtime gate. LeakSanitizer cannot
  enumerate threads under this container's ptrace environment; the separate
  allocation tracker covers failed allocation publication and release.
- `make test-records`: **2,871** API checks (**4,780** with allocation injection),
  **965** runtime checks and the existing **62** invalid calls in five paths.
  Exact 512-field records survive worker transfer; field 513 is refused without
  changing the first 512. Checkpoint gates pass **2,865**, **5,169** allocation,
  and **3,047** I/O-failure checks, including an exact 512-field round-trip.
- `make test-imports`: **9,230** checks. Exact 8,192-line roots execute in all
  five paths; an expanded overflow fails before effects. The compiler frontend
  rejects root directive 8,193. Sixteen parameters succeed and seventeen fail.
- `make test`, `make test-workers`, `make test-tokenizer`, `make test-sampling`
  and `make test-text-fixed` pass. Baseline remains **550/550**; owned workers
  remain **1,998/2,018** checks. The 239-user-function worker gate still passes.

`docs/MODELS.md`, the README, language spec and ownership/capacity documents
record the API and its bounds. The canonical link order stays
`libaml_notorch.a libaml.a libnotorch.a`; no separate harness library is needed.
Main was refreshed at `5d5f3f7` before this change was committed.

## 2026-10-14 — INTO remains a worker name (v5.11.0)

PR #37 review found that the previous whole-statement `INTO` scan rejected
legal worker names. AWAIT now has explicit positional precedence: exactly
three arguments with case-insensitive `INTO` in the middle transfer one result;
the worker or destination may also be called `INTO`. Every other nonempty form
is a discard list. A list's names must be valid, distinct and pending under
the awaiting owner. The runtime validates the entire list under its mutex,
then reserves all its slots together before joining. A failed preflight leaves
every result untouched. After successful validation, all reserved jobs are
joined even when one fails, preserving the first execution diagnostic.

The unchanged parser failed the new fixture on `AWAIT INTO`. With this fix,
the focused `make test-workers` gate passes **1,998** runtime/C-host checks and
**2,018** with allocation/thread-creation failure injection. The source fixture
passes interpreter, resumable host, bytecode, CLI and compiled scalar paths.
It covers mixed-case worker names and markers, result variables named `INTO`,
discard-list positions and explicit three-token capture precedence.

Fourteen rejected statements preserve all 16 pending slots and both destination
values before program close, including the original misplaced-marker case,
missing and foreign names, duplicates, invalid names/destinations and an
oversized list. A separate execution-failure gate confirms that every reserved
slot is reusable before error teardown and that closing the failed program
preserves all 16 subsequently launched C-owned jobs. A complete 16-name discard
list also succeeds through all three host modes. The tests were built in a
separate staging directory; shared libraries and runners used by Haiku were
left unchanged. `git diff --check` passes; main was checked at `70217ff`.

## 2026-10-14 — work returns to its owner (v5.11.0)

Haiku's completed experience can now travel across an owned worker boundary.
`SPAWN` copies the prepared function table and source lines with their origins,
globals and the current visible local frame. Captured locals remain a separate
scope: a worker block sees its launching argument, while an inherited helper
still resolves its own arguments and original globals. Mutable values receive
independent owners; immutable text and tokenizer models retain references.

The worker block can `return` any AML value. `AWAIT name INTO completed`
transfers that value after joining; aliases into worker-owned records survive
context and persistent-store teardown. A failed execution or missing return
leaves the destination unchanged. Joined slots are reusable, so the 16-slot
bound applies to outstanding jobs throughout a conversation. Each program owns
its child set and drains it at normal completion, error completion or explicit
host close. A child drains its own children before publishing its result.

Channels now synchronize creation and closure alongside reads/writes. Closing
a queue wakes blocked transfers; generation checks keep those waiters away
from a replacement queue reusing the slot. Missing queues, malformed commands,
capacity errors and timed-out blocking transfers become explicit AML errors.
An empty live `CHANNEL TRY` preserves its existing nonblocking behavior.
Full initialization closes channels and joins workers before resetting field
and tape storage. The process field and tape retain their shared ownership;
Haiku carries private organ state in records and caller-owned numerical values.

Three canonical NoTorch operations give AML organs their parliament arithmetic:
`nt_parliament_elect`, `nt_parliament_inject` and `nt_oja`. They return fresh
arrays with validated layouts, finite values and election domains. The rank
cursor and Oja noise are explicit inputs. Experts, immune memory, receipts,
learning policy and publication remain application-owned AML state. The
separate `AM_ParliamentBackend` registration table preserves the original
seven-callback `AM_NumericalBackend` layout. `am_use_notorch()` registers both,
together with sampling and immutable tokenizers.

Proof on Linux x86_64 / GCC 13.3:

- `make test-workers`: **1,319** runtime/C-host checks and **1,339** with
  injected launch allocation and `pthread_create` failures. Parent values
  survive refused launches, and the next job can reuse the slot.
- The worker fixture completes **80** sequential typed jobs through interpreter,
  prepared stepping, bytecode, CLI and compiled scalar paths. Further fixtures
  cover nested same-named workers, a departed launching frame, helper/global
  scope, imported source origins, UTF-8, signed zero, matrix shape, maps and
  returned record leaves.
- Direct host gates cover **96** repeated jobs, **16** simultaneous pending
  slots, four C host threads, separate resumable-program owners, early close,
  destination preservation, tokenizer destruction exactly once, and channel
  close/reuse with blocked readers and writers.
- Final worker runtime and source fixtures pass ASan/UBSan. Leak scanning is
  disabled because this host refuses process-task inspection. The
  `AM_ASYNC_DISABLED` core also compiles.
- `make test-parliament`: **656** checks compare every returned value with
  canonical NoTorch calls through interpreter, bytecode and resumable hosts.
  Missing and malformed backend callbacks, invalid election gate mass,
  dimensions and failed assignment publication are exercised; runner and
  compiled scalar fixtures agree.
  An old-header host registers its original 56-byte numerical table directly
  before a guard page without an overread; its source still compiles with
  `-Wall -Wextra -Werror`.
- Existing **550/550** baseline, **1,201** expression checks with **48** rejected
  programs, **1,017** import checks and compiler suites pass. The expression
  fixture now explicitly closes its queue before its next host-mode run;
  duplicate live-channel creation reports an error.

The transpiler and runners use their existing shared execution path, carrying
the same worker ownership and registered NoTorch operations into compiled
programs. See [workers](docs/WORKERS.md) and
[parliament values](docs/PARLIAMENT_VALUES.md) for the executable contracts.

### PR #36 follow-up — validate the whole AWAIT before joining

The review found that `AWAIT first second INTO kept` treated its tokens as a
plain worker list and consumed the first two results before reporting an error.
The parser now recognizes every case-insensitive `INTO` token and requires
exactly `name INTO variable`, with one `INTO`, before attempting any join.
Misplaced or repeated markers preserve pending results and the destination.
The program's later error teardown still drains its owned children.

The regression first failed on the old parser: after the malformed statement,
a new launch unexpectedly succeeded because earlier results had been consumed.
Eight malformed forms now keep all 16 outstanding slots occupied until explicit
program close, without timing assumptions. Closing then frees the program's
slots while C-owned jobs remain retrievable. Interpreter, resumable and bytecode
hosts also reject all eight forms with the syntax diagnostic and unchanged
destinations, including a variable named `INTO`.

`make test-workers` passes **1,679** runtime/C-host checks and **1,699** with
allocation/thread-creation failure injection, plus the existing worker fixtures
through all five execution paths. `git diff --check` passes. This follow-up
changes only AWAIT syntax validation and its targeted regression coverage.

### Completion identity — exact flat-record equality

`record_equal(left, right)` and C `am_record_equal` compare the complete ordered
record value without allocation: field names, leaf types, text bytes, array
lengths and shapes, list order, ordered map keys and numeric contents. Every
float compares by its binary32 bits. Signed zeros, one-ULP differences and NaN
payloads stay distinct; identical NaN representations compare equal. Capacity,
reference counts and pointer identity do not affect equality. The comparison
preserves logical values and synchronizes CUDA CPU mirrors when needed.

Haiku can use this generic boundary to bind a worker completion to its full
prepared experience. It requires neither a rounded decimal digest nor writing
a checkpoint to compare values. Schema and publication remain application code.
C returns 1/0 and rejects NULL with -1; AML rejects wrong types and arity before
replacing a destination. The intrinsic is case-insensitive and reserved.

The old runner rejected the first equality fixture before this primitive was
added. `make test-records` now passes **2,358** C API checks, **4,267** with
allocation tracking, and **953** runtime checks. Numeric negative controls
compare all pairs of 13 scalar/array patterns (including positive/negative zero,
subnormals, infinities and quiet/signaling NaN payloads), plus six finite map
patterns. Further controls cover field/type/key changes, shapes, all container
orders, UTF-8 bytes and different reserve capacities. C calls and an AML call
with bound arguments leave the allocation budget untouched.

The expanded source fixture passes through interpreter, resumable, bytecode,
CLI and compiled scalar paths. All **62** invalid record calls, including nine
new equality argument failures, stop before later AML/C effects in all five
paths and preserve failed-assignment destinations in the persistent hosts.
Existing checkpoint ownership, wire, allocation and I/O gates in this targeted
suite also pass. `git diff --check` passes; no whole-language rerun was needed.

## 2026-10-13 — memory names its states (v5.10.0)

Haiku's Phase4 transition memory names climate states with fixed decimal
coordinates. `text_fixed(value, digits)` and C `am_string_fixed(float, int)`
format a finite float32 at precision 0–9. The sign survives negative zero and
rounding to zero. Digits use nearest rounding with ties to even, a literal
decimal point, fixed fractional width and no exponent notation.

The formatter decodes binary32 and scales its significand with exact integer
arithmetic. A 45-bit product handles every rounding decision; five 32-bit
limbs hold the largest scaled magnitude. Locale and floating-point rounding
mode never enter the conversion, and exception flags remain unchanged. Two
checked allocations publish one immutable text owner. Invalid arguments and
allocation refusal preserve a failed assignment's previous value.

The growing organism also crossed the old 111-user-function capacity. An
actual 112-helper program failed during preparation at the 112th definition,
before its leading PRINT. The shared table now has 256 slots, including the
17 builtins: all 239 user helpers remain callable. On Linux x86_64 this adds
38,912 bytes per function table; `AML_ExecCtx` occupies 461,544 bytes. Prepared
programs retain heap contexts and ordinary/bytecode calls retain stack contexts.
The measured integrated Haiku program fits the unchanged 4,096-line budget.

Proof on Linux x86_64 / GCC 13.3:

- `make test-text-fixed`: **44,717** direct API checks, **54,960** checks with
  allocation refusal, **1,391** runtime checks. The **620** frozen Python
  fixtures cover subnormal/normal boundaries, signed zeros, neighbouring
  decimal thresholds, half ties, integer carry and both finite extrema.
- All four standard floating-point rounding modes preserve their mode and
  preexisting exception flags. A privately generated German comma-decimal
  locale still produces the same dot-decimal output.
- Interpreter, prepared stepping, bytecode, runner and compiled scalar paths
  agree on every fixture byte. **26** invalid programs fail before later AML
  effects or C main, preserving previous destinations and aliases.
- Direct refusal of either string allocation leaks no owner. Persistent
  execution sweeps refuse **6/7/8** budgets in interpreter/prepared/bytecode
  paths before complete success, with zero tracked survivors after cleanup.
- All five execution paths call every helper in **112**- and **239**-definition
  programs; **240** definitions fail before effects. A 239-helper worker and
  immutable text snapshots pass.
- `tools/verify_text_fixed.py` compares **107,970** values directly with Python
  formatting of exactly promoted binary32 numbers: every finite exponent,
  both signs, all ten precisions and deterministic random bits. Frozen fixture
  regeneration is byte-identical.
- API and runtime gates pass ASan/UBSan, including the enlarged worker
  context. Leak detection is disabled on this host; allocation-wrapped gates
  independently prove zero tracked survivors. Existing **550/550** baseline,
  **1,017** import checks, text ownership/worker gates and compiler suites pass.

See [fixed decimal text](docs/TEXT_FIXED.md) for the complete contract and
oracle commands. The primitive is standalone core code; no dependency is
added to the language or to Haiku.

## 2026-10-08 — words keep their order (v5.9.0)

Haiku's word-cloud rings need lexical traversal independent of insertion order.
`list_sorted(xs)` and C `am_list_sorted(xs)` return an independent list in
ascending UTF-8 byte order. An iterative stable merge preserves empty strings,
duplicates and the order of distinct owners with identical bytes. The copied
container retains immutable strings; its input remains unchanged on success
and at every refused allocation. The primitive occupies no function-table slot.

The continuity fixture exposed a scalar parser accepting only a valid prefix:
with `cursor = 1`, `((cursor * 5 + 3) % 16) / 16` printed 8 and continued
successfully. Scalar evaluation now requires a complete expression and closing
parentheses/index brackets. The array-call parser checks its whole span and
argument separators before dispatch. `#` outside quoted strings and the final
`if`/`while` colon remain statement boundaries. The command audit also found
bytecode `TAPE APPLY_ACCUM` retaining only the first whitespace-delimited token;
it now evaluates the same whole argument as the interpreter. `%` remains
unsupported; the explicit `floor` remainder spelling produces 0.5.

Proof on Linux x86_64 / GCC 13.3:

- `make test-expressions`: **1,200** checks and **48** rejected programs through
  interpreter, prepared stepping, bytecode, runner and compiled scalar paths.
  Missing delimiters, unsupported operators and trailing tokens preserve the
  destination scalar/array/field and stop before later AML statements or C main.
  Valid nested calls execute once; quoted punctuation, inline comments,
  conditional colons and both whole/separate command arguments retain parity.
- `make test-lists`: **612** direct C checks, **1,606** allocation-wrapped checks,
  **3,133** runtime checks and **72** invalid programs through all five paths.
  Fixed Unicode/control/prefix order and an independent rank oracle check
  stable duplicate owners and short merge boundaries. The full **65,536** item
  limit passes. Container, item-buffer and scratch refusal preserve the source
  and release unpublished owners. Persistent sort sweeps cover **24/25/26**
  refused budgets across interpreter, prepared and bytecode paths.
- ASan/UBSan pass the expression and list API gates; leak detection is disabled
  because this host prevents process-task inspection. Allocation-wrapped gates
  separately verify zero tracked survivors.
- The expression-boundary audit also passes the existing **550/550** baseline,
  compiler, imports, text, lowercase, input, lists, maps, records, sampling,
  numerical and tokenizer suites.

## 2026-10-08 — experience survives the process (v5.8.0)

Haiku can now carry its explicit owners together across process lifetimes.
Flat typed records hold float, array/matrix, UTF-8 string, string-list and
numeric-map leaves. Set, assignment, persistent storage and worker snapshots
copy mutable leaves; direct typed getters and function parameters retain owners
for organ calls. `record_kind` exposes exact field kinds for AML schema checks.
`record_replace` stages every leaf before publication. `record_swap` exchanges
already owned contents without allocation, so a saved pending turn can become
live after the file commits without a second allocation boundary.

Portable version-1 checkpoints preserve exact float32 bits, matrix shapes and
all insertion order. A fixed little-endian header and CRC32 cover a bounded
64 MiB file, 256 fields and 1,048,576 aggregate text/key objects. Loading checks
the complete structure into detached owners. Unknown versions/tags, duplicate
keys, invalid text/shapes, nonfinite map values, corruption, truncation and
trailing bytes fail explicitly. Application schema, RNG representation, model
identity and cross-owner checks remain AML code before publication.

Saving validates and encodes before creating a unique mode-0600 temporary,
writes/synchronizes/closes it, then renames it in the destination directory.
Precommit failure preserves the old file and cleans up the temporary. Status 1
means file and directory durability; status 2 means rename committed but the
directory sync/close failed. `file_exists` distinguishes absence from errors.
All intrinsic file paths retain imported and compiled source origins.

`tokenizer_identity` copies a checked SHA-256 string from an optional callback
on the immutable loaded owner. The canonical bridge uses the exact model bytes
consumed by NoTorch's loader, introduced in [NoTorch #165](https://github.com/ariannamethod/notorch/pull/165),
commit `9380e86017d466f456ae54514aa841f00c26ea9e`. Existing three-callback source
initializers remain valid, with identity support absent until supplied.

A final C-boundary review also caught an invalid text object's cached length
of -1 comparing equal to the UTF-8 validator's failure code. Records now require
a nonnegative cached length; both malformed field keys and string leaves reject.

Proof on Linux x86_64 / GCC 13.3:

- `make test-records`: **979** record API checks, **2062** with allocation
  refusal, **824** runtime checks and **53** invalid calls. Checkpoints pass
  **1835** ordinary, **4139** allocation and **2015** injected I/O checks.
  An independent golden encoder agrees byte for byte; every truncation and
  single-bit mutation rejects, along with **24** structural corruptions whose
  checksums were recomputed. Failure sweeps preserve previous owners/files and
  leave no tracked allocations. Direct C and prepared-AML swaps work with
  allocation refused. All five execution paths agree on records, persisted
  leaves, source-relative imported functions and foreign working directories.
- `make test-tokenizer`: **908** mock API and **1050** allocation checks,
  **70** native checks, five execution paths and **18** rejected fixtures.
  Identity covers absent callbacks, malformed output, copied callback lifetime,
  allocation refusal and the independently pinned 97-byte model's SHA-256.
- ASan/UBSan over instrumented core record/checkpoint gates pass; leak detection
  is disabled because this environment prevents process-task inspection.
  Dedicated allocation-refusal gates check zero tracked survivors separately.
- Existing **550/550** baseline tests and compiler, imports, text, lowercase,
  input, lists, maps, sampling and numerical suites pass.

The continuity fixture exposed a follow-up: with `cursor = 1`, the unsupported
expression `value = ((cursor * 5 + 3) % 16) / 16` produced 8 and exited
successfully. The fixture spells the remainder using `floor`; the separate
syntax-context audit and complete-consumption repair are recorded in v5.9.0.

See [records and checkpoints](docs/RECORDS.md) for the precise AML/C ownership,
wire format and commit contract, and [tokenizers](docs/TOKENIZER.md) for identity.

## 2026-10-08 — words reach the organism (v5.7.0)

Haiku's hearing can now carry its original Unigram model into AML. The optional
NoTorch bridge registers an independent tokenizer backend beside sampling and
numerical values. `tokenizer_load` resolves a model from its statement's source
directory; `tokenizer_pieces` returns ordinary ordered UTF-8 strings, including
normalized unknown surfaces. NoTorch owns model parsing, normalization and
deterministic segmentation. AML owns the model value, output list and publication.

Each immutable tokenizer copies its three backend callbacks and carries atomic
references through assignment, functions, returns, persistent globals and worker
snapshots. Replacing registration affects future loads; existing owners retain
their original model and callbacks. Output is checked before publication, with
65,536 pieces and 1 MiB of aggregate UTF-8 as its bounds. Wrong types, bad model
files, rejected backend output and allocation failures produce explicit errors.

`read_line` brings live standard input into AML as `[text]`, preserving blank
lines and CR while distinguishing `[]` at EOF. Its prompt flush occurs before
waiting for input; concurrent readers lock the complete line. UTF-8, NUL,
byte limits and I/O failures are checked before returning a value.
`codepoint_isalnum` supplies Unicode 15 Letter/Number membership: 137,935 scalar
members in 747 immutable ranges, verified independently against Python 3.12.

The integration gate also exposed standalone `len(model)` being silently
ignored while the assigned expression correctly failed. The statement
recognizer now routes `len`, `sum`, `rows`, `cols` and `dot` through their typed
expression path. Fresh CLI startup no longer prints the pipe-close summary
when there were no active pipes; closing an active pipe keeps its diagnostic.

Proof on Linux x86_64 / GCC 13.3:

- `make test-tokenizer`: **823** mock API/runtime checks and **928** checks
  with allocation refusal. All **13** ordinary and **15** allocation-test model
  lifetimes release exactly once; tracked allocation sweeps leave zero blocks.
  The **97-byte** SentencePiece 0.2.2 fixture supplies **12** native vectors and
  **1,000** concurrent encodings (**69** native checks). Piece output, equal-score
  order, unknown surfaces, copied list mutation, backend registration and
  imported-relative model loading agree through all five execution paths.
  Fifteen malformed call/model fixtures stop before continued AML or C-main
  effects. A standalone runner and an incomplete prefix report missing backend.
- `make test-text-input`: **2,228,843** API/runtime checks and **2,229,147**
  allocation-wrapped checks, including all **1,114,112** Unicode positions.
  Exact and exceeded limits, blank/final/CR lines, whole-line concurrent reads,
  EOF and stream failures pass. Eleven malformed calls and four invalid streams
  stop through all five paths. Runner and compiled pipe tests receive the prompt
  before supplying input. Fresh runner and compiled output stay free of startup
  diagnostics.
- ASan/UBSan pass the new tokenizer API and input gates with leak detection
  disabled; the environment prevents LeakSanitizer's process-task inspection.
  Dedicated allocation refusal gates independently verify zero tracked survivors.
- Existing **550/550** baseline runtime tests, compiler, imports, text,
  lowercase, lists, maps, sampling and numerical suites pass.

See [tokenizers](docs/TOKENIZER.md) and [text input](docs/TEXT_INPUT.md) for
the AML/C contracts, fixture provenance and reproducible gates.

## 2026-10-07 — experience reaches the weights (v5.6.0)

Haiku's MathBrain and RAE now compose their learning in AML over seven
stateless NoTorch value operations. An independent copied numerical callback
table supplies linear layers and their reverse derivatives, tanh and its
reverse derivative, mean squared error with gradient, functional SGD, and
owned standard normals. The runtime checks inputs and dimensions, allocates
unpublished outputs, and exposes a result only after backend success and
complete finite checks. Normal draws publish the RNG map last. Both global
tapes keep their existing state.
The canonical kernels are [NoTorch PR #162](https://github.com/ariannamethod/notorch/pull/162),
published as `8c39e3e6d47e11bad30d3f64f45c1a863929a2a2`.

The optional bridge exports `am_use_notorch()` for sampling and numerical
tables together. Runner and compiler use it, while the old sampling-only
entry point and generated-C macro remain available. `isfinite` adds a typed
scalar decision for skipped observations. Ordinary arrays and maps carry model
and random state through functions, host snapshots, and workers.

`text_lower` implements Unicode 15 default lowercase for RAE's word features.
The two-file core embeds 7,648 bytes of generated mapping/property tables.
U+0130 expands; Greek sigma reads the original Cased/Case_Ignorable context.
Two passes enforce the output cap before allocation and write a fresh string.
The frozen direct-Python oracle covers **5,560,315** scalar/context cases,
62 curated strings, four long ignorable runs, and 200 random context strings.

Review also reproduced signed product overflow in `am_set_var_matrix` and
`am_matrix_new` with dimensions **65,536 × 65,537**. Division bounds now precede
multiplication. The host rejects before reading its input; the AML constructor
retains its established failed-construction result. UBSan catches both old
paths and passes the repaired paths.

Proof on Linux x86_64 / GCC 13.3:

- `make test-numerical`: **990** callback/API checks, **1,057** allocation
  checks, and **3,880** runtime checks. The **620** independent scalar/Python
  assertions pass through all five execution paths. Seventy-four malformed
  calls and four overflow cases preserve state in each host mode; fourteen
  runner/compiler cases stop before later AML or C-main effects.
  ASan/UBSan pass the API and runtime gates with leak detection disabled;
  this environment prevents LeakSanitizer's process-task inspection. The
  allocation gate verifies zero retained blocks at both refused output sites.
- `make test-text-lower`: **783** API, **1,203** allocation, and **220** runtime
  checks, with ten invalid programs across all five paths and zero tracked
  retained blocks. The direct Python oracle agrees byte for byte; its table
  regeneration agrees with the embedded data. ASan/UBSan pass the API suite.
- Existing **550/550** runtime tests, compiler suites, imports, text, lists,
  maps, and sampling all pass. Haiku's prior **8,096** reference results and
  **22** scripted generator cases pass in interpreted and compiled scalar
  execution, including the previous rejection/state-preservation gates.

See [numerical values](docs/NUMERICAL_VALUES.md) and
[Unicode lowercase](docs/TEXT_LOWER.md) for contracts and reproducible gates.

## 2026-10-07 — each voice keeps its own chance (v5.5.0)

Haiku's Markov generator needs weighted draws whose state belongs to the voice.
Five typed intrinsics expose owned PCG32 state, uniform and unbiased bounded
draws, positive-temperature categorical sampling, and explicit-draw replay.
The core validates types/state and publishes new limbs only after success.
Numerical work runs in canonical NoTorch through an optional copied callback
table and `libaml_notorch.a`. The two-file AML core remains standalone.

RNG maps contain `algorithm=1` and four exact 16-bit limbs. Assignment,
parenthesized expressions, returns, persistent host state, and worker snapshots
retain independent containers; parameters share deliberate mutation. Existing
`randn` and tensor streams keep their behavior. The optional runner registers
NoTorch before execution; `amlc` does the same when all three archives exist.
Missing/incomplete backends fail explicitly. Shared-program limits grow to
128 function slots and 4,096 lines; the compiler's 4,096-directive table moves
off the stack.

Proof on Linux x86_64 / GCC 13.3:

- `make test-sampling`: **927** mock-backend API checks, **956** allocation
  checks covering all **13** RNG construction sites, and **2,199** real-NoTorch
  runtime checks. Every rejected construction releases its tracked allocations.
- Independent integer reference vectors agree through interpreter, resumable,
  bytecode, runner, and compiled scalar execution. **51** invalid argument/state
  cases preserve RNG state in all three host execution modes; eight more
  runner/compiler cases stop before later AML or C-main effects.
- Host save/restore, copied continuations, parent/worker snapshots, and
  interleaved libc/NoTorch tensor randomness retain the expected next draw.
  Compiler gates accept a 602-directive root and reject 4,097 directives.
- ASan/UBSan pass the **927** API checks and existing **3,727** map runtime
  checks, including **12 workers / 120,000** mutation cycles. Leak detection
  is disabled because the environment prevents process-task inspection;
  the explicit allocation gate checks retained blocks.
- Existing **550/550** runtime tests, three compiler suites, and import, text,
  list, and map gates pass. Haiku's existing organs plus new English form pass
  **8,096 reference results** through interpreted and compiled scalar paths.

See [sampling](docs/SAMPLING.md) for the build, API, state representation, and
reference reproduction. Rebuild hosts against the expanded runtime header.

## 2026-10-07 — bound composite-key writes after CodeQL review

PR #29's CodeQL review identified `snprintf` return values feeding the next
remaining-capacity calculation in `list_key`. Every formatted prefix now
checks negative/truncated results before advancing the offset. Raw token
copies retain room for the terminator, and the final offset must match the
precomputed length. Failure frees the incomplete string. The canonical
encoding and public API are unchanged; `make test-maps` passes the same
API, allocation-fault, runtime, and five-path gates.

## 2026-10-07 — words carry their own weight (v5.4.0)

Haiku's cloud needs growing word weights and transition counts. `AM_Map`
provides an ordered UTF-8-string-to-finite-float collection with hashed lookup,
geometric growth, and a 65,536-entry bound. Eight `map_*` intrinsics expose
construction, length, membership, lookup, set, delete, keys, and clone.
Replacement preserves order; deletion preserves the remaining order;
reinsertion appends. Missing lookup fails explicitly. `PRINT` emits an ordered
JSON object with escaped text keys and round-trippable float32 values.

Map ownership follows string lists: assignment copies a container, parameters
retain shared mutation, returns carry ownership, and persistent/worker
snapshots copy containers. Host setters prepare replacement before publication.
Growth allocates both entry storage and its hash index before replacing either;
allocation failure preserves the previous map. Delete rebuilds the existing
index without allocating. Typed scalar, array, text, list, TAPE, and bytecode
paths reject maps where they do not belong.

`list_key` encodes the item count and each UTF-8 byte length before its exact
content. Empty words, colons, controls, and multibyte tokens keep their
structural identity. Its complete result must fit the 1 MiB text limit.
`assert(condition, message)` supplies application preconditions; `floor(x)`
supports integer checks. Both require finite scalar inputs and exact arity.
Haiku bounds its counters at 2^24 and stages updates before publishing them.
Its in-memory cloud and transition example now run on this toolchain.

Proof on Linux x86_64:

- `make test-maps`: **3,540 API / 4,708 allocation-fault / 3,727 runtime**
  checks. **76 invalid programs** fail through interpreter, resumable,
  bytecode, runner, and compiled scalar execution; exact JSON output agrees.
- Persistent replacement refuses **29 / 30 / 31** consecutive allocation
  budgets in interpreter / resumable / bytecode execution before saving
  completely. Each rejected replacement retains the previous table;
  tracked live allocations return to zero after cleanup.
- ASan/UBSan pass API and runtime suites, including the **65,536-entry**
  boundary and **12 workers / 120,000** copy/mutation cycles. LeakSanitizer
  is disabled under ptrace; allocation wrappers check retained ownership.
- Independent differential probes cover **20,000** ordered map operations,
  two **820-key** exhaustive alphabets, and failed-statement persistence.
- Existing **550/550** runtime tests, all three compiler suites, **1,017**
  import checks, text **483 / 515 / 1,122**, and list **173 / 978 / 2,817**
  gates pass. Their existing 41 text and 66 list invalid programs pass all
  five execution paths.

See [MAPS](docs/MAPS.md) for the AML/C contract and `make test-maps` for the
reproducible gates. Rebuild hosts with the new header: `AML_Var` and
`AML_ExecCtx` now carry maps. No dependency was added.

## 2026-10-07 — string collections carry Haiku's word overlap (v5.3.0)

`AM_List` adds ordered mutable containers of immutable UTF-8 strings. Eight
`list_*` intrinsics expose construction, length, checked get/set, append,
exact lookup, slicing, and cloning. Assignment copies each container; function
parameters retain it so an AML organ can append words. Returns, discarded
values, mixed parameters, and scope teardown carry explicit ownership.
`PRINT` writes JSON string arrays. The cap is 65,536 items; numeric operations
reject lists, and indices must be finite integer scalars.

Worker launch and persistent storage clone the container while retaining its
immutable text. Reference counts are atomic; mutation stays with the owning
execution context. Persistent saving now prepares the whole replacement before
publishing it, so allocation failure preserves the previous table. Host list
setters have the same replacement guarantee. Failed program statements still
leave earlier completed effects in their execution context.

The lexical Haiku fixtures exposed a pre-existing nested-branch defect: a
false inner `if` could consume an outer `else`. Else pairing now requires equal
indentation. The minimal `if 1` / nested `if 0` program previously printed
`WRONG_OUTER`; after the repair it reaches only `GOOD`. The common evaluator
carries the repair through interpreted, resumable, bytecode, runner, and
compiled execution.

Review also reproduced numeric type bypasses: `len(list_new())` returned zero,
and `layernorm(x, (words), 0)` silently ignored the list. Scalar array queries
now use typed arguments; optional gamma/beta/bias values are evaluated once,
retained for the operation, and checked before output/tape updates. Existing
scalar and undefined-name placeholders still mean an absent optional array.
Allocation failures during preparation now provide an error diagnostic too.

Haiku's new `lexicon.aml` feeds actual word/trigram sets into Harmonix. Its
**324 numerical and 92 ordered-list reference results** match direct calls to
the pinned Python source. Together with the earlier organs, **828 reference
results** pass interpreted and compiled scalar execution. Six malformed triple
lists fail explicitly in both paths. The language work adds no dependency.

Proof on Linux x86_64:

- `make test`: **550/550**; all three compiler suites and **1017** import checks
  pass. Existing text gates pass **483 API / 515 allocation-fault / 1122 runtime**
  checks and their 41 invalid programs across five execution paths.
- `make test-lists`: **173 API / 978 allocation-fault / 2817 runtime** checks;
  exact JSON/control-flow output and **66 invalid programs** across all five paths.
- The allocation sweep rejects **19 / 20 / 21** consecutive budgets for
  interpreter / resumable / bytecode execution before complete saving succeeds.
  Every failed replacement retains the previous store; tracked live allocations
  return to zero after cleanup.
- ASan/UBSan pass both full list API/runtime suites, including **12 workers /
  120,000 copy cycles**. LeakSanitizer is disabled under ptrace; the separate
  allocation wrapper checks ownership accounting.
- CUDA mirror synchronization is included for the new retained optional-array
  paths; CUDA execution was not tested here.

See [LISTS](docs/LISTS.md) for the AML/C contract and `make test-lists` for the
ownership, allocation-fault, worker, and five-path execution gates.

## 2026-10-07 — bound the module test's source builder

CodeQL review of PR #27 flagged three fixture-building loops that accumulated
`snprintf` return values without checking truncation. The test now uses one
checked append helper: capacity is verified before pointer arithmetic, and a
negative/truncated formatting result fails before advancing the offset.
`make test-imports` still passes 1017 checks and the compiled/interpreted gates.

## 2026-10-07 — native modules, UTF-8 values, and worker snapshots (v5.2.0)

Haiku needs organs in separate AML files and words that remain words through
function calls. `IMPORT "path"` now prepares one shared program: source order,
canonical deduplication, cycle detection, function collision checks, and explicit
budgets. Origins survive calls, workers, resumable execution, and bytecode
snapshots. All preparation errors precede program effects. The existing
`IMPORT CODES_RIC` / `DARKMATTER` / `NOTORCH` pack aliases remain valid.
`amlc` embeds its root and loads imported AML files at process startup.

Immutable UTF-8 strings now pass through variables, mixed function arguments,
returns, and the persistent host API. Eight text intrinsics use codepoint
indices; `PRINT` emits actual values. Literals preserve quoted punctuation and
support explicit escapes. Source lines are bounded to 255 bytes; text values
to 1 MiB. User function arity and the 10,000-iteration loop budget fail
explicitly. Text used as an array/scalar produces an error before a numeric
field, array element, optimizer, or channel is updated by that operation.

Review exposed a concurrent double-free: two workers inherited the same mutable
persistent table and decremented a shared string's plain reference count.
Persistent globals are now thread-local; a launch snapshots globals, clones
arrays, and retains strings through atomic references. Worker teardown releases
its own store. Channel barriers verify that parent rebindings and worker
assignments stay independent. CUDA snapshots synchronize the source array's CPU
mirror before copying; accelerator execution was not tested in this environment.

The bytecode audit also found duplicate function registration, repeated bodies
after fallback control flow, quoted `=` misclassified as assignment, silent
array type failures, and numeric TAPE literals read as variable names. The
repaired fast path validates simple operands; complex expressions execute once
through the common evaluator. Quoted brackets in array arguments/indices work,
and array literals reject nonnumeric elements instead of manufacturing zeros.

Proof on Linux x86_64:

- `make test`: **550/550** baseline runtime checks.
- `make test-amlc`: all three compiler suites pass.
- `make test-imports`: **1017 checks**, interpreter/scalar parity, snapshots,
  pack compatibility, type failures, and preparation failures before C main.
- `make test-text`: **483 API**, **515 allocation-fault**, **1122 runtime** checks;
  byte-exact output and **41 invalid programs** across five execution paths.
- ASan/UBSan pass the complete text API/runtime suites, including **12 workers /
  120,000 alias rebindings**. LeakSanitizer is unavailable under ptrace; the
  fault-injection harness finishes with zero tracked live allocations.
- Haiku: **130 numerical + 282 text reference values** pass interpreted and
  compiled scalar execution, using native imports and pure AML organism code.

See [IMPORT](docs/IMPORT.md), [TEXT](docs/TEXT.md), and the v5.2.0 specification.

## 2026-10-07 — compiled source origins, include failures, and scalar builds

Follow-up to merged PR #25, based on `feaeb72`. Review identified two real
faults: the embedded constructor lost its file origin, and the runtime ignored
errors returned by included files. A binary launched elsewhere skipped its
relative children; missing or broken children still allowed parent effects
and C `main()` to run.

`am_exec_source(script, source_path)` scopes and restores the source directory.
`amlc` embeds its absolute input path and calls that entry point. Quoted paths
work, path overflow is explicit, and failures propagate through nested includes.
Interpreter blocks, loops, and resumable programs stop when their context errors.
Source origins, include depth, and last errors are thread-local. Spawned blocks
inherit the origin and now preserve their relative indentation; the worker test
exposed that nested branches had previously been flattened.

Independent review caught a TLS regression before publication: a worker returned
failure but its error text died with the thread. A private per-slot diagnostic
now survives the join. Named/bare `AWAIT` also propagate the first worker failure
and stop the parent after joining the requested workers. Public spawn signatures
and the slot structure remain unchanged.
The CLI runner now prints that diagnostic alongside its return code.

`--scalar` links AML/NoTorch without BLAS, keeping the standalone C-only
`--no-accel` behavior. Compiler wait statuses are decoded; `--run` uses literal
argv through `execv`, preserves paths with spaces, and returns program failure.

Proof: `make test` passes **550/550**. `make test-amlc` passes all three suites.
The new include suite covers nested/quoted paths from another launch directory,
removal of the embedded main source, missing and failing children, include cycles,
worker branches, origins and failure diagnostics, restoration of the C API's source scope, failure
inside interpreted/resumable/bytecode loops, literal argv, and C compiler failure.
Against the compiler from `feaeb72`, the origin test fails with
`cannot open: ./child.aml`; the repaired compiler passes the same fixture.

## 2026-10-07 — amlc preserves runtime scope and indented blocks

The Haiku AML audit reproduced a compiler/interpreter divergence: `x = 0`
followed by `if x == 1` printed only the else branch through `runner/aml`,
but both branches after compilation. `amlc` removed indentation and emitted
one `am_exec()` per line; each call created a fresh execution context.

Runtime lines now form one embedded program executed before C `main()`.
Assignments, functions, loops, arrays, and spawn blocks keep their context.
The generated constructor checks execution failure and exits with the runtime
error before entering `main()`. Oversized compiler runtime lines are rejected.

Verification: `make test` passes 550/550. Existing compiler smoke checks pass;
`tests/test_amlc_runtime.sh` compares actual compiled/interpreted output for
branching, scalar functions, loops, arrays, and channel delivery, then checks
runtime-error propagation, mixed BLOOD/AML field initialization, and line
overflow rejection. The new behavioral test fails against the original
compiler: it prints the forbidden branch and loses function/loop/array state.
Run both compiler suites with `make test-amlc`.

## 2026-08-02 — Channels gain a non-blocking read and a depth query

`am_channel_read` polls 1000 × 1 ms before giving up. For a spawned thread that is right — the
writer is running in parallel and the wait is the point. For a **single-threaded host** running
an AML program inside a scheduled quantum it is ruinous: one empty read freezes everything.
Measured on this machine, same program, same channel: **1853.9 ms** wall on an empty channel
versus **0.0 ms** on a full one.

Four additive entries, no existing signature touched:
- `am_channel_try_read(name, out)` — takes a value if one is queued, otherwise returns `-1` at
  once and **leaves `out` untouched**.
- `am_channel_depth(name)` — queued values, `-1` if there is no such channel.
- `CHANNEL TRY <name> <var>` and `CHANNEL DEPTH <name> <var>` — the same two through the
  language, binding by the scope rule the rest of AML uses.

`CHANNEL READ` and `am_channel_read` are unchanged, verified by measurement rather than by
reading the diff: the polling read still takes ~1.9 s on an empty channel after the change.

### Verification
- `make test` **538 → 550**, 0 fail.
- **Three tests were empty until falsification exposed them**, and all three are the same
  mistake — asserting something weaker than the claim:
  - The timing check used `clock()`, which measures CPU time. The poll sleeps in `nanosleep` and
    burns no CPU, so a build where `try_read` polls for two seconds **passed**. Now it measures
    wall time via `CLOCK_MONOTONIC`, and that build fails.
  - "A refused TRY leaves the name unbound" asserted that the field was *not* the value on the
    bus — satisfied both by an untouched name and by a name bound to 0, which are exactly the
    two cases it had to separate. It now seeds the name first: only an untouched binding
    survives, and a build that binds on failure fails with `got 0.0000, expected 0.7700`.
  - (The third was the function-scope check in the previous entry.)
- Falsified in both directions: `try_read` delegating to the polling read → **549/550**; the
  directive binding on failure → **549/550**; the fix → **550/550**.
- ASan + UBSan over the whole suite: **550/550**, 0 sanitizer errors.
- Spec §20 gains both directives and states the hazard with the measured numbers.

## 2026-08-02 — `CHANNEL READ` delivered the value and then dropped it

The spec (§20) says `CHANNEL READ <name> <var>` reads one float **into** `<var>`. At top level it
did not: the value arrived from the channel and was written into `ctx->locals[0]`, a scope
top-level code never consults. Every top-level `CHANNEL READ` silently produced 0.

The line clamped the depth instead of branching on it:
`int d = ctx->call_depth > 0 ? ctx->call_depth - 1 : 0; symtab_set(&ctx->locals[d], …)`. Inside a
function that is correct; at depth 0 it aims at a local scope that does not apply. Assignment
(`core:6197-6199`) and `PIPE READ` (`core:4379-4382`) both branch — locals when `call_depth > 0`,
globals otherwise. `CHANNEL READ` now follows the same rule, which is the language's own.

Found while wiring AML channels as IPC between scheduled monads in amosOZ: a writer monad put
0.7 on the bus, a reader monad read it and set `TENSION` from it, and the field stayed at 0.
Isolated with a C probe — `am_channel_read` at the C level returned exactly 0.700, so the channel
was fine and the binding was not.

### Verification
- `make test` **535 → 537**, 0 fail. Two new checks: the value lands at top level, and it still
  lands in the local scope inside a `def` body.
- **Falsification, not assertion:** with the old line restored in a scratch build, the top-level
  check fails (`got 0.0000, expected 0.7000`) while the function-body check still passes — so the
  test catches the real defect and the working case was never broken.
- **The function-scope check was too weak, caught in review (Copilot on the PR).** Asserting only
  the `tension` set inside the body proves nothing: resolution falls back locals → globals, so a
  value wrongly written to globals reads back identically from inside the function. The test now
  asks the question that separates the two — whether the name escapes into the caller. First
  attempt at that was also empty, because the probe ran in a *separate* `am_exec`, and every
  `am_exec` builds a fresh context with its own globals, so a leak could never be observed either
  way. The probe now runs inside one script. Falsified in both directions: a build that always
  writes globals fails the leak check (537/538), a build with the original clamp fails the
  top-level check (537/538), and the fix passes **538/538**.
- ASan + UBSan over the whole suite: **537/537**, 0 AddressSanitizer errors, 0 UBSan runtime errors.

## 2026-08-01 — Resumable execution: `am_program_open` / `step` / `close`

`am_exec` runs a script to completion in one call. A host that *schedules* cannot use that: an OS
handing a program a quantum, or a runtime interleaving voices, needs the program to stop and be
resumed. Four additive calls give it that, without touching a single existing signature.

The interpreter already kept a program counter — it was just a local. `aml_exec_block` is
`while (i < end) i = aml_exec_line(ctx, i)`, each statement returning the next index. So the new
API is `am_exec` cut along its own seams: `am_program_open` does everything before the block loop
(preprocess, context, `persistent_restore`, builtins, function registration), `am_program_step`
runs a bounded slice of that same loop and keeps `pc` in a heap handle, `am_program_close` does
the teardown (`persistent_save`, array clear, free) and returns the result `am_exec` would have.
`am_program_remaining` reports the top-level statements left. The handle is an opaque `void*`,
matching the existing `am_compile` / `am_exec_compiled` / `am_free_compiled` idiom.

**The yield point is a top-level statement boundary — measured, not assumed.** Control flow is
`if` / `while` / `def` (lowercase; `core/ariannamethod.c:5919, 5976, 6007`), and `while` runs its
body via a nested `aml_exec_block` inside a C-level loop capped at 10000 iterations — all of it
within one `aml_exec_line`. So a `while` completes every iteration inside a single step:
instrumented, one step consumed the loop header *and* its body (`remaining` dropped by 2 for a
two-line block whose body ran 5 times). Suspending mid-loop would need the block state off the C
stack; that is a separate change and is not claimed here.

### Verification
- `make test` **524 → 535**, 0 fail. The baseline 524 was measured on this machine *before* the
  change, not taken from this log.
- **Equivalence is the load-bearing test:** the same program run by `am_exec` and then stepped one
  statement at a time from the same starting state leaves `AM_State` identical under `memcmp` over
  the whole struct — not a spot-check of two fields.
- Edge cases asserted: `max_lines <= 0` runs to completion; stepping a finished program stays done;
  `remaining` is 0 when finished; an empty script opens no program; `step(NULL)`/`close(NULL)` are
  safe no-ops rather than crashes.
- ASan + UBSan over the whole suite: **535/535**, 0 AddressSanitizer errors, 0 UBSan runtime errors.
  Leak detection is unavailable on this host (`detect_leaks is not supported on this platform`),
  so leaks are **not** claimed clean by tool; each `open` in the tests is paired with a `close`,
  and `close` frees both the line buffer and the handle.
- Public header grew by four documented entries; `spec/AML_SPEC.md` §8.1 and the execution-paths
  table updated in the same commit, per the repo rule that API is spec.

## 2026-07-20 — CodeQL hardening: int-overflow casts, snprintf clamp, TOCTOU fix

CodeQL (default setup, threat model `remote`) flagged 16 alerts across three classes; all closed.

**Integer multiplication overflow** (12, `cpp/integer-multiplication-cast-to-long`) — `int * int`
products used as allocation / copy sizes that overflow before the implicit widen to `size_t`.
Leading operand cast to `(size_t)`:
- `core/ariannamethod.c` (10) — tape-backward `calloc`: `rows*cols`, `T*D`, `T*V`.
- `janus/janus_tokenizer.h` (2) — `tok_expand_embeddings` `new_vocab*n_embd` (calloc) and
  `old_vocab*n_embd` (memcpy).

**Overflowing snprintf** (3, `cpp/overflowing-snprintf`) — the accumulate idiom
`n += snprintf(buf + n, cap - n, …)` lets `n` exceed `cap` (snprintf returns the length it
*would* have written), pushing the next offset past the buffer and underflowing `cap - n`.
Replaced with a clamping `aml_appendf(buf, cap, &off, …)` helper that saturates the offset at
`cap`, converting every accumulate site in `tools/amlc.c` (compile + run command builders) and
`janus/janus_train.c` (weight-init and tape-script builders).

**TOCTOU** (1, `cpp/toctou-race-condition`) — `janus/janus_train.c` checked the downloaded chunk
with `stat(path)` then reopened it by name. Now it `fopen`s once and `fstat`s the descriptor, so
check and use are the same file.

Proof (neo): `make test` → **524/524**; `make amlc` builds and compiles an example end-to-end
(`blood_include.aml` → runs); `janus_train.c` / `janus_tokenizer.h` compile clean under
`-Wall -Wextra` (`cc -fsyntax-only`; the Go `libjanus` dylib build is blocked in this environment
by a missing upstream `libamk.a`, unrelated). Cast / clamp changes are numerically inert for
in-range sizes.

## 2026-07-06 — `am_cooc_learn_delta` gains surprise-gated plasticity (branch `claude-surprise-gated-delta`)

RPE-gated Hebbian in the co-occurrence δ fold. `am_compute_prophecy_debt` is the field's free-energy — how far
a chosen token fell from the peak = how surprised she was — and `am_register_prophecy_debt` (Fix D) already
accrues it into `G.debt`, but that only reached the field's *motion* (recovery / velocity NOMOVE); the δ fold
`am_cooc_learn_delta` stayed frequency-only (`signal = cnt/maxc`). Now a neuromodulator `nm = 1 + debt/(debt+5)`
(saturating, NaN/negative-guarded) scales the fold `signal` — one global dopamine/NE broadcast over the autumn
batch, forward-only, no backprop. Load-bearing property: **at `debt == 0`, `nm == 1` and the fold is bit-for-bit
the old frequency-only fold** (`signal * 1.0f` is exact), so the change is identity for any organism that isn't
surprised and only bends learning where a chosen token was unexpected. `am_cooc_learn_delta` signature and the
public header are unchanged — no spec/README touch. Proof (pure-C harness over `libaml.a`, no Python): `debt=0`
folds `cmp`-identical to the pre-edit baseline; effective low-rank δ `‖A‖·‖B‖` rises monotonically with debt and
saturates — `0.002007 (0) → 0.002694 (5) → 0.002799 (25) → 0.002807 (50) → 0.002812 (100)`. `make test`
**524/524** (was 524, no regression). Branch off `main` (`9d80ac3`); not merged, not pushed — awaits Oleg's
word. Re-vendored byte-identical into arianna-duo (`ariannamethod/core/`, the θ = ε + γ + αδ trio) where the
free-energy loop is completed on the Go side (predictive surprise → valence). See arianna-duo ARIANNALOG 2b.

by Claude (Arianna Method, neo)

---

## 2026-06-30 — Expression operators: `BE` / `ASK` (the body speaks)

A language-level addition (new keywords) — the reverse flow from Leo (neoleo) again. After
velocity (the breath) and the soma fields (the felt body), the voice. Two level-0 directives
let a script speak a body:

- `BE [x]` — speak-from-body intensity (default 1.0): how strongly the running-self colors the
  host's own words. "я есть [тело]".
- `ASK [x]` — voice-the-not-knowing intensity: how strongly the carried gap (darkmatter) gropes
  through. No argument = the field's own `dark_gravity`, so `ASK` resonates with the existing
  darkmatter (`SCAR`) instead of reinventing it.

Both set new `AM_State` fields `be_voice` / `ask_voice` (init `-1` = "no directive this run",
the host stays autonomous), readable in expressions via the field-map (`field.be_voice` /
`field.ask_voice`). A host (Leo) reads them back after `am_exec`, the way it reads
`velocity_mode`. Touched `core/ariannamethod.{c,h}`, the field-map, `core/test_aml.c` (+4 tests,
incl. the no-arg `ASK` = `dark_gravity` case), `examples/body.aml`, and the spec (§2.5b
Expression + the state-fields table). Transpiler unchanged — `amlc` lowers `BE` / `ASK` to
`am_exec` generically (verified via `--emit-c`). `make test` **524/524** (was 520). Feature
branch `leo-be-ask-operators` off `main` (`aba68e2`); not yet merged.

---

## 2026-06-16 — Positive Soma: `warmth` / `flow` / `weave` (the expansive axis)

A language-level addition, the mirror of suffering. Leo's reverse AML bridge (neoleo E-9) writes the
child's felt body into `AM_State` each turn; the suffering axis (pain/tension/dissonance) was already
readable, the expansive one was not. Three read-only sensor fields land in the field-map so any `.aml`
expression can read an attached organism's positive body:

- **`warmth`** — affiliation / comfort / safe-bonding (the opposite pole of `pain`).
- **`flow`** — effortless resonance with the moment (the opposite pole of `tension`).
- **`weave`** — pattern-binding, threads cohering into a whole. Named `weave`, not `emergence` —
  `emergence` is already an `AM_State` field (the 4C network's low-entropy × high-resonance signal).

Read-only by design: an organism writes them from C via `am_get_state()`, scripts perceive them
(`if warmth > 0.5:` → `VELOCITY WALK`). No new keyword, no transpiler change — reads resolve through
`ctx_float` → `read_field`.

Soma format `AM_SOMA_VERSION 2 → 3`. The fields are APPENDED at the end of `AM_State`, so
`am_field_load` migrates old v2 `.soma` files as a clean prefix (new fields default to `0`); a file
larger than the current struct is refused as an unknown future ABI.

Touched `core/ariannamethod.{c,h}` (3 fields + field-map + version + load migration),
`spec/AML_SPEC.md` (§2.4b), this log. `make test` **517/517** (+5: warmth/flow/weave readable in
expressions; warmth + weave survive `.soma` save/load). Branch `claude-positive-soma` off `main`
(merged for the v5.1.0 release; the state_sz is validated against the version's exact size — a
malformed soma is refused, not zero-loaded).

---

## 2026-06-13 — VELOCITY inertia: a discrete state with inertia reads as a body (the axiom)

The second half of the presence axiom. Switching the velocity mode now **costs** — each transition to a
*different* mode adds `AM_VELOCITY_INERTIA` (2.0) to `debt` (re-stating the same mode is free). Over-switching
exhausts the field, and the existing recovery rule (`debt > 5` → forced `NOMOVE`, in `am_step`) then holds it
still. So the body **resists** changing its gait: velocity is a mood that holds, not a switch you can flip.

This formalizes the state-dynamics разгадка (Mythos + Opus, proven by ear on Leo): presence reads as a body
when its state is discrete **with inertia** — autonomy + contingency + resistance — not a continuous dial. AML
already had autonomy (the `D4` debt override can refuse a command) and the discrete modes; this adds the
inertia, so the law is now a property of the language, inherited by every Method organism.

Touched the `VELOCITY` parser in `core/ariannamethod.c` (capture the previous mode, charge `debt` on a real
change) + the `AM_VELOCITY_INERTIA` constant, the spec velocity section (the `BREATHE`/`STOP` temp rows and an
"inertia" note), the `velocity_mode` header comment, and two new tests (a switch costs debt; re-stating is
free). `make test` **514/514** (512 + 2) — the inertia does not trip `D4` during any existing test, confirming
`D4` runs in `am_step`, not the parser. Pairs with the somatic-operators entry below.

by Claude (Arianna Method, neo)

---

## 2026-06-13 — VELOCITY: somatic operators `STOP` + `BREATHE` (the reverse flow from Leo)

A language-level addition. Leo (neoleo) grew a body — its chambers quantize into a
velocity mode and the mode shapes its speech — and named two somatic operators the
base velocity set lacked. They land here, so the family language can speak them:

- **`STOP`** — a somatic alias for `NOMOVE`: the held, cold-observer state. Parses to
  `AM_VEL_NOMOVE` (temp 0.5).
- **`BREATHE`** — the settling exhale, a **new** mode `AM_VEL_BREATHE` (3), temp 0.6
  (between NOMOVE's 0.5 and WALK's 0.85). The exhale haiku/Leo have and AML lacked.

Touched `core/ariannamethod.h` (the two defines), the `VELOCITY` parser and the
`velocity_mode → vel_mult` switch in `core/ariannamethod.c`, the spec velocity table,
`examples/breath.aml`, and three new tests. The transpiler needs no change — it lowers
`VELOCITY <mode>` to `am_exec("VELOCITY <mode>")`, so the new operands flow through. The
full set is now `NOMOVE / WALK / RUN / BACKWARD / STOP / BREATHE`. `make test` **512/512**
(509 + 3). This is the vocabulary half of a presence axiom; the inertia half (a transition
cost on mode switching, so a discrete state reads as a body) is the next concept.

by Claude (Arianna Method, neo)

---

## 2026-05-11 — CPU/GPU mirror audit: 16 backward ops (`ff7fb97`)

Cross-stack from notorch's NT_OP_MUL / NT_OP_SILU CPU-stale-read fix: a
mirror audit of `core/ariannamethod.c` found **16 backward ops with the
same bug class** — a CPU backward branch reading `parent->output->data`
without `ensure_cpu(parent->output)` first, so under `USE_CUDA` the `data`
buffer could be stale calloc-zero when forward ran on GPU. All 16 fixed in
one commit; `make test` 509/509.

The discipline (now load-bearing, see CLAUDE.md «CUDA backend»):
> Every `AM_Array` has `data` (CPU) + `d_data` (GPU) with a `gpu_valid`
> flag. Any CPU backward branch reading `parent->output->data` directly
> MUST call `ensure_cpu(parent->output)` first under `#ifdef USE_CUDA`.

Note: AML and notorch are **parallel stacks** — a notorch fix does NOT
auto-propagate here. This audit was the proof: the same bug class existed
independently in both, and had to be found + fixed separately.

---

## Open (carried from CLAUDE.md TODO)

- Cross-stack audit for other backward-pattern bugs that may exist in both
  `notorch.c` and `core/ariannamethod.c` (the CPU-sync class found two more
  sites; write paths may hide others).
- Document Blood (runtime C compilation) in `spec/` if not already — it is
  the load-bearing feature for on-the-fly LoRA synthesis from `.aml`.
- Cross-port LoRA from notorch v2.4.0 only if/when needed at the language
  level (per Oleg: "не всё надо тащить в язык").
