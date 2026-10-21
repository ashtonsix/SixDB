# Object integration and historical MVCC investigations

[Outcomes and cross-module lessons](OUTCOMES.md) is the short starting point.
Use the [performance map](performance/README.md) for particular measured
operations and negative results, [metadata map](metadata/README.md) for state,
ownership and ABI evidence, and [earlier prototypes](history/README.md) for
the retained selection/conversion falsifiers and exact source recovery.

Stage 2 implementation and comparative benchmarking are complete. The two
disposable drivers and rejected prototype matrices are retired. Start with
[outcomes and handover](OUTCOMES.md) for findings, consequences and remaining
questions, and the [retirement guide](../../benchmarks/retired-drivers.md) for
the retained check set and historical source recovery.

Current checks exercise production ObjectStore through actual Ikea formats and
Loom lifetimes. They retain catalog/Shape/bounded-reclamation histories,
publication with old readers, count-changing append/conversion, fused-read
authorization, Ring cancellation/reset and physical pack growth. Their private
codec/trace/runtime fixture stays local to those correctness checks.

```sh
orb -m ubuntu cmake -S . -B build/orbital-objects -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=orbital-mvcc
orb -m ubuntu cmake --build build/orbital-objects --target orbital_mvcc_metadata_validate -j1
```

`orbital_mvcc_validate` is a compatible entry point for that same retained set.
It no longer builds rejected implementations or LMDB. `orbital_validate` and
`loom_validate` separately own module behavior. The
[production module](../../../orbital/README.md#embedded-objects) and
[specifications](../../../orbital/spec/README.md) own the interfaces and obligations.

Historical [performance feedback](performance/FEEDBACK.md),
[baseline findings](performance/FINDINGS.md) and [metadata comparisons](metadata/README.md)
keep useful comparisons and rejected alternatives readable. Their code is
recoverable from Git and measured source bundles. The withdrawn history also
has a verified [bundle reference](baseline/history-artifact.json); its Git bundle
needs ancestor `8412e5c`. Historical results retain their original qualification.
