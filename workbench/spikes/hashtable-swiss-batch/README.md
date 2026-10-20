# SIMD batches over 16-byte Swiss groups

Completed Zen 5 comparison: **masking eliminates the extra tuple checks and
recovers dense miss gains, but wider metadata lookahead is not a better general
default**. Keep Ikea's current reader and 87.5% ceiling. The strongest tested
candidate is read-only four-group batching for dense, miss-heavy overflow; no
production strategy selector is introduced. See [results](RESULTS.md) for the
combined candidate, actual Boost/Abseil controls and losing cases.

Ashton's follow-up: compare 32/64 control bytes at once, retain the existing
16-byte logical groups, and discard candidates after the first group containing
an empty. Every candidate in that terminating group remains eligible.

The final private overlay changes ordinary reads only. Writes, RMW and staged
reads keep the maintained scalar Swiss engine. Earlier cohorts also changed
mutation resolution; that exposed an AVX-512 erase inlining cliff, so the final
mode separates them. Physical capacity, placement, triangular probe order,
tags and the 87.5% ceiling remain unchanged. The probe sequence is `g, g+1, g+3, g+6, …`;
packed SIMD lanes therefore assemble the next logical groups rather than
mistaking adjacent physical groups for probe order. `tail64` keeps the first
scalar group, batching four groups only after that group cannot terminate.
The 32-byte path uses one contiguous load when the next two groups are adjacent,
assembling them separately on wrap and nonadjacent steps.

`line32` and `line64` instead batch only the first control cache line, then
resume ordinary scalar triangular probing. They preserve the original sequence
without requesting an extra control line on the default 64-aligned backing.
For a 64-byte window, the permitted groups depend on the starting group:

| First group within the line | Logical groups compared before leaving it |
| --- | --- |
| 0 | 0, 1, 3; physical group 2 is ignored even if empty |
| 1 | 1, 2 |
| 2 | 2, 3 |
| 3 | 3 |

Matching bits stay in physical order, which is also logical order within these
prefixes. No gather or bit-extraction permutation is needed. Small Swiss stores
use the scalar fallback; wide loads stay inside assigned backing. Unaligned
custom backing remains correct, but the one-cache-line traffic claim is for the
default allocator's alignment.

Sanitizer checks pass for random set/map histories, clone, RMW and staged reads.
The instrumented actual reader matches an independent scalar oracle over 96,000
queries in each variant, including the exact 26,487 full-key checks and their
order. Separate masks test wrap, upper bits and later candidates within a
terminating group. Instrumentation is absent from release measurements.

The [local evidence](evidence/static/provenance.json) retains the initial
isolated-mask LLVM-MCA exercise and complete primary-code comparisons. LLVM
21.1.8's `znver5` selector uses a Zen 4 scheduling proxy; those literal metadata
sequences are not Zen 5 timing or cache estimates. A separate untimed pass counts
control-region requests on actual tables; it does not report PMU misses.

The benchmark/check scaffolding is adapted from the archived width experiment
([source receipt](../hashtable-swiss-probes/archive.json)); actual Boost 1.92 and
the shared pinned Abseil provider remain controls. Three native panels completed
on the same dedicated worker: [ordered batches](evidence/lookahead/provenance.json),
[bounded first-line batches](evidence/bounded/provenance.json), and the
[final read-only candidate](evidence/readonly/provenance.json). The final panel
preserves 28 complete primary read/write caller functions, including erase/RMW,
and verifies whole-table hot/cold map8 reads and writes with real peers. The
earlier panels also cover sets and hot 64-byte maps. Report recipes were
successfully regenerated from all three S3 archives.

Use Linux with the pinned toolchain, prefixing commands with `orb -m ubuntu`
from macOS. Recover the final audit and summary without rerunning benchmarks:

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-swiss-batch/evidence/readonly build/swiss-batch-report
```

The current CMake/native runner defaults to `group16`, `batch64`, `tail64` and
read-only overlays. `SWISS_BATCH_CASES` selects `K_b_V` cases (CMake uses semicolons,
the native script uses spaces). Exact historical replay uses each archive's
`source.tar.gz` or binaries: the final runner's expanded preservation checks
deliberately reject the earlier coupled writers. Private headers are generated
under ignored build output; this directory does not maintain another table owner.
