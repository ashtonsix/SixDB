# Geometry API cleanup evidence

This export answers whether the unified Boundlist geometry/selection contract
adds runtime or storage cost. The [report](../../API.md) owns the decision;
[scorecard](scorecard.md) presents absolute ns/request and [summary.csv](summary.csv)
keeps all selected cells, repetitions' ranges and bytes.

C1 is dependent latency. C8 is reciprocal throughput from eight independent
requests, not elapsed time for an eight-request batch. Three process passes each
retain three samples per cell. Process order alternates. Both compiler vectorizers
are disabled; the pinned compiler is Clang 21.1.8. Exact flags and disassembly remain
in each full worker bundle.

## Selected cohorts

| Export | Host | Job | Selection |
| --- | --- | --- | --- |
| [arm-values](arm-values/artifact.json) | Neoverse V2 / c8g.medium | `20261021T023445Z-f908ab18` | Existing StreamPack operations, before/current/fused |
| [granite-values](granite-values/artifact.json) | Granite Rapids / c8i.large | `20261021T023439Z-c31bc267` | Same, AVX2 and AVX-512 |
| [zen-values](zen-values/artifact.json) | Zen 5 / c8a.medium | `20261021T025325Z-68070b85` | Same, 2,097,152 requests per sample after short-run large-pool variation |
| [arm-geometry](arm-geometry/artifact.json) | Neoverse V2 / c8g.medium | `20261021T032004Z-af90a502` | Complete raw/selected geometry → value, and borrowed geometry versus old composition/fused scans |
| [granite-geometry](granite-geometry/artifact.json) | Granite Rapids / c8i.large | `20261021T031959Z-32e0b803` | Same, AVX2 and AVX-512 |
| [zen-geometry](zen-geometry/artifact.json) | Zen 5 / c8a.large | `20261021T031953Z-4f2d159e` | Same, AVX2 and AVX-512; one pinned measurement CPU |

Each before/after pair uses its own host. Values and geometry cohorts are separate
runs; the two Zen cohorts use different instance sizes. Do not subtract numbers
across those cohorts as a seam-cost estimate. `host.json` records hardware and
`job.json` records the source digest, command and configuration. Artifact references
recover full source snapshots, before headers and linked disassembly from S3.

The primary values source digests are `376430f6…` (ARM/Granite) and
`421b0876…` (Zen). Geometry uses
`a193f8584a9bccc63e2123f8e01467efaf79009a818c7852ee59c163bb424882`.
The before Boundlist/StreamPack files match Git `7a57deae` byte-for-byte.

The values CSV files also contain intermediate new-API rows in the Zen run.
Those rows are superseded: [inputs.json](inputs.json) selects only the three
existing operations from values cohorts. Final new-API rows come exclusively
from geometry cohorts. The initial borrowed-overlap expression was rejected
because algebra let the compiler discard a boundary; none of its timing rows
are selected. The final consumer uses both boundaries in its dependent result.

## Work and memory actually measured

StreamPack cases use 256 intervals, M=4095, B=1,048,320, grouped R/U with U present,
and retirement metadata. Pools contain 1/64/4096 blocks. A value's final word
selects the next request; this is not a full-value byte scan. New geometry cases
use raw ownership, explicit selection or an overlap window before that value
read. Bitmap contents in timed panels are live; independent correctness histories
exercise retirement and zero-size combinations.

Borrowed geometry covers 16/256/4096 intervals, u16/u64 cuts and nonzero high
origins. Point/range consumers include query construction, domain checks, both
range boundaries and dependent payload selection. Locate's old composition starts
with an admitted rank; the new ordinary operation also checks that rank. The
independent fused endpoint control is an explicit SIMD scan, not a claim that a
flat scan is the strongest algorithm at every width/count.

Storage fields in CSV are logical encoded bytes, granted metadata/payload backing
and view size (or cut/payload bytes for borrowed geometry). They are identical
between matched versions. Pool sizes and allocated bytes do not prove cache
residence; there is no new DRAM or Loom claim here. Existing actual Loom
qualification remains in [the portfolio report](../../PORTFOLIO.md).

## Recovery and reduction

Each subdirectory's `artifact.json` identifies a verified full worker archive;
`provenance.json` records selected bytes. For example:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/boundlist/evidence/api/arm-geometry build/recovered/api-arm
python3 workbench/spikes/boundlist/api-report.py \
  workbench/spikes/boundlist/evidence/api/inputs.json build/api-report
```

[api-codegen.py](../../api-codegen.py) compares linked complete functions from
recovered compressed disassembly. Its namespace/address normalization and opcode
histograms are diagnostic, not machine-byte or cycle-equivalence assertions.

## Final generic-domain checks

The final source adds mixed origin/end carrier coverage and preserves full-width
queries passed to an optional owner hook. It is source digest
`9d03765d20ad6d89801374150bed82c8bb818dd353d36ffbee7e07614581c9b5`.
Updated native tests pass on all five profiles. Both complete consumer executables
(`boundlist_portfolio` and `boundlist_api_geometry`) rebuild **byte-identically**
to the timed geometry cohort on every profile: ten matching SHA-256 pairs.
This supports using the recorded timings for those covered consumers, while
separately testing the newly covered domains.

| Export | Job | Evidence |
| --- | --- | --- |
| [arm-contract](arm-contract/artifact.json) | `20261021T033302Z-9b4617b4` | NEON tests and before/after binary hashes |
| [granite-contract](granite-contract/artifact.json) | `20261021T033256Z-2a90074d` | AVX2/AVX-512 tests and hashes |
| [zen-contract](zen-contract/artifact.json) | `20261021T033250Z-dfa84a0d` | AVX2/AVX-512 tests and hashes |

`api-contract-native.sh` requires the preceding measured builds on the same
worker and fails if either complete consumer binary changes.
