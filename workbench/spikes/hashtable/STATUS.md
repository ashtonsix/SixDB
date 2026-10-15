# Working account — 15 October 2026

Ashton asked to finish in-flight work and consolidate the research before further
experiments. **That consolidation pass is complete. Nothing is running or queued.**
The broader goal remains to level up the selected hierarchy before graduation;
its unresolved requirements are explicit in [GRADUATION.md](GRADUATION.md), not
silently waived. Ashton subsequently authorized archiving the closed experiment
and committing the consolidated research in shared main. No hashtable production promotion
or development worktree is involved.

## Reading path and decisions

[README](README.md) is the entry. [DESIGN](DESIGN.md) describes the container
without requiring earlier conversations. [RESULTS](RESULTS.md) is a reproducible
absolute scorecard separating serial latency, reciprocal throughput and
logical memory. [RECALIBRATION](RECALIBRATION.md) records the architectural lessons
and selected/rejected directions. Earlier accumulated reports are preserved in
[RESULTS-HISTORICAL](RESULTS-HISTORICAL.md), outside the primary reading path.

Keep the bucket hierarchy, compact whole-table lists, explicit N8 operations,
Orbital authority and scoped cold Loom execution. Co-select geometry, growth,
inline budget and external ownership. Completed occupancy deltas avoid needless
promotion/growth on repeated keys. Doubling is the working direction; 90% is not
universal across slot geometries. Larger families help wide-map memory but do not
fix wide multimaps. The next wide-map control uses14slots; this is not a final
universal preset. The shared compact packet selector remains rejected/unselected.

The native evidence supports cold throughput and narrow storage advantages, not
broad superiority. Compact lookup, wide multimaps and several merge/growth cases
remain weak. Preserve sets/maps/multimaps, all eight key widths and W1–64 throughout
future implementation. Ordinary/exact baselines, physical memory and residence,
retained readers, balancing and primitive composition remain part of that scope.

## Completed latest comparisons

**Worker format panel:** Zen20261015T091603Z-e2f53c23,
V2...-e87bc9af, Granite...-aa8e38f7. Each 192processes/7,680 rows/768 cells,
42 checks and 18 complete dispatcher inventories. Retained in
`evidence/worker-format-{zen5,neon,granite}`; all summaries reproduce byte-identically
from archives. Local 26ASan/UBSan/stack-UAR and 4 TSan checks pass.
Source digest 85b1269f18e6d37a81cba0b4ae36c8999467bf4dea2a1397d92800a3edbbc664.
[Worker epochs](worker-epochs.md) owns the absolute scorecards and growth controls.

**Actual Worker fold:** all 102 timed processes and 34 checks per native host exit 0;
3,060 rows/306 cells, ten samples/cell. Jobs:
Zen20261015T093934Z-17aca3c5, V2...-573591cd, Granite...-1db2314b.
The jobs then fail their final report because V1 omitted three CSV column labels.
The strict corrected report recovers only that exact 33-label/36-value schema;
original failures and unchanged raw measurements remain preserved. All three
recovered summaries reproduce byte-identically from retained data in
`evidence/worker-fold-{zen5,neon,granite}`. Full archives retain sources, binaries,
assembly, checks and process-resource records. No reruns were substituted.

V1 source 5ec330b4b4dfad3aa1e4f963b93d45e9e9c40de585a1b254414daa6ed27e3e47.
V2 header/report correction 934aa78b8bdb56c235b7fe13db59ac580a37b5af588ba859a953d72e57096b45;
operation bodies are unchanged. Local V1 completes 115 fold checks across 40 domain/
kind/W2/W64 cases plus 3 original Worker regressions, and 51 benchmark processes.
V2 completes 31 ASan/UBSan/always-stack-UAR and 6 TSan checks. All local and native
waits, report, retention and archive handles are terminal. Validation archives
are in `evidence/worker-fold-validation`.

The fold keeps immutable source ownership on its original Worker/Pool, copies
bounded 64-entry chunks, processes eight work units per Task visit, and grows only
the private destination. Exact-prefix retries survive cancellation. Consecutive
multimap values share lookup/reservation for both contenders. Source retirement
is charged separately. Loom independently audited the final-release-credit and
cursor/source lifetime protocol with no identified bug in this serialized fixture.
Future non-done Task termination needs terminal credit handling. No pipelined
handoff, same-seed bucket-wise union or retained-reader destination publication
is claimed. [Worker folding](worker-fold.md) owns results and limitations.

## Evidence and ownership safeguards

[Benchmark validation](BENCHMARK-VALIDATION.md) identifies allocator-perturbed
historical lifetime comparisons. Corrected growth and Worker panels supersede
those rankings. Separate cold read-window results are unaffected; the Granite
recovery excludes its incomplete final family instead of relabeling a timeout.
The current scorecard keeps winning and losing complete pairs together.

Orbital confirmed that allocation growth and metadata-directory relocation can
retire old source state before returning. Fine-grained preservation must precede
those events and cover initialized bytes plus affected owner metadata. A Hold is
not immutability; copied prefixes are not issued writes. The exploratory misuse
of before-write events was removed; links.h/bag_buffers.h remain unchanged.
Whole-material private capture is supported; a byte-COW consumer is not.

Use pinned Linux Clang 21/C++23 and `orb -m ubuntu`. Auto-vectorizers are disabled;
explicit NEON/AVX2/AVX-512 code is measured. The stable local build workspaces are
`list-growth-workspace`, `list-growth-asan-workspace` and
`worker-epochs-tsan-workspace` under `build/hashtable-design`. Frozen captures
are authorized. Do not edit/stage/revert peers' dirty production Ikea/Orbital/Loom
files. The required TuplePack author dependency is accounted for below.

Remaining work is an implementation/graduation decision, not another queued
experiment campaign. Start with the finite gaps in GRADUATION.md; do not restart
from the latest narrow result or treat research cleanup as proof of broad parity.

## Repository consolidation

The active hierarchy moved from `hashtable-design/` to `hashtable/`. Earlier
benchmark controls moved into `reference/`; they remain buildable under the same
spike selector. The closed `hashtable-separated/` implementation and its full
findings are archived with [recovery instructions](SEPARATED-EXPERIMENT.md#recovery).
All three pre-move trees have download-verified recovery references in `archive/`.
Live build/report paths changed; measured evidence bytes, source identities and
native target names did not. Old paths inside captured manifests identify the
measured source and deliberately remain unchanged.

The research uses `tp::native::read_transfer_body`, the fixed-transfer author
entry point already present in the measured source captures. Consolidation also
includes that narrow TuplePack dependency, the existing reader forwarding and
independent guarded-transfer checks, so the committed research builds without
uncommitted API additions. It forwards to the existing kernel; it does not change
the transfer algorithm. Unrelated SeriesPack kernel and Orbital edits are excluded.

Validation after relocation: seven focused C++ checks pass (lookup, layout solver,
SIMD hash packets, dense-domain advisory, narrow-map/wide-multimap Worker folding,
and exact-key baseline), as does the TuplePack GPR suite. All 246 retained exports
verify against staged Git bytes. The current scorecard and Worker-fold summary
reproduce byte-identically; all 62 report recipes resolve their repository inputs.
Authored documentation links and Python/shell syntax checks pass. These checks
validate consolidation, not new native performance claims.
