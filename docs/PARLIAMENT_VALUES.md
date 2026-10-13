# Owned parliament arithmetic

These three array operations extend the optional canonical NoTorch numerical
bridge. They allocate a fresh AML result, borrow their input values for the
call, and never enter a global autograd tape. AML owns experts, immune memory,
event receipts, learning policy and publication. The callbacks do not load or
train a model.

| AML call | Result |
|---|---|
| `nt_parliament_elect(votes, resonance, alive, minimum, consensus)` | `[gates E, next_consensus, elected_count]` |
| `nt_parliament_inject(A, B, gates, x, rank, alpha)` | `x + alpha * sum(gate[e] * A[e] @ (B[e] @ x))` |
| `nt_oja(A, B, x, dy, noise, cursor, lr, signal)` | `[updated A, updated B, next_cursor]` |

Arrays use row-major order. Election derives E from the vote length. Injection
derives E from gates and D from x; A is `[E,D,R]` and B is `[E,R,D]`.
Oja updates one expert: D is the length of x, R is `len(A)/D`, and its rank
window is `len(noise)`. The supplied cursor and noise belong to the caller;
there is no shared rotating rank counter or hidden random draw. Cursor advances
by the window modulo R; a signal with magnitude below `1e-8` preserves the
matrices and cursor. Generate explicit noise with an owned `rng_normal`
stream when the application wants it.

E and R are in 1..16. Array sizes remain within AML's ordinary array budget.
The alive mask contains exactly zero or one, at least `minimum` experts must
be alive, and consensus/gates lie in [0,1]. Scalars and every array element
must be finite. Oja requires an integer cursor in `[0,R)`, a window in 1..R,
and nonnegative learning rate. Invalid shape, type, backend status or output
stops execution before the assignment publishes a value.

The returned election must have unit gate mass within `2e-6`, no gate for a
dead expert and a selected count within the live population. Softmax underflow
may leave fewer positive gates than elected slots. The C primitive permits an
undersized live population as a zero-gate no-op; AML deliberately requires the
declared minimum before invoking it so application policy handles that state.

The election and bounded Oja equations are defined by canonical NoTorch's
`nt_parliament_elect_values` and `nt_oja_values`. In particular Oja is the
DoE-derived rule with an explicit rank window; it is distinct from
`nt_hebbian_step`. CPU values are the tested backend here; calling NoTorch
does not imply an automatic Metal dispatch.

```aml
votes = nt_linear([1,0, 0,1], [0,0], [0.25,0.75], 2, 2)
election = nt_parliament_elect(votes, [0,0], [1,1], 2, 0.5)
gates = zeros(2)
gates[0] = election[0]
gates[1] = election[1]
proposal = nt_parliament_inject([0.1,0.1, 0.2,0.2], [0.1,0.2, 0.2,0.1], gates, [0.25,0.75], 1, 0.1)
assert(len(proposal) == 2, "owned concept proposal")
```

`AM_ParliamentBackend` supplies optional `parliament_elect`,
`parliament_inject`, and `oja` callbacks through
`am_set_parliament_backend`. The separate table preserves the original
seven-callback `AM_NumericalBackend` C ABI. A host that does not provide an extension receives
an explicit `parliament backend unavailable` error on that call. Register
before starting programs or workers; `am_use_notorch()` installs canonical
implementations together with the other NoTorch services.

The focused gate compares all returned values to direct canonical calls
through the interpreter, bytecode and resumable host, checks malformed calls,
refuses missing/invalid callbacks and verifies that failed assignments preserve
the previous value. The shell gate also compiles and runs an ordinary AML
fixture through `amlc --scalar`.
