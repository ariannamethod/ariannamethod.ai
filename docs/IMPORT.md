# Native AML modules

`IMPORT "path/to/module.aml"` prepares another AML source in the current program's function and variable scope. An organism can put its numerical organs, memory operations, and speech functions in separate `.aml` files and call them directly.

```aml
# organs/pulse.aml
pulse_gain = 0.1

def pulse_step(previous, observation):
    return previous + pulse_gain * observation
```

```aml
# main.aml
IMPORT "organs/pulse.aml"
pulse = pulse_step(0.2, 3)
TENSION pulse
```

## Preparation and execution

1. Resolve the root source and each import to an absolute canonical path. Relative imports resolve from the source containing the directive. `.`/`..` and symbolic links resolve to the same file identity.
2. Expand imports in depth-first order at their first occurrence. A completed source appears once per prepared program; reaching a source still being expanded is a cycle and fails preparation.
3. Validate source limits and register all function definitions. Duplicate user function names, collisions with builtins or expression intrinsics, invalid declarations, and function table overflow fail preparation.
4. Execute the prepared statements in order. Imported top-level assignments share the caller's globals. Imported functions use the same function table and can read those globals; ordinary function-local assignment rules remain in force.

Every preparation error happens before any AML statement in that prepared program executes. A statement before a bad import therefore has no effect. This guarantee covers module loading, source budgets, and function registration; ordinary execution errors stop at the failing statement.

`IMPORT` accepts a nonempty double-quoted path and an optional trailing `#` comment. It is case-insensitive and must occur at indentation zero. Path text is literal: no expression evaluation or escape expansion. Import files must be regular files without embedded NUL bytes. An empty module is valid. Each directive retains a no-op source boundary, including repeated imports, so adjacent source blocks do not merge through an empty expansion.

The existing bare pack directives `IMPORT CODES_RIC`, `IMPORT CODES/RIC`, `IMPORT DARKMATTER`, and `IMPORT NOTORCH` remain runtime aliases for `MODE`, including inside conditional blocks. Quoted imports select source modules.

There is one shared namespace for functions and one shared global variable scope. A later top-level assignment to an existing variable updates it in execution order. User function names are case-sensitive; builtin and intrinsic collision checks follow their case-insensitive dispatch. The string intrinsics (`text_len`, `text_bytes`, `text_equal`, `text_find`, `text_slice`, `text_concat`, `text_codepoint`, `text_from_codepoint`) are reserved without consuming function table slots.

## Source origins and INCLUDE

Each prepared line carries its original absolute source path and physical line number. A function body retains the directory of the module that defined it, even when called from another file or after the host changes its working directory. Relative `INCLUDE` inside that function resolves from the module. A spawned block inherits its source directory. Returning to the caller restores the caller's source directory.

`INCLUDE` keeps its existing execution contract: load and execute a source in a separate execution context at that statement. Use `IMPORT` to share functions and globals. `INCLUDE` continues to use its own recursion limit, and imported sources can contain `INCLUDE` statements.

Preparation diagnostics identify the source and physical line that requested an invalid import or declared an invalid function. Runtime diagnostics inside imported functions identify the originating module and line. The existing fixed diagnostic buffer can abbreviate long source paths or messages.

## Execution APIs and snapshots

| Entry point | When imports are read | Lifetime of the prepared sources |
| --- | --- | --- |
| `am_exec`, `am_exec_source`, `am_exec_file` | Before that execution | One call |
| `am_program_open` | At open | Until `am_program_close`; stepping never reloads imports |
| `am_compile` | At compilation | Until `am_free_compiled`; repeated `am_exec_compiled` calls use the same snapshot |
| `amlc --scalar` executable | In its runtime constructor | One process execution |

The C APIs taking source text use the active source directory, or the working directory when the host has not supplied a source origin. `am_exec_source(script, source_path)` supplies an explicit root origin. Prepared resumable and bytecode programs retain absolute origins after the host changes its working directory. Changes or removal of imported files after preparation do not alter that snapshot. A later `INCLUDE` still reads its file when executed.

`amlc` embeds the root's AML program and prepares imports through `am_exec_source` at process startup. The root source file may be removed after compilation; imported files remain runtime inputs at their resolved paths. Imported files contain runtime AML. C extraction by the `amlc` frontend applies to its root input; `IMPORT` does not add C translation units to that frontend.

The bytecode path uses the prepared function table once and advances past already executed compound statements. Function definitions, `if`/`else`, and `while` therefore have the same statement boundaries as the interpreter.

## Bounds

| Resource | Current bound |
| --- | --- |
| Canonical source path | 255 bytes plus NUL (`AML_MAX_SOURCE_PATH`) |
| Imported file | 1 MiB |
| Source files per preparation | 64 including the root (`AML_MAX_IMPORTS`) |
| Nested import edges from root | 16 (`AML_MAX_IMPORT_DEPTH`) |
| Expanded executable/source-boundary lines | 4096 (`AML_MAX_LINES`) |
| Nonblank, noncomment line content after indentation | 255 bytes plus NUL (`AML_MAX_LINE_LEN`) |
| Function table | 128 entries, including 17 current registered builtins: 111 user definitions |
| Function name and parameter name | 31 bytes plus NUL |
| Parameters per function | 8 |

Whole-line comments and blank lines do not consume expanded line slots. Import boundaries do consume slots. The root `amlc` frontend accepts 4096 directives; imported runtime sources share the expanded 4096-line budget. These budgets fail explicitly instead of dropping trailing lines or definitions. The expanded budgets accommodate Haiku's combined memory, English form, and generator modules.

## Verification

```sh
bash tests/test_aml_imports.sh
```

The suite exercises nested and diamond imports, canonical deduplication through a symlink, ordered shared globals, cross-module function calls, imported function and worker origins, source diagnostics, resumable and repeated bytecode snapshots after file removal, and interpreter/scalar executable parity from another directory. Rejection cases cover malformed directives and declarations, missing/nonregular/NUL-containing sources, cycles, function collisions, and path/source/depth/line/function budgets. The executable failure cases check that neither preceding AML effects nor `BLOOD MAIN` run after preparation failure.
