# String lists in AML

A list is a mutable, ordered container of immutable UTF-8 strings. Duplicates
and empty strings are valid items. Haiku uses lists for words and flattened
triples; lookup and deduplication remain AML operations.

```aml
def append_word(words, word):
    list_push(words, word)
    return words

words = list_new()
list_push(words, "сова")
copy = words
result = append_word(words, "🦉")
list_set(result, -1, "שלום")
PRINT words
PRINT copy
PRINT result
```

Output:

```text
["сова", "🦉"]
["сова"]
["сова", "שלום"]
```

## Intrinsics

Names are case-insensitive and reserved against user definitions. They occupy
no function-table slots. Every intrinsic accepts exactly the arguments shown.

| Expression | Result and effect |
| --- | --- |
| `list_new()` | New empty list |
| `list_len(xs)` | Number of items |
| `list_get(xs, index)` | String at the index |
| `list_push(xs, text)` | Append the string; return the new length |
| `list_set(xs, index, text)` | Replace one item; return the stored string |
| `list_find(xs, text)` | First matching index, or -1 |
| `list_slice(xs, start, end)` | New container with the selected items; end is exclusive |
| `list_clone(xs)` | New container with all items |
| `list_key(xs)` | Canonical text key preserving the exact ordered items |

`list_push` and `list_set` work as standalone statements and inside expressions.
`list_find` compares exact UTF-8 bytes and starts at index zero. It performs no
case folding or normalization.

`list_key` prefixes the item count and each UTF-8 byte length with decimal
digits and `:` separators. `[]` becomes `0:`; `["", "a:b"]` becomes
`2:0:3:a:b`. Content is copied unchanged, so empty items, embedded delimiters,
and Unicode spelling retain their identity. The result must fit the 1 MiB
text limit. This encoding is useful for the compound keys in [numeric maps](MAPS.md).

Negative indices count from the end. Get/set reject indices outside the list;
slice bounds clamp to `[0, list_len(xs)]`, and an end before the start produces
an empty list. Indices must be finite integer-valued AML scalars in the signed
32-bit range. Wrong types, wrong argument counts, and out-of-range get/set
operations produce runtime errors.

Use these intrinsics to access lists. Bracket indexing, numeric array
operations, scalar expressions, and numeric TAPE operands do not consume lists.
`[1, 2]` remains numeric-array syntax; list construction uses `list_new` and
`list_push`. Items are strings, so nested lists and numeric list items are
rejected.

Numeric `len`, `sum`, `dot`, `rows`, and `cols` use typed arguments and their
documented argument counts. Passing a parenthesized list or a function that
returns a list is an error just like passing a list variable. Optional numeric
array arguments in `layernorm`, `seq_layernorm`, and `spa_connectedness` also
reject strings, lists, and maps. Their existing numeric/undefined-name sentinel for
an absent optional array remains valid. Argument expressions are evaluated
once, with temporary references released after the operation.

## Copying, calls, and workers

Every AML assignment of a list clones its container, including assignments of
function results and parenthesized values. The immutable string items retain
references. Replacing an item or appending to the copy leaves the original
container unchanged.

Function parameters retain the argument's list reference. Mutation through a
parameter is visible to its caller. Assignment to that parameter binds a new
copied container in the function's local scope. Returning a list transfers an
owned reference; subsequent assignment applies the container-copy rule.
Nested calls, discarded return values, top-level returns, and scope cleanup
release their owned references.

Persistent restoration and saving clone list containers and retain their string
items. `am_set_var_list` does the same. `SPAWN` takes a launch-time snapshot of
global variables, so parent and worker mutations affect separate containers.
The worker's persistent table is thread-local and is cleared at worker exit.

List and string references are atomic. Container mutation belongs to one
execution context; a C host sharing a mutable list between threads synchronizes
its own mutations. AML worker snapshots provide separate containers.

## Output and bounds

`PRINT xs` writes a JSON array followed by LF, using comma-space separators.
Strings use double quotes. Quotes and backslashes are escaped; LF, CR, and TAB
use `\n`, `\r`, and `\t`. Other bytes below `0x20` use lowercase `\u00xx`.
Other UTF-8 bytes, including DEL, are printed directly. An empty list prints
`[]`. Printing a string obtained with `list_get` retains ordinary string PRINT
behavior, without JSON quoting.

A list holds at most **65,536 items** (`AM_MAX_LIST_ITEMS`). Every string keeps
the existing 1 MiB UTF-8 byte bound. Source lines retain the 255-byte bound;
list construction can span statements, functions, and modules.

Failed push/set operations leave that list unchanged. Push growth allocates a
replacement item buffer before publishing it; a failed allocation retains the
old buffer, capacity, length, and item references. Cloning, slicing, and host
setters leave their source containers unchanged on allocation failure. Failed
persistent replacement preserves the previous persistent table.

These guarantees apply to the named operations. Program execution preserves
the effects of completed earlier statements. After a later runtime error, the
runtime still attempts to save completed global changes to persistent storage;
that save itself must allocate successfully before replacing the prior table.

## C API

`AM_List` exposes `items`, `len`, `capacity`, and atomic `refcount` storage.
Callers maintain ownership through the API and treat each `AM_String` item as
immutable.

| Function | Ownership and failure result |
| --- | --- |
| `am_list_new()` | Owned empty list; NULL on failure |
| `am_list_ref(xs)` / `am_list_free(xs)` | Retain/release a container; NULL is accepted |
| `am_list_clone(xs)` | Owned container copy; NULL on failure |
| `am_list_push(xs, item)` | Borrow `item`, retain on success; new length or -1 |
| `am_list_get(xs, index)` | Owned retained string; NULL for invalid list/index |
| `am_list_set(xs, index, item)` | Borrow `item`, retain on success; zero or -1 |
| `am_list_find(xs, item)` | First matching index or -1 |
| `am_list_slice(xs, start, end)` | Owned container with clamped bounds; NULL on failure |
| `am_list_key(xs)` | Owned canonical key string; NULL on failure or size overflow |

Release strings returned by `am_list_get` with `am_string_free`. C set returns
a status; AML set returns the stored string. Self-replacement is valid: set
retains the incoming item before releasing the previous slot.

After `am_init()`, `am_set_var_list(name, xs)` validates the identifier, clones
the container into the calling thread's persistent table, and enables
persistent mode. Zero means success. Failure preserves the old binding and
the caller's list. `am_get_var_list(name)` returns a borrowed `const AM_List*`,
or NULL for a missing/nonlist variable. The borrowed pointer remains valid
until the calling thread mutates or resets persistent state. Clone it to retain
an independent container.

Rebuild C hosts against the updated header and library: `AML_Var` and
`AML_ExecCtx` now include list fields.

## Verification

```sh
make test-lists
```

The dedicated suite checks low-level ownership and allocation failures, typed
execution, copied assignments, shared parameters, persistent storage, worker
snapshots, numeric type rejection, exact JSON output, and interpreter/resumable/
bytecode/runner/compiled scalar parity.
