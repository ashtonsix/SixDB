# Packed hash tables

Research for SixDB sets, maps and multimaps composed from SeriesPack, TuplePack
and StreamPack, with Orbital ownership and Loom execution. Keys span
8/16/24/32/40/48/56/64 bits; values are opaque fixed-width records of 1–64 bytes.
The selected architecture is the bucket hierarchy. This is the single top-level
hashtable spike. The [separated-layout experiment](SEPARATED-EXPERIMENT.md) is
archived; older benchmark controls live in [reference/](reference/README.md).

**Research is consolidated. No new experiment campaign is queued.**
The evidence supports useful cold-throughput and narrow-memory advantages, but
not broad superiority over Abseil. Wide multimaps, compact lookup and some merge
histories remain material weaknesses. A few hot-latency nanoseconds may be an
acceptable trade for throughput and memory; larger losses remain explicit.

Read these in order:

1. [Design and implementation direction](DESIGN.md): the structure, operation and ownership boundaries, and choices made.
2. [Current absolute scorecard](RESULTS.md): latency, reciprocal throughput and memory, with each measured configuration identified.
3. [Reflection and decisions](RECALIBRATION.md): lessons from the separated experiment and the resumed work.
4. [Graduation assessment](GRADUATION.md): what is established and what still needs resolving.
5. [Working account](STATUS.md): completed runs, recovery references and current scope.

| Detail | Owning research |
| --- | --- |
| Measurement correction | [Validation and lifetime](BENCHMARK-VALIDATION.md) |
| Cold reads, caller heat and protection | [Read windows](READ-WINDOWS.md), [staged consumers](STAGED.md), [Loom execution](../../../loom/docs/execution.md) |
| Slot geometry and growth | [Capacity families](CAPACITY-FAMILIES.md), [growth cycles](GROWTH-CYCLE.md), [Worker formats](worker-epochs.md#solver-sized-formats-in-the-complete-worker-caller) |
| Small owners and batch operations | [Small-owner lifetime](small-growth.md), [hierarchy batches](hierarchy-batches.md) |
| Values and external bags | [Value widths](value-widths.md), [bag policies](bag-policies.md), [vectors/buffers](multimap-vectors.md) |
| Multiple writers and consolidation | [Worker epochs](worker-epochs.md), [actual Worker folding](worker-fold.md), [earlier consolidation](CONSOLIDATION.md) |
| Primitive composition | [Value consumers](value-consumers.md), [code inspection](codegen.md), [lookup consumption](lookup-consumption.md) |
| Older attribution and scorecards | [Historical results](RESULTS-HISTORICAL.md); superseded lifetime claims are marked |

The `evidence/` receipts retain selected data and recovery recipes. Full source
captures, binaries, assembly and raw repetitions are in their verified archives.
Do not combine one configuration's timing with another's memory or treat an
OrbStack diagnostic as a native result. Follow the [build guide](../../../BUILDING.md)
and [measurement conventions](../../benchmarks/README.md); use pinned Linux
Clang 21 through `orb -m ubuntu`. Select `-DSIXDB_SPIKES=hashtable`; existing
`hashtable_design_*` target names are retained for continuity with the measurements.
Consolidation does not promote the prototype into production.
