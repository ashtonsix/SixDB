# Isolated current-root Engine memory, 2026-10-20

This profiles the SAME frozen graph7 geometry as the throughput screen, without
constructing an LMDB database in the process. It measures this directory plus
one64-byte material allocation/Object per directory entry, not a full Engine or
representative records-per-segment density. No historical cuts or writes remain
live. All source material/locator bytes, warm read outputs and final metadata
release checks passed.

The actual caller/build/inputs are `memory.cpp`, `prepare_memory.py`,
`memory-build.json`, `memory-inputs.json`, `memory-receipt.json`. Raw kernel
memory and allocator observations are `memory-{8192,262144}.csv/log`; curated
fields are `memory-summary.json`. `/proc/self/status`, `smaps_rollup` and
`mallinfo2` are read in-process at named boundaries. An instrumented external
default unsynchronized PMR pool records requested and upstream backing bytes;
this preserves the default pool policy with resource wrappers and one additional
empty internal pool bookkeeping object. It is a memory accounting probe, not a
timing. GuestCPU8/helper0, pinned Clang21.1.8, exact frozen fixture/core objects.

| Metric | 8,192 entries | 262,144 entries |
| --- | ---: | ---: |
| Configured empty process RSS | 3.66MiB | 3.70MiB |
| Warm process RSS, live external inputs freed | 9.21MiB | 176.61MiB |
| Warm process PSS | 7.03MiB | 174.42MiB |
| Warm RSS increase over configured baseline | 5.55MiB | 172.91MiB |
| Lifetime peak RSS incl source/setup | 14.19MiB | 211.83MiB |
| Warm process virtual address size | 166.39MiB | 4,737.50MiB |
| Logical PMR metadata requested live | 0.853MiB | 27.216MiB |
| PMR pool upstream backing live | 1.831MiB | 42.993MiB |
| Native Pool mapped-byte counter | 4.137MiB | 132.324MiB |
| Native Pool reserved address counter | 149.844MiB | 4,670.844MiB |

RSS includes the process's resident file/shared pages; PSS apportions shared
pages. Neither is the same as Pool mapped/reserved counters or active allocation
requests. PMR requested bytes are NESTED IN PMR backing, not additive to it.
The Native Pool metadata counter at262K is4.811MiB and is also not a whole-process
metadata measurement. These separate counters do not establish disjoint resident
sets and must not be summed into an RSS estimate.

At262K the requested native bodies are16MiB material,17MiB directory groups
(64B group header per16entries plus64B dirents) and0.291MiB for529graph bodies.
The same prototype uses32B Control,88B Format and368B graph ownership proof.
Current metadata, allocation geometry, prepared capacity, allocator retention and
one Object/native allocation per material entry substantially exceed body bytes.
That is a prototype/granularity trade-off, not proof of a minimum required by the
format. The measurements include one layer, not all base/summary layers, delta
buffers, query execution, history or a complete mixed producer.

Original132B source rows are freed after full-byte qualification, and the4MiB
request trace is freed after warming. No malloc_trim is invoked. At262K freeing
the trace transfers4,194,320B from malloc live to free arena while RSS remains
unchanged. Thus the warm figure includes about4MiB of freed caller trace retained
by glibc, alongside other real allocator caches; it is not physically input-free
RSS. At8K its trace mapping returns to the OS. Peak RSS is a lifetime setup/source
peak, not the warm backend's active bytes. After worker shutdown,262K process
RSS is13.61MiB with about9.95MiB free malloc arena retained; that is not a leak
claim or proposed production trim policy.

Source audit found a minor sscanf unit-validation issue: each rollup address
header also produced a bogus zero field. `memory-parser-note.json` records their
removal from the curated summary using known field names. Raw/source evidence
is unchanged; genuine RSS/PSS/virtual/peak fields were unaffected. No rerun was
needed. Loom's initial read-only PMR/oracle audit found no material defect and
reconstructed the native mapped/reserved geometry at both sizes; interpretation
is retained separately when delivered.
