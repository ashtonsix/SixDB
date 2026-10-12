# Prior art and comparisons

Read the mechanisms, then reproduce comparisons under this spike's contracts.
Different hashing guarantees, ownership, value lifetimes and mutation semantics
matter as much as a favorable lookup number.

| Source | Useful question |
| --- | --- |
| [Abseil Swiss Tables](https://abseil.io/about/design/swisstables) | Strong default set/map baseline; full-hash entropy for location and fingerprints |
| [Folly F14](https://github.com/facebook/folly/blob/main/folly/container/F14.md) | Fourteen-entry SIMD chunks, overflow bookkeeping, small-table and value-layout alternatives |
| [Boost concurrent_flat_map](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered/reference/concurrent_flat_map.html) | Safe concurrent visitation and bulk independent lookup; a competitor to Ring throughput |
| [libcuckoo](https://github.com/efficient/libcuckoo) | Concurrent cuckoo hashing and resize |
| [IcebergHT paper](https://arxiv.org/abs/2210.04068), [implementation](https://github.com/splatlab/iceberghashtable) | Two-tier locality, concurrency, resize; distinguish its guarantees from a simple affine permutation |
| [Iceberg hashing quotienting](https://arxiv.org/html/2109.04548v3#S3) | Which bits the address can replace, and the assumptions behind the theoretical bounds |
| [Thorup, High Speed Hashing](https://courses.cs.washington.edu/courses/csep521/21wi/readings/Thorup.pdf) | Random multiply-shift guarantees versus fixed-multiplier intuition |
| [Vectorized hash tables across CPU architectures](https://anakli.inf.ethz.ch/papers/vectorized_hash_tables_vldb23.pdf), [artifact](https://github.com/hpides/vectorized-hash-tables) | Explicit vector probing, fingerprints, buckets, architecture/load/value/skew sensitivity; thread-local tables are not shared-writer evidence |
| [Compact sparse hash](https://github.com/tudocomp/compact_sparse_hash) | Integer-width/quotient storage competitor |
| [Fast and Simple Compact Hashing via Bucketing](https://koeppl.github.io/bin/paper/algorithmica22fast.pdf), [implementation](https://github.com/koeppl/separate_chaining) | Packed integer buckets and explicit AVX2 alternatives |
| [unordered_dense](https://github.com/martinus/unordered_dense) | Dense element array as a different lookup/iteration/memory tradeoff |
| [ClickHouse RowRefs](https://github.com/ClickHouse/ClickHouse/blob/master/src/Interpreters/RowRefs.h) | Tagged inline singleton versus duplicate-value lists and contiguous ranges |
| [DuckDB linear-chained joins](https://duckdb.org/library/factorization/) | Separate duplicate groups from unrelated hash collisions; build/probe contract differs from mutable multimaps |

The old local [unified quotient experiment](../../../../calico-simple-containers/flatmap16-prototype/experiments/unified_quotient/README.md)
explores tiny lists, SIMD-density cliffs, overflow filters, churn and fused batch
consumption. Its 16-bit Apple-only results do not establish this brief's targets:
the scalar loop was independent throughput, memory accounting mixed bytes with
Abseil capacity, and the large Ring experiment overprovisioned buckets without a
comparable Abseil-prefetch arm. Preserve the questions, rerun the evidence.

[Calico QHash](../../../../calico/qhash/REPORT.md) provides useful failures as well
as clustered-key wins: random-word keys and point latency lost, and publication
coverage was incomplete. Its representation restrictions do not carry forward.
The [SixDB bucket consumer](../layout-analyser/bucket/README.md) already stresses
SeriesPack/TuplePack composition and records the repeated-tile placement gap.

Before measurement, retain exact source revisions for selected implementations;
these links identify projects and papers, not frozen benchmark inputs.
