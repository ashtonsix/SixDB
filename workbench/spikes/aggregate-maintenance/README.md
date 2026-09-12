# Aggregate maintenance

**How can higher strata retain useful per-field `min`, `max`, `count`, and
`sum` summaries without every mutation causing expensive ancestor writes?**
This spike concluded on 2026-09-07 with two executable count/sum probes.
Start with the [conclusions and limits](CONCLUSIONS.md) for the combined result
and useful reasons to return. The core structure and maintenance policy remain
open; the code and evidence below preserve the investigation.

The [design notes](design.md) develop the semantics and alternatives;
[literature](literature.md) connects relevant mechanisms, and
[experiments](experiments.md) records questions worth testing.
Ashton's [shared buffer and dirty-prefix filter](shared-dirty-buffer.md)
is a lighter candidate, examined for contention, cache traffic, and read-driven
flushing after the first measurements.
Its [brief executable comparison](dirty-buffer/README.md) found a favorable
region for contended hot locations, and a cheaper no-filter buffer comparand.
The first executable probe below asks how coalescing changes logical count/sum
work and the CPU cost of applying and querying corrections. Its CMake targets
and `aggregate-deltas-v1` measurement identifier name that particular probe.

[First findings](FINDINGS.md) and [retained evidence](evidence/local-arm-20260907/README.md)
are available. [Research-experience notes](../../notebook/research-experience.md)
record what using this probe changed about the workbench.
The separate [sketches, filters, and histograms note](../../notebook/secondary-summaries.md)
keeps their different contracts open. A related
[notebook thought](../../notebook/ideas.md#when-less-logical-work-costs-more-cpu)
connects the first surprising result to broader construction-cost questions.

## Run and iterate

On Linux, from the repository root:

```sh
python3 workbench/spikes/aggregate-maintenance/run.py
python3 workbench/spikes/aggregate-maintenance/run.py --profile screen
python3 workbench/spikes/aggregate-maintenance/run.py --profile screen --case hot --case uniform
```

From this macOS workspace, prefix the command with `orb -m ubuntu`. The default
is a small smoke profile. `screen` uses larger working sets and 14 deliberate
scenarios. The runner prints a new result directory under
`build/experiments/aggregate-maintenance/` and never overwrites an earlier run.
`--cpu`, `--repetitions`, and `--min-time` change measurement controls.

One invocation refreshes the stable editor configuration for active studies,
then incrementally configures/builds this study's independent benchmark build,
checks the models against an oracle, collects deterministic accounting, runs pinned
sequential Google Benchmark repetitions, and generates tables. Build and
check failures leave their logs and a failed receipt. An empty selection,
missing repetition, oracle mismatch, or source change prevents a successful
result. Google Benchmark is fetched by immutable revision and archive hash;
subsequent builds reuse it. Running an experiment stays local; retention is
an explicit follow-up command printed by the runner.

For editor support, [activate the study](../../tools/editors.md) with
`python3 workbench/tools/dev.py --add aggregate-maintenance`.

The result directory contains `summary.md`, `summary.csv`, `accounting.csv`,
every raw timing repetition in `benchmark.json`, command logs, compiler flags,
hardware information, binaries, and a `run.json` receipt with source/artifact
hashes. `source.tar.gz` includes tracked and untracked non-ignored source files,
excluding `build/` and spike evidence, so an uncommitted experiment can still
be reconstructed without recursively capturing earlier results.
Keep source unchanged until a run completes. Use the
[retention command](../../tools/artifacts.md) to upload and verify the full
bundle and keep compact evidence beside findings. The analyzer also accepts
that compact directory, with no download needed.

For direct C++ iteration:

```sh
cmake --build build/clang/aggregate-maintenance --target aggregate_deltas_check
build/clang/aggregate-maintenance/workbench/spikes/aggregate-maintenance/aggregate_deltas_check smoke
ninja -C build/clang/aggregate-maintenance \
  workbench/spikes/aggregate-maintenance/CMakeFiles/aggregate_deltas_core.dir/model.cpp.o
```

`model.cpp` is compiled once into a library shared by the check and benchmark
drivers. Scenario definitions live in `cases()` there; the two drivers use the
same generator and mechanisms. `analyze.py RUN_DIRECTORY` regenerates tables
without recompiling or rerunning measurements. Interpretation belongs in
findings, rather than in the table-generating script.

## What is compared

All methods maintain signed 64-bit count and sum pairs over a dense abstract
hierarchy. Fixtures use bounded integers and key-presence transitions through
insert/delete operations. Leaves and each configured ancestor stratum have
summary cells; geometry uses powers of two and cheap prefix shifts.

| Method | Batch work | Query correction |
| --- | --- | --- |
| `eager` | Apply every mutation to each covering summary cell | None |
| `eager_batch` | Coalesce leaf deltas, expand affected ancestors, coalesce again, apply to base | None |
| `ancestor_runs` | Coalesce leaves; create sorted ancestor-addressed runs within spatial partitions | Consult pending runs by the query's disjoint ancestor cover |
| `location_runs` | Coalesce leaves; create sorted location-addressed runs with prefix sums within spatial partitions | Two boundary searches per intersecting run |

Both buffered methods maintain one pending total per affected partition/batch.
A query covering a whole partition consumes that total; boundary partitions
consult their runs. A configurable run-count threshold folds a partition into
the base. Folding location runs coalesces leaves before expanding/coalescing
ancestor updates. Every run is drained at the end, including a partial final
buffer. There are no intermediate run-to-run merge levels in this first probe.

This isolates spatial partitioning, temporal batch coalescing, and query
geometry. It exposes whole-range fan-out across partition totals, while giving
both buffered designs the same simple shortcut for wholly covered partitions.

## Scope of the evidence

- A batch becomes visible as a whole, and queries read the current state after
  it. Individual commits inside a batch and retained historical snapshots are
  not modelled. This is a visibility constraint, not an MVCC implementation.
- The timed cycle includes batch processing, sorting, allocation during those
  operations, queries, threshold folds, and final draining. Creating the empty
  base and partitions, trace generation, and the row oracle are excluded.
  Mutation deltas/beforeimages are precomputed; primary rows are not updated
  during the timed cycle. This measures the summary mechanism only.
- Google Benchmark receives manual elapsed wall time for that cycle. Its
  `real_time` field is the relevant measurement; `cpu_time` also sees excluded
  setup and should not be substituted. Reported ns/mutation includes queries
  and maintenance; it is not isolated insert latency.
- Accounting uses a separate instantiation of the same code. Timing omits the
  counters. Every timed query is still checked against its precomputed oracle.
- `foreground_entries` counts logical contributions emitted into scratch or
  direct updates, including initial leaf coalescing inputs. It is not a count
  of records sent to storage. `base_updates` counts summary-cell adjustments.
  Subtract `leaf_base_updates` to isolate adjustments at higher strata.
- Pending payload bytes count allocated entry/prefix capacities. Partition
  metadata and the dense base are separate. Largest scratch capacity is the
  largest single temporary entry vector, not simultaneous peak RSS; vector
  control blocks, allocators, and allocator high-water retention are not all
  priced by these counters. The receipt's child RSS peak covers the whole run,
  including build/check, and is not a per-method memory measurement.
- No compression, disk, WAL, replication, concurrency, min/max repair,
  retained history, or arbitrary filtered SQL aggregates are measured here.
  Per-field NULL distinct from row deletion and numeric overflow are also
  outside this bounded count/sum fixture.
  Thus fewer base updates cannot be reported as a physical write-amplification
  reduction. Cache-residence tiers are not established by this probe.

Mutation/query randomness is independent. Configurations that change query
load, batching, partitioning, or hierarchy depth verify an identical mutation
digest. Each scenario records its seed and geometry. The correctness driver
also checks every half-open range of a small universe, including empty ranges,
across multiple fanouts, partitions, run thresholds, and seeds.
