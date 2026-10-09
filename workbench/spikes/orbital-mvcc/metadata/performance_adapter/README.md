# Actual compact read adapter qualification

`check.cpp` exercises the performance harness's `Candidate` and
`CandidateBodies` directly for Store8, Store16 and Store32. It adds no second
runtime or acquisition adapter. Point hits, misses, dependent chains and scans
are checked against the independent ordered-map oracle. Both the ordinary body
and the fused consumer use the same encoded Tuple data as the harness.

The check verifies stationary, noncopyable and nonmovable caller acquisition,
active scope counts, no PMR allocations during acquisition or read binding,
exact independent published bytes, and late discovery through an old scope
after a real replacement. Replacement is test scaffolding: it mutates the actual
Store through the benchmark's const catalog accessor, publishes a newly encoded
owner, and checks that old/new callers see different independent expected values.

The output records complete candidate PMR live/peak/allocation counters, history
and descriptor capacities, Arena charge, and resident caller/type sizes.
They are different categories, not a summed memory or stack estimate. In
particular, the Arena can retain idle backing after a version is reclaimed.
Corpus Schema/images/rows/encoded bytes and controls, physical Arena/Pool
metadata, allocator overhead and the replacement-only test backing/control
remain explicitly external to the candidate PMR counter. Small node or handle
sizes do not describe total metadata.

The helper captures source bytes into a private incremental workspace before
building. Its receipt records the exact source digest and executable hash, so
concurrent adapter changes cannot silently qualify a different revision.
It produces correctness and accounting evidence only; it does not run benchmark
clocks or establish a performance result. The normal CMake target is
`orbital_mvcc_actual_adapter_check`, also included in
`orbital_mvcc_metadata_validate`; the [metadata guide](../README.md#evidence-and-executable-probes)
shows focused build/check commands. The private runner below additionally
captures a source cut and qualification receipt.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/performance_adapter/check.py \
  --output build/orbital-mvcc/metadata-adapter/NEW-native
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/performance_adapter/check.py \
  --sanitize --output build/orbital-mvcc/metadata-adapter/NEW-sanitize
```

This check uses eight Objects, matching its original captured harness. The live
driver now permits larger catalogs and uses caches bounded by offered work;
this older check does not independently qualify that broader population.

The captured 2026-10-09 native and full ASan/UBSan runs passed 576 independent
read outcomes, six bounds refusals and three old-scope replacement histories;
qualification/accounting JSON matched exactly. Receipts are in ignored
`build/orbital-mvcc/metadata-adapter/qualification-{native,sanitize}-1/`.
The imported PMR resource gained explicit deleted copy/move members between
captures; the actual adapter, descriptor implementation, read kernels and check
source were identical. These receipts qualify those captured controls, not
subsequent changes to the live experiment.
