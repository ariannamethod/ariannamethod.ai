# Living DOE sessions in AML

A program opens one parliament on a dense llama-family GGUF through the NoTorch
CPU Harness. Each execution context owns its host, KV cache, expert state and
pending consolidation. Functions within that context share the session.
Independent programs and spawned workers each open their own. A saved spore
carries expert state across those boundaries. Paths resolve from the source
file containing the call.

| Expression | Effect |
| --- | --- |
| `doe_open(host, spore, capacity)` | Open a host and a context of 1 to 1,048,576 positions, within the declared host limit. Use an empty spore string for new experts. |
| `doe_encode(text)` | Return token IDs from the GGUF tokenizer, including its BOS policy. Pretokenized hosts can take IDs directly. |
| `doe_prompt(ids)` | Start a turn with a nonempty token vector at the current KV position and return the full next-token logits. |
| `doe_logits()` | Copy the current full logit vector during an active turn. |
| `doe_choose(id)` | Record and forward this token, returning its ID. Use −1 for greedy selection. A stop token records the choice and closes generation for this turn. |
| `doe_finish(mode)` | Commit a turn after one or more choices. `'vote'` carries routing and history forward. `'learn'` also updates elected factors and lifecycle. `'async'` hands the same update to a background worker. |
| `doe_wait()` | Join a pending consolidation. The next prompt, save, transplant and close also wait. |
| `doe_reset()` | Start the next turn at KV position zero while keeping the parliament. |
| `doe_save(path)` | Publish the completed state atomically to a new GGUF filename. |
| `doe_transplant(donor_spore, slot)` | Copy just the A/B factors of the donor's matching live seat into the current completed parliament. |
| `doe_close()` | Discard this in-memory session and release the host. Also used to end a read-only probe after `doe_prompt`. |

Commands return 1 on success, except for the token and vector results above.
A backend error stops the program. Interpreter, bytecode, resumable programs
and workers drain pending work on teardown, including after errors. Explicit
save controls disk publication. An ordinary host using the C interface registers
the table with `am_set_doe_backend`. The NoTorch adapter supplies
`am_use_notorch_doe()` in `libaml_doe.a`.

## Build and run

Build [NoTorch with native consolidation](https://github.com/ariannamethod/notorch/commit/5f408730544f90e248915b6456252a72423b2068)
and set `NOTORCH_ROOT` to that checkout. For a portable CPU build:

```sh
make -C "$NOTORCH_ROOT" lib lib_harness BLAS_FLAGS= BLAS_LIBS=
make harness NOTORCH_ROOT="$NOTORCH_ROOT"
./runner/aml-harness conversation.aml
```

```aml
doe_open('model.gguf', '', 256)
doe_prompt(doe_encode('Every night the keeper carries a'))
chosen = doe_choose(-1)
doe_finish('async')
doe_save('keeper.gguf')
doe_close()
```

Supply `NOTORCH_LDFLAGS` when using a BLAS-enabled NoTorch build. For installed
archives, `make install-harness` installs both AML bindings and runners.
`amlc` discovers the DOE binding alongside the Harness and NoTorch archives in
`AML_PREFIX/lib`. A standard AML build continues to run its existing programs.

## Reproduce the retention experiment

Use the [SmolLM2-360M-Instruct Q4_K_M GGUF](https://huggingface.co/bartowski/SmolLM2-360M-Instruct-GGUF/blob/7be6f65f1db715fe5dc5a4634c0d459b4eed42ec/SmolLM2-360M-Instruct-Q4_K_M.gguf).
From the AML root on Linux, with the absolute model path in `MODEL`:

```sh
printf '%s\n' "$MODEL" "$PWD/retention-output" | taskset -c 2 ./runner/aml-harness examples/doe/retention.aml
```

The script checks the host SHA-256, claims a new directory, creates both
parents and grows four branches. The ten distractors and their newline
continuations are identical across branches. Each distractor and measurement
starts with a fresh KV. The voting controls inherit the learned parent, then
carry elections and oscillator history without further Oja updates. A probe
opens a saved generation, optionally transplants one seat, reads logits and
closes. `scores.tsv` gives both full-vocabulary probabilities and log odds.
`contrast.tsv` subtracts the bell-history margin from the key-history margin.
`seats.tsv` subtracts each recipient's own margin from its single-seat
transplant on probe zero. `turns.tsv` records the raw forced-token logits.
