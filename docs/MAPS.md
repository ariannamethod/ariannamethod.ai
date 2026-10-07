# Ordered numeric maps in AML

A numeric map associates exact UTF-8 strings with finite float scalars. The
container grows with the words it encounters and remembers their insertion
order. Haiku uses these maps for word weights, occurrence counts, and its
ordered trigram memory.

```aml
def encounter(weights, word):
    if map_has(weights, word):
        return map_set(weights, word, map_get(weights, word) * 1.1)
    return map_set(weights, word, 1)

weights = map_new()
encounter(weights, "silence")
snapshot = weights
encounter(weights, "echo")
PRINT weights
PRINT snapshot
PRINT map_keys(weights)
```

Output:

```text
{"silence": 1, "echo": 1}
{"silence": 1}
["silence", "echo"]
```

## Intrinsics

Names are case-insensitive and reserved against user definitions. Intrinsics
occupy no function-table slots and require exactly the shown arguments.

| Expression | Result and effect |
| --- | --- |
| `map_new()` | New empty map |
| `map_len(m)` | Number of entries |
| `map_has(m, key)` | One if the key exists, otherwise zero |
| `map_get(m, key)` | Stored scalar; an absent key is an error |
| `map_set(m, key, value)` | Insert or replace; return the stored scalar |
| `map_delete(m, key)` | Delete and return one; return zero for an absent key |
| `map_keys(m)` | Independent string list of keys in insertion order |
| `map_clone(m)` | Independent map with the same entries and order |
| `list_key(xs)` | Canonical string encoding of an ordered string list |

Keys are strings; empty strings and all valid non-NUL UTF-8 text are accepted.
Equality compares exact UTF-8 bytes without normalization or case folding.
Values must be scalar floats and finite. Arrays, strings, lists, maps, NaN,
and infinities cannot be stored as values. There are at most **65,536 entries**
per map (`AM_MAX_MAP_ITEMS`). Replacing an existing entry works at the limit.

Replacing a value preserves the original key and its position. Deleting a key
preserves the relative order of the remaining keys. Inserting that key again
appends it. `map_keys` retains immutable key text in an independent list;
mutating that list or the original map leaves the other container unchanged.

Lookup uses a hash table with linear probing. A separate entry array preserves
order; its hash index is kept below or at half occupancy. Insertion grows
storage geometrically. Deletion compacts the ordered array and rebuilds the
existing hash index without allocating. Lookup and replacement have expected
constant cost; deletion has linear cost in the current entry count.

Maps use the map intrinsics. Bracket indexing, scalar expressions, numeric
array operations, text/list operations, and numeric TAPE operands reject maps.
`map_set` and `map_delete` are also valid standalone statements.

## Composite keys

`list_key` encodes a string list as:

1. The item count in decimal ASCII followed by `:`.
2. For each item, its UTF-8 **byte** length in decimal ASCII, then `:`, then
   the exact raw UTF-8 bytes of that item.

There is no padding, leading zero, escaping, or separator after an item.
The count and lengths delimit the encoding. Empty items, colons, whitespace,
quotes, control characters, and multibyte text remain distinct.

| List | Encoded text |
| --- | --- |
| `[]` | `0:` |
| `[""]` | `1:0:` |
| `["", ""]` | `2:0:0:` |
| `["a", "bc"]` | `2:1:a2:bc` |
| `["ab", "c"]` | `2:2:ab1:c` |
| `["é", ":"]` | `2:2:é1::` |

The output is valid UTF-8 and retains ordinary string semantics: `text_bytes`
counts encoded bytes, while `text_len` counts codepoints. The complete encoding
must fit the existing **1 MiB string limit**, including decimal prefixes. The
size is checked before allocating. Input lists and strings remain unchanged.
For example, a three-word list can safely key trigram counts without choosing
a separator that might also occur inside a word.

## Preconditions and integer-valued scalars

`assert(condition, message)` accepts exactly two arguments: a finite scalar
and a UTF-8 string. A nonzero condition returns one. Zero stops execution with
`assertion failed: ` followed by the message, subject to the runtime's existing
error-buffer length. Argument expressions are evaluated once, in source order.
Statements completed before the assertion remain completed.

`floor(x)` accepts exactly one finite scalar and returns its value rounded
downward using the scalar float representation. Both names are reserved
case-insensitively and occupy no function-table slots.

```aml
def count_once(counts, key):
    n = 0
    if map_has(counts, key):
        n = map_get(counts, key)
    assert(n >= 0 and n == floor(n), "count must be a nonnegative integer")
    assert(n < 16777216, "count exceeds exact scalar integers")
    return map_set(counts, key, n + 1)
```

AML scalars are float32. Consecutive nonnegative integers are exactly
representable through 16,777,216. Maps preserve the scalar representation;
applications choose and check their count and time domains explicitly.

## Copying, calls, and workers

Every AML assignment clones the map container, including assignments of
parenthesized values and function returns. Keys retain immutable string
references. Function parameters retain the caller's map, so mutating it through
a parameter is visible to the caller. Assigning a new value to that parameter
creates a local copied binding. Returns transfer an owned reference; a
subsequent assignment still follows the copy rule.

Scope cleanup, discarded return values, top-level returns, and nested calls
release their owned references. Persistent restoration, persistent saving, and
`am_set_var_map` copy map containers. `SPAWN` copies the launch-time global
state, including maps, so parent and worker mutations operate on independent
containers. Each worker clears its thread-local persistent table on exit.

Map and string reference counts are atomic. Container mutation belongs to one
execution context; a C host sharing a mutable map across threads synchronizes
its own mutations.

## Output and allocation guarantees

`PRINT m` emits a JSON object in insertion order, followed by LF. Entries use
comma-space separators and colon-space between key and value. Keys use the
same JSON escaping as [string lists](LISTS.md). Scalars use `%.9g`, enough
decimal digits to round-trip float32; only finite values enter the map. An
empty map prints `{}`.

Growth allocates a replacement entry buffer and hash index before publishing
either. Failed insertion leaves capacity, buffers, length, values, order, and
key references unchanged. Replacing an existing value and deleting a key
allocate nothing. Clone, keys, and composite-key construction free incomplete
outputs on failure and leave their sources unchanged. Host setters preserve
the previous binding on failure. Persistent saving prepares the complete
replacement symbol table before publishing it.

These guarantees cover each named operation. Multiple successful mutations
are separate effects. After a later runtime error, execution still attempts to
save completed global changes to persistent storage; that save must itself
allocate successfully to replace the old table. An application that needs a
transaction across several entries prepares a copied map and publishes it
only after its work succeeds.

## C API

`AM_MapEntry` contains `key`, `value`, and `hash`. `AM_Map` contains `entries`,
`len`, `capacity`, `buckets`, `bucket_capacity`, and atomic `refcount` storage.
The entry array is insertion-ordered; a bucket contains its entry index plus
one, with zero denoting an empty bucket. Treat these fields as API-maintained
storage and use the functions below to mutate it.

| Function | Ownership and failure result |
| --- | --- |
| `am_map_new()` | Owned empty map; NULL on failure |
| `am_map_ref(m)` / `am_map_free(m)` | Retain/release; NULL is accepted |
| `am_map_clone(m)` | Owned independent copy; NULL on failure |
| `am_map_has(m, key)` | One if present; zero if absent or either input is NULL |
| `am_map_get(m, key, out)` | One if found, zero if absent, -1 for NULL arguments; leaves `*out` unchanged unless found |
| `am_map_set(m, key, value)` | Zero on success, -1 for invalid arguments, nonfinite value, full map, or allocation failure |
| `am_map_delete(m, key)` | One removed, zero absent, -1 for a NULL argument |
| `am_map_keys(m)` | Owned independent list with retained keys; NULL on failure |
| `am_list_key(xs)` | Owned immutable encoded string; NULL for a NULL input, excessive output size, or allocation failure |

Set borrows its key and retains it only when inserting a new entry. Replacing
an equal key updates the value in place and retains the original key. Get
copies its scalar into the caller's output. Delete borrows its lookup key and
releases the map's key reference when found. Clone and keys retain references
to immutable text; their containers have independent ownership.

After `am_init()`, `am_set_var_map(name, m)` validates the identifier, copies the
map into the calling thread's persistent table, and enables persistent mode.
Zero means success; failure preserves the old binding and the caller's map.
`am_get_var_map(name)` returns a borrowed `const AM_Map*`, or NULL for a missing
or non-map binding. That pointer remains valid until the calling thread
mutates or resets persistent state. Clone it to retain an independent map.

Rebuild C hosts with the updated header and library: `AML_Var` and
`AML_ExecCtx` now include map fields.

## Verification

```sh
make test-maps
```

The suite covers ordered lookup and deletion, map limits, composite keys,
ownership, allocation failures, copied assignments, shared parameters,
persistent state, worker snapshots, numeric type rejection, exact output,
assertions, scalar floor, and interpreter/resumable/bytecode/runner/compiled
execution.

The scalar gate reports **3,540 API checks**, **4,708 allocation-failure checks**,
and **3,727 runtime checks**, plus **76 rejected programs across five execution
paths**. The allocation sweep covers entry/hash growth and complete persistent
replacement with mixed map/list/text/array bindings; rejected budgets 29, 30,
and 31 precede successful replacement, and tracked live allocations return to
zero. ASan/UBSan passes the API and runtime suites, including the 65,536-entry
limit and 12 workers performing 120,000 snapshot/rebinding cycles.
