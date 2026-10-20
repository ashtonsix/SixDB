# Retired Orbital drivers

The allocation Page store and disposable Stage 2 performance harness served
their investigations. Their timing executables, launch matrices, rejected
implementation and unused prototype/ABI controls are retired. Selected results,
negative findings, raw-byte identities and artifact references remain beside
their investigations. The old packed-page ABI study is historical too.

## Current integration checks

Configure `SIXDB_SPIKES=orbital-mvcc` and build `orbital_mvcc_metadata_validate`
(or the compatible `orbital_mvcc_validate`). Both run the following checks
against production Orbital/Loom and real Ikea encodings, independently of LMDB
and the rejected MVCC implementation:

| Target | Consequence retained |
| --- | --- |
| `orbital_object_catalog_check` | Selected production catalog, versions, alternatives and retention |
| `orbital_object_shape_check` | Actual Shape interpretation and backing ownership |
| `orbital_object_bounded_check` | Interrupted reclamation, refusal, recycled pins and retained readers |
| `orbital_mvcc_composed_check` | Codec publication, old-reader discovery, conversion and private failure |
| `orbital_mvcc_composed_read_fusion_check` | Fused read authorization, bounds and independent expected outcomes |
| `orbital_mvcc_composed_append_check` | Changed counts through append/conversion and unequal interpretation at equal byte extents |
| `loom_object_module_check` | Suspension, cancellation, late discovery and foreign immutable completion |
| `loom_object_ring_check` | Active/overflow borrowers, cancellation and Store reset between visits |
| `orbital_pack_growth_check` | Relocation, Hold refusal and explicit rebinding after real pack growth |

Their private trace, encoded corpus, row-map oracle and Pool/Lane lifetime live
with the [integration fixture](../spikes/orbital-mvcc/metadata/composed/fixture.h).
The fixed test Record is not an Engine data model. Production `orbital_validate`
and `loom_validate` continue to own their module checks.

## Historical recovery

The last source before retirement is commit `c8f90811`. Inspect a file without
restoring a checkout, for example:

```sh
git show c8f90811:workbench/spikes/orbital-mvcc/performance/main.cpp
git show c8f90811:workbench/benchmarks/orbital/workload.h
```

For a complete historical checkout, choose a new ignored path:

```sh
git worktree add --detach build/recovered/orbital-drivers-c8f90811 c8f90811
```

That commit preserves code for inspection; each measured result's own source
capture, binary and host receipt remain authoritative for reproduction. Use its
artifact reference and the [recovery tools](../tools/artifacts.md) for those
original bytes. No old raw output or recovery reference was removed here.

## Offline reports

Existing report spellings remain usable after restoring their collected inputs:

```sh
python3 workbench/benchmarks/orbital/run.py --output COLLECTED_ALLOCATION --summarize-only
python3 workbench/spikes/orbital-mvcc/performance/run.py --summarize COLLECTED_VERSIONED
python3 workbench/spikes/orbital-mvcc/performance/run.py --report COLLECTED_VERSIONED
python3 workbench/spikes/orbital-mvcc/performance/broader_run.py \
  --report-only COLLECTED_BREADTH --output build/reports/breadth
python3 workbench/spikes/orbital-mvcc/performance/cooperative_run.py \
  --report-only COLLECTED_SERVICE --output build/reports/service
```

These commands read collected measurements; they no longer build or dispatch
experiments. Full `--report` also needs the original captured executable,
disassembly and qualification inputs. Retained report functions preserve their
units, raw repetitions, missing values and failure checks.

## Reuse before Engine

[Native peers](../peers/README.md) supply LMDB discovery without the old
Store/schema layer, plus the existing verified Abseil provider. Flags, encoding,
transaction/snapshot lifetime and value consumption belong to the new caller.
The [dataset tools](../datasets/README.md) already share pinned source bytes and
parameterized preparations; workload-specific sampling and query traces stay
with their actual consumer. The old keyset membership windows remain a specific
input format, not a general record or query interface.
