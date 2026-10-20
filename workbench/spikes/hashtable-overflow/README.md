# Overflow tags and continuation

Can eight-bit fingerprints and filtered continuation improve Ikea's Swiss
overflow without changing its primary path? Ashton requested actual Boost and
Abseil controls, independent hot/cold Zen 5 throughput, whole-table reads/writes,
and then higher Swiss occupancy. This is isolated research, not a promotion.

See [results and decision](RESULTS.md). The production headers are unchanged.
Multimaps, K-way merge and Loom scheduling are outside this experiment.

## Treatments

| Owner | Swiss metadata | Deletion | Swiss ceilings |
| --- | --- | --- | --- |
| baseline | 16 seven-bit tags/16 bytes | Tombstones | 87.5% |
| tag8 | 16 tags/16 bytes; 254 occupied codes | Tombstones | 87.5%, 93.75%, 96.875% |
| filter15 | 15 tags + eight-class continuation byte/16 bytes | Empty slot + conservative continuation; erasure budget | Same three |

**Swiss compares 128 bits: 16 or 15 fingerprints, including in AVX2/AVX-512
builds.** Wider vector code serves the primary bucket. Comparing 32/64 bytes is
a separate experiment; combining logical groups must respect earlier groups'
termination conditions.

`overlay.py` derives checked private deltas from captured production headers;
generated copies remain in the build directory. Eight-bit tags are calculated
only after routing to Swiss, preserving primary read/upsert instructions. The
filtered candidate uses an independent hash byte for continuation classes and
explicitly bounds a probe to one traversal of the table, including when stale
continuation bits all remain set. Its guarded publication APIs are removed:
this spike qualifies concrete exclusive owners, Swiss growth, clone, RMW and
staged reads, not descriptor primary growth/merge or MVCC publication.

## Run and recover

Use the pinned Linux toolchain; prefix commands with `orb -m ubuntu` from macOS.

```sh
python3 workbench/tools/worker.py run workbench/spikes/hashtable-overflow/density.sh \
  --machine zen5 --instance-type c8a.2xlarge --capacity on-demand \
  --deadline 3600 --env SIXDB_BUILD_JOBS=1 --detach
```

The runner builds incrementally, checks primary codegen and behavior, then runs
405 sequential callers with rotated treatment order. `run.py` can also run a
primary-occupancy panel without `--density`. The density panel targets three
Swiss populations to cross the different owners' capacity boundaries. Its raw
`load=85` is an unused command default: use actual `n`, primary capacity,
`swiss_slots` and `swiss_load`, not that label.

Retained evidence includes all competitors and negative cases. Full raw
repetitions, per-round maintenance costs, binaries, sources and emitted assembly
are in its recoverable S3 bundles. For example:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/hashtable-overflow/evidence/density-main build/overflow-recovered
python3 workbench/spikes/hashtable-overflow/audit.py build/overflow-recovered --levels 925,980
python3 workbench/spikes/hashtable-overflow/summarize.py build/overflow-recovered \
  --output build/overflow-report
```

`cases.csv` contains per-cell medians/MAD and physical capacities; regenerated
`churn.csv` includes every erase/refill maintenance pulse.

References: [Boost layout](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered/structures.html),
[implementation analysis](https://bannalia.blogspot.com/2022/11/inside-boostunorderedflatmap.html),
[maintained Ikea comparison](../../benchmarks/hashtable/README.md).
