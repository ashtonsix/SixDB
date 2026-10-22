# Overflow tags and continuation

Completed comparison of eight-bit tags, filtered continuation and Swiss density.
Eight-bit tags are now promoted into Ikea at the unchanged 87.5% ceiling;
filtered continuation and higher ceilings were not promoted. The completed
[32/64-byte comparison](../logical-groups/README.md) retains 16-byte probes.

See [results and decision](RESULTS.md). Those measurements describe the captured
pre-promotion implementations. Multimaps, K-way merge and Loom scheduling were
outside that comparison.

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

The captured generator derived private deltas from production headers; there
was no maintained owner copy. Eight-bit tags were calculated
only after routing to Swiss, preserving primary read/upsert instructions. The
filtered candidate uses an independent hash byte for continuation classes and
explicitly bounds a probe to one traversal of the table, including when stale
continuation bits all remain set. Its guarded publication APIs are removed:
this spike qualifies concrete exclusive owners, Swiss growth, clone, RMW and
staged reads, not descriptor primary growth/merge or MVCC publication.

## Recovery

Use the pinned Linux toolchain; prefix commands with `orb -m ubuntu` from macOS.

The archived runners checked primary codegen and behavior, then ran
405 sequential callers with rotated treatment order. The density panel targeted three
Swiss populations to cross the different owners' capacity boundaries. Its raw
`load=85` was an unused command default: use actual `n`, primary capacity,
`swiss_slots` and `swiss_load`, not that label.

Retained evidence includes all competitors and negative cases. Full raw
repetitions, per-round maintenance costs, binaries, sources and emitted assembly
are in its recoverable S3 bundles. For example:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/hashtable-swiss/tags/evidence/density-main build/overflow-recovered
python3 workbench/spikes/hashtable-swiss/tags/audit.py build/overflow-recovered --levels 925,980
python3 workbench/spikes/hashtable-swiss/tags/summarize.py build/overflow-recovered \
  --output build/overflow-report
```

`cases.csv` contains per-cell medians/MAD and physical capacities; regenerated
`churn.csv` includes every erase/refill maintenance pulse.

Source and runner bytes remain in commit `786d7589`, the worker bundles'
`source.tar.gz`, and Linux `~/sixdb-archive/hashtable-overflow-20261020/`
([local receipt](archive.json)). Replaying a measured program uses its captured
source or binary; the old generator intentionally is not maintained against the
promoted format. Only the offline audit and summarizer remain here.

References: [Boost layout](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered/structures.html),
[implementation analysis](https://bannalia.blogspot.com/2022/11/inside-boostunorderedflatmap.html),
[maintained Ikea comparison](../../../benchmarks/hashtable/README.md).
