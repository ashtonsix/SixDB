# Spatial placement with the same consumer

Exploratory native fixture for the [layout-analyser spatial question](../experiments.md#add-mechanisms-only-to-discriminate-a-live-explanation).
Every organization represents the same 96B logical row: a 64B core and a 32B
extension. This is a read consumer over scalar u64 fields, not an Engine
interface or an Ikea layout selection result.
The [local validation record](LOCAL.md) preserves the initial checks. Subsequent
server comparisons and their receipts are in the parent spike's
[findings](../findings.md#spatial-placement-is-a-contextual-consumer-question)
and [evidence guide](../evidence/README.md).

| Organization | Physical storage per row | Extension location |
| --- | ---: | --- |
| `split64+32` | 64B core + 32B extension | Separate dense plane |
| `dense96` | 96B | Immediately after core |
| `padded128` | 128B including 32B padding | Immediately after core |

Padding/alignment phases are explicit. `padded128` describes its stride; a
nonzero base phase deliberately tests a shifted placement. Split planes have
separate mappings and recorded base addresses. Allocation includes both planes
even when only cores are requested. Page rounding and guard-page address space
are reported separately from logical bytes.

## Consumer and comparisons

All twelve words have deterministic seed/row/field values, except the first
core word holds an encoded next-row ID. This control word is part of the 64B
core read and result. One word also carries an extension selector. Exactly one
row per group of eight requests its extension in the sparse arm; the chosen
position varies with seed, avoiding a fixed correlation with the alternating
line phase of dense96 rows. This is stratified one-in-eight selection, not IID
Bernoulli selection or a model of clustered predicates.

The generated random row permutation forms one closed dependent cycle. K=1,
8 or 32 independent positions start evenly around that cycle. Advancing each
position N/K steps visits every logical row exactly once across the positions;
next-row discovery actually depends on the loaded encoded ID. Repeated passes
preserve this coverage. The cycle and data stay identical across K and layouts.
This measures independent requests, not an exact count of outstanding loads.
A noncryptographic order fingerprint accompanies each sample, so equal seeds
from different standard-library implementations need not be assumed to have
produced the same shuffle. Source/executable hashes provide provenance separately.

Each request consumes every core word, and either no extension, one eighth of
extensions, or every extension. Rotations/additions produce an observable u64
result. Each complete pass is checked against generated logical words, including
after timing. Every stored word and every cycle link is validated before timing.
K loops are specialized/unrolled; their register spills, address arithmetic,
branches and bookkeeping are charged. There is no per-record timestamp or fence.

`--compute N` adds result-dependent integer arithmetic, consumed by the result,
between the core expression and extension expression in source. It is useful
consumer work, not a calibrated delay: compiler and hardware scheduling may
overlap independent loads with it. `--prefetch-extension` issues an optional
software prefetch for a demanded extension before that computation. These are
explicit follow-up knobs; the default screen varies neither. `--ordered` uses
sequential logical IDs with the same fields and consumer instead of the random
cycle. Explicit arbitrary-distance lookahead and cross-core consumers are not
implemented here.

One layout is allocated at a time. Candidate order and case order are shuffled
within repetitions, sequentially on one pinned CPU. Every case makes a full
untimed pass before a timed batch. That pass estimates the number of full passes
needed for the requested duration. Short batches increase their full-pass count
up to eight adjustments/100,000 passes; actual elapsed time, requested duration
and adjustment count are reported. Calibration has no hard deadline. Construction,
validation, allocation and the warm pass
are outside steady-state timing; their commands' total costs remain in `run.json`.
The temporary permutation is released before timing. Different physical byte
footprints at the same logical row count are an intended comparison outcome.

Modeled line/page demand is the union of byte-address granules within each
operation, summed over operations. It does not count cache misses, physical
traffic or distinct lines across the batch. For example, at phase zero:

| Organization and access | Modeled 64B lines/op | Modeled 128B lines/op |
| --- | ---: | ---: |
| dense96 core only | 1.5 | 1.25 |
| dense96 full row | 2 | 1.5 |
| padded128 full row | 2 | 1 |
| split64+32 full row | 2 | 2 |

An independent byte-enumeration check covers shifted bases, both line sizes,
and split/contiguous range unions. Sparse demand can vary slightly with seed
because selected rows have different phases; raw samples and modeled ranges
preserve that variation.

## Build and run

The parent spike integrates `add_subdirectory(spatial)`; the only native target
is `layout_spatial_probe`. Use the [pinned Linux toolchain](../../../../BUILDING.md).
Prefix commands with `orb -m ubuntu` from macOS.

```sh
python3 workbench/spikes/layout-analyser/spatial/run.py --profile smoke
```

The runner uses the existing source-capture/experiment helper and its own
`build/workspaces/layout-spatial` workspace. It builds only this target, runs
native and accounting checks, then two small footprints with two seeds and
phases 0/32. Its output lives in ignored `build/experiments/layout-spatial/`.
Use `--workspace PATH` to separate independent runs; the helper serializes a
shared workspace. Linux hardware discovery is reused read-only from
[memory characterisation](../../memory-characterisation/README.md).

The fleet coordinator can run this script on an already chosen worker:

```sh
bash workbench/spikes/layout-analyser/spatial/run.sh
```

It uses `SIXDB_CPU` and writes under `$SIXDB_RESULTS/spatial`. It provisions
nothing. A captured source must include this directory, the parent's integration
CMake, the root CMake/cmake helpers, `workbench/tools/experiment.py` and its
capture/receipt dependencies, and memory characterisation's `hardware.py`.
Ordinary SixDB captures already include these. The runner retains its binary,
compile commands, compiler cache, source digest and commands in the full bundle.

`--profile screen` uses 128-row hot controls and a larger logical footprint
chosen from reported cache capacity, two seeds, three repetitions, phases
0/32/64/96, and all three K values/access modes. `--large-mib` overrides the
logical 96B-row footprint; the runner records the actual rounded row count.
The automatic footprint aims for core-only bytes at four times the largest
reported data/unified cache but caps logical bytes at 512 MiB. If the cap makes
this smaller, the recorded ratio exposes it. Capacity alone establishes no
residency claim. The local ARM VM's reported 128B lines are used in its model;
its results must not be relabeled as 64B-line server evidence.

Allocations request base pages with `MADV_NOHUGEPAGE` and are first-touched on
the pinned CPU. `placement.txt` records relevant `smaps` and `numa_maps` fields
before/after each layout. No explicit NUMA binding or global policy change is
made. A missing NUMA receipt is recorded as unavailable (the local VM has no
`/proc/self/numa_maps`), rather than attributed to a node. Reported CPU topology
and receipts accompany results; sibling/background
load is not controlled by this fixture and should be coordinated externally.

`samples.csv` keeps every repetition, seed, phase, request condition, allocation,
counter and checksum. `report.py RUN_DIRECTORY` generates a conditional summary
and JSON; it never turns normalized adjacency savings into a whole-row cost.
The runner selects report inputs/placement receipts with the standard artifact
helper, ready for the coordinator to retain a useful comparison. Raw timing
files/binaries remain in the full run. No cloud result is implied by local checks.
