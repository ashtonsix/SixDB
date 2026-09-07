# Shared dirty-buffer probe

Tests [Ashton's candidate](../shared-dirty-buffer.md): keep a collection-wide
append buffer and cheaply identify summaries affected by pending changes.
[Findings](FINDINGS.md) and [retained evidence](evidence/local-arm-20260907/README.md)
are available. This is a small in-memory mechanism experiment.

## Run

On Linux from the repository root, with at least five permitted CPUs:

```sh
python3 workbench/spikes/aggregate-maintenance/dirty-buffer/run.py
```

From macOS, prefix with `orb -m ubuntu`. The default selects a short comparison
of small, dispersed, and hot-location cases with four writers. `--case hot`
limits the scenario; `--filter` accepts Google Benchmark's name regex. Names
look like `hot/t4/word_check_span/manual_time`. `--filter .` includes everything,
including one-writer controls. Use `--filter-kib 8` or `32` to change filter size.
`--min-time` and `--repetitions` control sequential measurement.

The retained ARM runs explicitly used `--march armv8-a+lse`, supported by the
local guest. The default leaves ISA selection empty; pick an ISA appropriate
to the measurement host. All builds use the pinned Clang and generic tuning.

The runner refreshes the shared editor database, builds only
`dirty_buffer_probe`, checks the models, measures, and generates tables under
`build/experiments/aggregate-maintenance/dirty-buffer/`. The source snapshot,
compiler flags, commands, raw repetitions, accounting, and binary accompany
that result. `python3 .../dirty-buffer/analyze.py RUN_DIRECTORY` regenerates
its tables. `cmake --build build/clang/dirty-buffer --target dirty_buffer_probe`
supports direct incremental iteration; this probe is one independently compiled
TU. The earlier aggregate-delta implementation is not rebuilt by this target.

The runner prints a [retention command](../../../tools/artifacts.md) for chosen
results. It uploads and verifies the complete bundle, then keeps compact
samples, counters, tables, and provenance in Git. The analyzer can regenerate
tables directly from this compact evidence without fetching the bundle.

## Comparands

| Method | Work before batch-boundary reads |
| --- | --- |
| `eager_plain` | Direct count/sum additions; one writer only |
| `eager_atomic` | Relaxed atomic additions to all nine summary levels |
| `eager_striped` | Per-writer root and first-byte partials, shared atomic deeper summaries |
| `scatter_record` | One append reservation per record, four scattered filter bit probes per prefix, unconditional atomic OR |
| `word_or_record` | Four-bit masks within a word, unconditional atomic OR, record reservation |
| `word_check_record` | Read-before-OR word masks, record reservation |
| `word_check_span` | Read-before-OR masks, 64-record producer spans in one buffer |
| `exact_top_span` | As above, with an isolated exact first-byte bitmap |
| `read_flush_span` | Word masks/spans; flush on the first positive read instead of scanning |
| `append_scan` | Spans, no filter; conservatively scan pending changes for every query |
| `append_flush` | Spans, no filter; flush on the first read |

Buffered methods replay every update to every ancestor before the epoch ends.
There is no sorting, coalescing, compression, or unpaid maintenance. Dirty reads
otherwise scan the buffer separately per requested summary. Read-triggered
flush is deliberately the extreme threshold of one positive read; this does
not implement an adaptive pressure policy. Capacity-triggered flush happens
after the epoch's queries if they did not already trigger it.

## Measurement contract

Each mutation supplies precomputed signed count/sum deltas from a row history
containing inserts, replacements, and deletes. A whole batch is visible at
once; writers do not update primary rows or acquire beforeimages in the timer.
Ancestor identities are precomputed for all methods. Keys are 64-bit values,
and summaries cover root plus prefixes at 8-bit strides. Tests use exact,
bounded integers and one field; no min/max, NULL, MVCC, or durability model.

The small case has 1,024 possible keys; other cases have 65,536. Hot cases
choose among 64 of them. Default batches contain 1,024 updates and four queries
(root, first-byte, two-byte, and full-key summaries). `hot4096` uses batches of
4,096. `points` and `hot_roots` use 64 point/root queries respectively. Each run
has eight epochs. Changing locality/batch size also changes realised mutation
mix and total work; compare implementations within a row, not a causal cache-
or batch-size effect inferred from different rows.

Summary cells are spaced 128 bytes apart, matching this guest's reported line
size. The allocated base is about 0.91 MiB for the small universe and 53.08 MiB
for the large one. The hot cases only touch a small subset of the large base.
This isolates summary slots but deliberately enlarges their footprint; it is
not a selected SixDB representation or an established cache-residence tier.
The base is cleared outside manual timing before each iteration. This warms
memory and is expensive for the large allocation, even in hot cases.

One writer runs directly on guest CPU 0. Four persistent workers use CPUs
0–3; their coordinator uses CPU 8 here. Per-epoch barrier/dispatch overhead is
included in the four-writer write phase, equally across comparands. Separate
atomic count/sum updates are safe for these quiescent reads, not atomic pair
publication for arbitrary concurrent readers. Span reservations have no holes
in these divisible batches; stalled-writer publication is not modelled.

The timer includes writes, queries, full replay, and reset. It excludes thread
creation, empty-state initialization, trace generation, and full-state checks.
Phase counters average all timed iterations; manual `real_time` is the cycle
measurement. Google Benchmark's `cpu_time` includes excluded setup and is not
substitutable. Reported ns/update includes the configured reads and maintenance.

The accounting instantiation checks every marked prefix after each writer phase,
every query against an independent afterimage row oracle, and all final summary
cells. Timed instantiations omit accounting but still check every query. OR
counts can vary with races between first setters; they come from one accounting
run, not deterministic hardware-event counts. All cases have identical input
and query checksums across methods. Cache-miss PMU events were unsupported in
this VM, so contention causes remain interpretations of software timings and
operation counts. These are exploratory ARM-VM results, not Zen5 conclusions.
