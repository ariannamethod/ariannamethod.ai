# Explicit records, continuing experience

This work follows the [Arianna Method Manifesto](../ARIANNA_METHOD_MANIFESTO.md).

A record owns a named group of AML values. Haiku uses one record for its growing
memory, learner arrays, random streams, turn counter and model identity. The
organism chooses its fields and validates its own relationships in AML. The
runtime copies owners, preserves their exact bytes on disk, and publishes a
checked replacement as one operation.

```aml
state = record_new()
record_set(state, "turn", 0)
record_set(state, "words", list_new())
list_push(record_get(state, "words"), "rain")

pending = record_clone(state)
record_set(pending, "turn", 1)
status = checkpoint_save(pending, "haiku.state")
record_swap(state, pending)
```

Here the file commits before live state changes. The final `record_swap` uses no
allocation when called with already bound record variables. `status == 1` means
both file and containing directory were synchronized. `status == 2` means the
complete new file was committed by rename, but directory synchronization or
close failed. An application must treat both positive statuses as committed;
it can report status 2 without retrying or rolling back the turn.

## Values and ownership

Records preserve insertion order and admit at most **256 fields**. Keys are
ordinary immutable UTF-8 strings, including the empty string. Values are flat
leaves: a float, numeric array or matrix, UTF-8 string, string list, or numeric
map. Records and tokenizer models cannot be fields. A model is reopened from
its application metadata and checked with `tokenizer_identity` before restore.

| Intrinsic | Result |
|---|---|
| `record_new()` | New empty record |
| `record_set(record, key, value)` | Record after independently copying the leaf; an existing key keeps its position |
| `record_get(record, key)` | Retained typed leaf; absent keys are errors |
| `record_has(record, key)` | Scalar 0 or 1 |
| `record_keys(record)` | New ordered string list |
| `record_kind(record, key)` | `"float"`, `"array"`, `"string"`, `"list"` or `"map"`; matrices are arrays |
| `record_clone(record)` | Complete independent mutable contents |
| `record_replace(live, checked)` | Live record after staging a complete independent copy and replacing its contents |
| `record_swap(a, b)` | Scalar 1 after allocation-free exchange of the two records' contents |
| `checkpoint_save(record, path)` | Scalar 1 or 2 after commit; an error before commit |
| `checkpoint_load(path)` | New detached record; missing or invalid files are errors |
| `file_exists(path)` | Scalar 1 for a regular file, 0 for `ENOENT`/`ENOTDIR`; other failures and nonregular paths are errors |

Names are case-insensitive and reserved against user function definitions.
`PRINT record` emits `<record>`; use explicit field access to print its contents.
Records cannot enter scalar or numeric-array expressions.

`record_set`, record assignment, persistent globals and worker snapshots copy
all mutable leaves. Immutable text retains atomic references. Copying a record
does not retain aliases between its mutable leaves: each field owns its own
array, list or map. Function parameters share their record, so helpers can
change its fields. `record_get` retains the selected leaf and can pass it
directly to a mutating helper:

```aml
map_set(record_get(state, "weights"), "rain", 2)
```

Existing leaf assignment semantics still apply: string-list and numeric-map
assignments copy their containers. Array assignment keeps its existing rule:
bare aliases copy, while array-producing calls return their owned result.
For a mutable field snapshot of any leaf kind, clone the record or explicitly
copy the leaf. Call helpers directly with `record_get` when they should mutate
the field's owner.

Replacement stages every leaf before changing live contents. If any allocation
fails, all old fields remain intact. Previously retained child references keep
those old children alive after replacement. Swapping exchanges contents while
keeping both wrapper identities and reference counts; retained child references
remain valid, and swapping a record with itself succeeds without work.

Record reference counts are atomic. Mutable access, replacement and swap belong
to one execution context or a synchronized embedding host. Workers receive
independent mutable snapshots. Earlier statements and argument evaluation keep
their effects if a later operation fails.

## Detached restore

Loading checks the whole binary structure into detached owners. AML then checks
its schema, field kinds, dimensions, counters, RNG representation and other
application relationships before publication:

```aml
checked = checkpoint_load("haiku.state")
assert(text_equal(record_kind(checked, "turn"), "float"), "turn kind")
assert(record_get(checked, "turn") >= 0, "turn value")
record_replace(state, checked)
```

An application can instead use `record_swap` once it already owns and validates
an independent candidate. Restore replaces a state; replaying learning updates
or merging counts would change the saved experience. No checkpoint operation
reads or writes an ambient symbol table, field-weather singleton, tape, or
random source.

## Paths and atomic files

AML paths resolve from the statement's original source file. A function in an
imported module uses that module's directory; an absolute path stays absolute.
Compiled programs retain those origins when launched from another directory.
C APIs use the literal path supplied by the host. Directories must already
exist. `file_exists` follows the path to a regular file and reports permission
and I/O errors explicitly. Existence is a query; a subsequent load still checks
the file it actually opens.

Save validates and encodes the complete bounded record before creating a file.
It then creates a unique mode-0600 `.aml-checkpoint-XXXXXX` temporary in the
destination directory, writes every byte, synchronizes and closes the file,
and renames it to the requested path. Every failure before rename preserves
the previous destination and removes the temporary. Rename is the commit point:
readers see either complete old bytes or complete new bytes. Save then
synchronizes and closes the containing directory, returning 1 or 2 as above.
The destination adopts the temporary file's permissions. An externally killed
process can leave an uncommitted temporary, which is never a load candidate.

Load accepts regular files only, checks a bounded exact read and close, and
rejects truncation, trailing bytes, corruption, unknown versions or flags,
unknown tags, duplicate field/map keys, malformed UTF-8, NUL, invalid shapes,
nonfinite map values and exceeded limits. It neither creates missing state nor
silently substitutes defaults after corruption.

## Portable checkpoint version 1

The complete file is limited to **64 MiB**. It contains at most 256 fields and
**1,048,576 text objects** in aggregate: field keys, string leaves, list items
and map keys. Each string is limited to 1 MiB, lists/maps to 65,536 entries, and
arrays to 1,048,576 floats. These bounds apply before decoded objects become
visible. A record can exist in memory beyond the aggregate checkpoint budget;
saving it then fails explicitly.

All integers are unsigned little-endian. Floats store their **exact IEEE-754
binary32 bits**, including negative zero, subnormals, infinities and NaN
payloads in scalar/array leaves. Maps preserve their existing finite-value
contract. Array shape and every record/map/list insertion order are preserved.
There are no timestamps or host-layout fields, so identical records produce
identical checkpoint bytes. A host must provide IEEE binary32.

| Header offset | Bytes | Meaning |
|---|---:|---|
| 0 | 8 | `41 4d 4c 43 50 00 0d 0a` (`AMLCP`, NUL, CR, LF) |
| 8 | 4 | Format version: 1 |
| 12 | 4 | Flags: 0 |
| 16 | 8 | Payload byte length, exactly file length minus 32 |
| 24 | 4 | Field count |
| 28 | 4 | IEEE CRC32 over header bytes 0–27 followed by the payload |

CRC32 uses the reflected polynomial `0xedb88320`, initial `0xffffffff` and final
XOR `0xffffffff`. Each ordered field consists of a four-byte key byte length,
one-byte tag, three reserved zero bytes, eight-byte value byte length, key UTF-8
bytes, and value bytes. Wire tags are independent of internal AML type numbers:

| Tag | Value body |
|---:|---|
| 1 | Four bytes of scalar float bits |
| 2 | Raw UTF-8 string bytes |
| 3 | `u32 length`, `u32 rows`, `u32 cols`, then length float bit patterns |
| 4 | `u32 count`, then repeated `u32 byte_length` and UTF-8 string bytes |
| 5 | `u32 count`, then repeated `u32 key_byte_length`, UTF-8 key bytes and four-byte float bits |

An array has positive length. Vectors use shape `(0, 0)`; matrices use positive
rows and columns whose checked product equals length. Each value must consume
exactly its declared byte section. Reserved bytes, duplicate keys and every
structural invariant are checked even when a checksum is valid.

## C ownership boundary

`AM_Record` is opaque. The existing `AML_Var` carries a `type` tag and typed
leaf pointer, plus the new `record` pointer for record values.

```c
AM_Record* am_record_new(void);
void am_record_ref(AM_Record* record);
void am_record_free(AM_Record* record);
AM_Record* am_record_clone(const AM_Record* record);
int am_record_set(AM_Record* record, AM_String* key, const AML_Var* value);
const AML_Var* am_record_get(const AM_Record* record, const AM_String* key);
int am_record_has(const AM_Record* record, const AM_String* key);
AM_List* am_record_keys(const AM_Record* record);
int am_record_replace(AM_Record* live, const AM_Record* checked);
int am_record_swap(AM_Record* a, AM_Record* b);
int am_checkpoint_save(const AM_Record* record, const char* path,
                       char* error, size_t error_cap);
AM_Record* am_checkpoint_load(const char* path, char* error, size_t error_cap);
int am_file_exists(const char* path, char* error, size_t error_cap);
int am_set_var_record(const char* name, const AM_Record* record);
const AM_Record* am_get_var_record(const char* name);
```

Record set/replace/swap return 0 on success and -1 on failure. A C `get` result
is borrowed **until any record mutation**; the host must retain a desired leaf
before later mutation. Keys returns an owned list. The host persistent setter
copies its record and returns 0 on success; its getter borrows the stored owner.
Checkpoint errors use an optional bounded NUL-terminated diagnostic buffer.
All unpublished allocations are released on failure.

`make test-records` exercises ownership, allocation refusal, exact portable
bytes, recomputed-checksum structural corruptions, every truncation of a golden
file, I/O faults, worker/persistent copies and
all five execution paths. `make test-tokenizer` includes immutable content
identity and the native loaded-byte digest.
