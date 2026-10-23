# Closing the cohesion regressions

2026-10-23. This note follows the [handover](HANDOVER.md). It re-measures the
cohesion rehaul against its baseline, separates real losses from measurement
artifacts, and records the contract changes made while closing them. The earlier
receipts under `evidence/interface-cohesion-v2-20261023/` remain the record of
their own source cuts.

## Method

The comparison pairs baseline `f1534200` with the working tree, and separately
with `HEAD` (`COHESION_AFTER_COMMIT=HEAD`) as a like-for-like control, using the
existing runners:
- `cohesion-v2-native.py` covers the SeriesPack and TuplePack benchmarks, the
  filter-composition harness and TuplePack initialization;
- `cohesion-v2-peripheral.py` covers Bec256, both Hashtable owners and the
  StreamPack implementation bench.

Each case gets five trials with alternating arm order, pinned to one CPU.
[`cohesion-r5-compare.py`](cohesion-r5-compare.py) reports the median of the
per-trial after/before ratios, so a value above 1 means the after arm is
slower. Runs used AWS `c8a.4xlarge` (Zen 5, profiles AVX2 and AVX-512) and
`c8g.4xlarge` (Graviton 4, NEON).

Three corrections to the earlier method:

- **Placement control.** Every cohort was also built with
  `-falign-functions=64 -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6`
  on both arms (`COHESION_CXXFLAGS`). A loss that appears in the default build
  but not under this flag comes from where code landed, not from what it does.
- **Stack placement.** Each trial runs both arms with the same environment
  padding (`STACK_PADS`), and the padding differs between trials. A median over
  trials therefore does not inherit one arbitrary stack alignment for
  stack-resident fixtures.
- **StreamPack harness.** The after arm's `implementation_bench.cpp` replaced
  its timed projection loop, `bool shuffled` becoming `projection_order`. The
  peripheral runner now gives the before arm the same projection loop, and only
  the Ikea calls differ between arms.

## Findings at HEAD (before these changes)

**NEON.** No SeriesPack, TuplePack, filter or initialization case is more than
3% slower, in either the default or the aligned build. Many cases are faster:
up to 38% for headed SeriesPack mutations and 31% for TuplePack partial-window
predicates.

**Bec256, StreamPack and Hashtable on x86 are placement artifacts.**
- The default build shows 20–30% StreamPack losses on ordered value reads,
  20% on AVX-512 Bec256 terminal decode, and 13% on AVX2 random encode in one
  run.
- With aligned code, every Bec256, StreamPack and Hashtable case on all three
  profiles is within ±3.2%. AVX-512 terminal decode becomes 17% faster.
- The ordered-read differences also changed sign between paths that share the
  same raw code (`spike-body` against `typed`). That points to the harness loop,
  not to Ikea.

**TuplePack tight-column filters are a harness inlining artifact.**
- In `inline_fused/independent8`, AVX2 ops 0 and 3 lose 11–22%, while ops 1,
  2 and 4 win 13–18%. AVX-512 op 0 wins 37%. Aligned code does not change this.
- The predicate's tight path has the same instructions in both arms. The
  `bound_fused` variants, which call that same body out of line, sit at
  1.000 ± 0.012.
- The difference is how the harness's eight-stream loop is scheduled around a
  larger inlined generic path. It is not the predicate's cost.

**Remaining small x86 losses that persist under alignment:**

| Case | AVX2 | AVX-512 |
| --- | ---: | ---: |
| SeriesPack `placed/k31h8/substituted/*/mutation-bound` | 1.07–1.08 | 1.03 |
| TuplePack `initialize<false>` (complete-unit construction) | 1.00–1.02 | 1.03–1.04 |
| TuplePack `composition/128/body` (kernel-only `write_body` pair) | 1.00 | 1.04 |

The `composition/128/body` loop calls two `native::write_body` kernels whose
instructions are identical in both arms. The initialization loop differs by one
spilled loop bound. These cases are recorded rather than attributed.

## Changes

**Trustworthy binding.**
- `tuplepack::layout::identity()` is a digest of the unit extent and every
  code. Views and plans retain it.
- `bind_reader`, `bind_writer`, `bind_predicate`, complete-unit writers and
  `bind_transfer` now compare identities instead of unit extents. A plan for
  another law that happens to share the unit size is refused with
  `error::description`.
- A layout decoded separately, as from one schema shared by many blocks, has
  the same identity, so its plans bind to every block attached with an equal
  law.
- StreamPack's typed `rebind(view)` now checks layout equality and returns
  `expected`; `rebind_unchecked` keeps the per-value loop path. `layout` gains
  `operator==`.

**Uniform effect resolution.**
- Every Ikea effect source now exposes `plane_storage(plane)`: SeriesPack,
  TuplePack and StreamPack views, Bec256 destinations and `dense_output`. Before
  this change only Hashtable and the Intervals editor did.
- An owner resolves any record the same way, as `data() + offset`.

**Tried and dropped: per-record origins.** Carrying the plane origin in every
`journal::entry`, which would make retargeted buffers detectable at
resolution, cost 8–33% on NEON single-row TuplePack journal writes and 5–17% on
x86 SeriesPack substituted writes. A 32-bit origin tag in the existing padding
cost the same, because the price is the per-record origin load rather than the
wider record. The journal keeps its HEAD layout. The contract says owners keep
the origin they issued against when they may retarget a view before resolving.

**Fewer consumer rules.**
- `seriespack::initialize(dense_output, values, effects)` and
  `effect_capacity_initialize(dense_output)` construct a runtime-width array
  through compiled bodies, mirroring `bind_reader(dense_input)`. Frame's `pack()`
  can drop its own 64-way width dispatch.
- `seriespack::visit_point_reads(view, row, emit)` and
  `tuplepack::reader::visit_reads(first, active, emit)` report the exact bytes a
  point or packet read touches. This gives SeriesPack and TuplePack the
  footprint hook that Hashtable staged probes, Intervals windows and StreamPack
  metadata visits already had.

**Register-only complete TuplePack construction.** `write_plan<128>` assembles
each unit from both packet halves in registers and stores it once. The previous
path cleared the unit (a `memset` call), then made one or two out-of-line
preserving writes that read it back.

**Faster masked writes.** `seriespack::selection::region_words()` resolves the
selection's kind once. Bound writers and their admission then read one region
word per sixteen rows, without re-dispatching through `mask()`.

**Tried and reverted, for substituted SeriesPack writes:**
- Emitting composed coverage field by field across a complete substituted tile,
  so contiguous head planes coalesce, made substituted mutations 12–37% slower
  on x86. Coalescing saved less than the duplicated traversal cost.
- Restoring `f1534200`'s single-body coverage visitor, in place of the
  rehaul's split into an inlined identity test and an outlined footprint member,
  made them 5–13% slower on NEON and AVX2. The rehaul's split is a real fix and
  stays.

Checks: [`ikea/test/contracts.cpp`](../../../ikea/test/contracts.cpp) covers
identity refusals and decoded-layout sharing, checked rebind, record
resolution through `plane_storage` against the bytes each command changed, region
words, the runtime constructor against the typed writer, and both footprints. The footprint
checks perturb every byte and require each one that changes a read to lie inside
the emitted spans.

## Results for the final tree

Measured on the committed tree (`cc8a401d`) against `f1534200`, with aligned
code and varied stack offsets. The Bec256, StreamPack and Hashtable row comes
from the preceding run. That tree differed only in a SeriesPack composed-mutation
header those benches do not include.

| Area | NEON | AVX2 | AVX-512 |
| --- | --- | --- | --- |
| TuplePack complete construction (6 cases) | 0.70–0.80 | 0.45–0.52 | 0.29–0.39 |
| TuplePack point reads and writes | parity (0.94–1.02) | parity | parity, with one 0.86 |
| SeriesPack placed headed mutations | 0.62–0.88 | 0.65–0.93 | 0.57–0.88 |
| Filters | 116 of 360 faster than 0.97, none slower than 1.01 | 136 faster, 9 slower | 133 faster, 5 slower |
| Bec256, StreamPack, Hashtable | parity (±4%, DRAM-bound chain reads ±6%) | parity | parity |

The only x86 filter losses are the TuplePack tight-column `inline_fused`
harness cases explained above. SeriesPack substituted (patched-tail) mutations
stay at HEAD's 3–7% x86 loss: two attempts to close it are recorded above.
TuplePack `composition/128/body` keeps its 4% AVX-512 kernel-only difference.

On NEON, no SeriesPack, TuplePack, filter or initialization case is slower
than 1.04. All 98 selected tests (Ikea, owner, Directory, Loom and Orbital
checks) and all 93 public headers pass on NEON, AVX2 and AVX-512 for the
committed tree.

## Uneven feature support

| Capability | Has it | Missing, and the likely value | Action |
| --- | --- | --- | --- |
| Read footprint for prefetch or scheduling | Hashtable (staged probes), Intervals (staged windows), StreamPack (value metadata visit) | SeriesPack points, TuplePack packets | Added `visit_point_reads` and `visit_reads` |
| Plane storage for effect resolution | Hashtable, Intervals editor | SeriesPack, TuplePack, StreamPack, Bec256 views | Added `plane_storage` everywhere |
| Runtime-width compiled binding | SeriesPack reads (`dense_input`) | SeriesPack construction | Added `initialize(dense_output, …)`; runtime replace and set remain typed-only |
| Law identity checked at bind | SeriesPack (types), Bec256 (one law) | TuplePack (unit extent only), StreamPack rebind (unchecked) | Added identity and checked rebind |
| Layout-to-layout transfer | TuplePack (same law), StreamPack (rank rewrite) | SeriesPack transcode (widen, Local↔striped), which Frame thaw/reseal needs | Proposal: a fused native read-then-write between two views |
| Stock summary laws | SeriesPack `sum_change` | TuplePack offers only general observation brackets, with no stock sum delta | Proposal: a TuplePack `sum_change` over selected codes |
| Head-plane early rejection (`guarded`) | SeriesPack | TuplePack has no equivalent; its codes are byte-contained, so the benefit is unclear | Leave |
| Value predicates | SeriesPack, TuplePack | StreamPack (prefix and equality over bytes) | Strings are Ashton's area; flagged only |
| Branchless sparse gathers | AVX-512 (masked loads) | AVX2 and NEON partial-window TuplePack reads use per-row branches | Experiment: pointer select to a zero unit keeps the active-only contract; measure against predictable masks |
