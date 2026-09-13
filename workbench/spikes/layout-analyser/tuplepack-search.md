# Layout search: mechanisms and falsifying probes

Research/design note, 2026-09-11. **Search physical code placement and bound
operation recipes together, and judge heuristics by measured regret against a
small exhaustive reference.** The suggestions below are experiments, not an
Engine interface, search algorithm, analysis cadence or migration policy. The
placement counts and [bounded analyser checks](tuplepack-reference/README.md) are executable;
no new CPU performance measurements were run for this note. The
[spike hypothesis](../tuple-layout/README.md) and [initial evidence](../tuple-layout/initial.md)
remain the starting point.

## Useful prior mechanisms, with their limits

- **HYRISE: workload partitioning and a costed search.** Its cost model accounts
  for projection geometry, alignment, cache levels and access selectivity.
  Section 4.2 splits attributes into always-co-accessed primary partitions,
  considers merges, and searches non-overlapping covers. Its pruning uses
  independent additive partition costs; order within a primary partition is
  immaterial under that model. Section 4.3 uses co-access graph partitioning for
  larger problems. These are useful candidate generators for SixDB, but their
  optimality/pruning arguments do not transfer automatically. Our inference:
  ordered byte maps, shared loads, preservation merges and native handoff can
  change costs inside and across such groups. Equal co-access sets therefore
  need not be interchangeable. The prototype also omitted transactions and
  recovery (§2), so its measurements do not price SixDB's mutation ownership.
  [Grund et al., *HYRISE—A Main Memory Hybrid Storage Engine*, §§2–4](https://www.vldb.org/pvldb/vol4/p105-grund.pdf#page=5).

- **PAX: change placement inside a bounded storage group.** PAX keeps a page's
  records together while storing each attribute in a separate minipage; matching
  ordinal positions support reconstruction. This is a useful precedent for
  comparing small planes without equating the plane, tuple, transfer packet and
  storage unit. It is not a search over mixed-width bit positions or instruction
  recipes. Our inference: use its placement distinction as a controlled variable,
  not a reason to choose a page-sized TuplePack grain. Splitting into planes must
  still earn its additional addresses, gathers and mutation coverage.
  [Ailamaki et al., *Weaving Relations for Cache Performance*, §§3–4](https://www.vldb.org/conf/2001/P169.pdf#page=3).

- **H2O: layouts and execution strategies are joint choices.** H2O evaluates
  alternative layouts and access strategies, generates specialized operators,
  and caches compiled operators. Its adaptation objective includes layout
  transformation cost; proposed layouts are created lazily when used. It tracks
  predicate and projection affinities separately, with an adaptive history
  window. Carry over the joint choice and explicit amortization, not that window
  policy. Its CPU model is based on cache misses, including intermediate results;
  its execution strategies materialize output in memory. Our inference: neither
  substitutes for measuring a tiny native handoff, partial-byte writer or recipe
  preparation. We need distinguish rebinding an operation from repacking stored
  data, including segments that retain their current representation indefinitely.
  [Alagiannis et al., *H2O: A Hands-free Adaptive Store*, §§3.2–3.5](https://stratos.seas.harvard.edu/sites/g/files/omnuum4611/files/stratos/files/h2o.pdf#page=4).

- **ByteStore: measure real alternatives before choosing.** Its advisor encodes
  each column as ByteSlice and PP-VBS, profiles scans with 100 literals spanning
  feasible selectivities, and compares the area under the performance curves;
  known selectivity can inform the choice. The advisor treats lookup performance
  as comparable and reduces the decision to scans. Copy the empirical comparison,
  not this reduced objective: TuplePack writes, ordered projections and native
  consumers can reverse the ranking. Weight profiles by the intended workload;
  an unweighted selectivity curve is a workload assumption. Its offline tuning
  boundary also does not establish an acceptable preparation budget for changing
  maps on small segments.
  [Zhang et al., *ByteStore: Hybrid Layouts for Main-Memory Column Stores*, §4, Algorithm 5](https://arxiv.org/pdf/2209.00220#page=9).

- **HyPer: generate a useful region, retain reusable machinery.** The 2011
  compiler pushes data through a pipeline to keep values in registers;
  `produce/consume` is a code-generation interface, not a per-tuple runtime
  protocol. It combines generated tuple-processing code with precompiled C++
  machinery and deliberately splits complex plans to control code growth.
  Generating LLVM IR reduced compilation latency relative to its C++ route.
  Carry over the region question: specializing extraction alone may leave most
  of the benefit unavailable until extraction and its consumer can optimize
  together. This does not imply that all native calls spill; our ABI probe already
  demonstrates register handoff in particular signatures. Its whole-query
  compilation timings do not predict today's tiny-map specialization cost.
  [Neumann, *Efficiently Compiling Efficient Query Plans for Modern Hardware*, §§3–4](https://www.vldb.org/pvldb/vol4/p539-neumann.pdf#page=3).

- **Later HyPer: delay compilation until remaining work can repay it.** Adaptive
  Execution starts pipeline workers in a bytecode interpreter, considers
  unoptimized/optimized compilation using observed progress and empirical
  compilation/speedup estimates, and changes the worker variant between morsels.
  Worker variants share state and range parameters. A single LLVM-based query
  translation supports the execution modes. This is useful prior work for
  improving retained plans, but the paper's known remaining query work is different
  from forecasting future reuse of a transactional map. Its interpreter is also
  different from TuplePack's already compiled, parameterized native kernels.
  Borrow the measured break-even decision and compatible replacement boundary;
  neither its VM nor its switching cadence is implied.
  [Kohn, Leis and Neumann, *Adaptive Execution of Compiled Queries*, §§III–IV](https://15721.courses.cs.cmu.edu/spring2018/papers/03-compilation/kohn-icde2018.pdf#page=3).

The local [Calico point-layout findings](../../../../calico/workbench/prototypes/point-layout/REPORT.md)
already separate placement from decoder granularity and show a read-clustering
candidate losing on partial updates. Those equal-width scalar results motivate
this comparison; they do not predict a coalesced mixed-code writer.

## What to cost and retain

A candidate consists of code positions `L`, placement `P` (stride/planes), and
one recipe per operation. An operation description needs its **ordered** caller
map, holes, read/write direction, consumer, transfer shape and access pattern.
A co-access matrix loses information needed to distinguish these operations.

For a declared observation horizon, use total time as the experimental objective:

```text
T = analysis + actual preparation events + optional repacking
    + sum_o (number of invocations_o * measured complete-operation time_o)
```

Operation costs must match the execution context in which they are combined.
The [V2 mixed-plan evidence](../tuple-layout/viability.md), retained under
[mixed-v2](../tuple-layout/evidence/mixed-v2/provenance.json), falsifies treating isolated costs
as reliable fixed coefficients for final selection: within the same 56 plans
(28 layouts × two uniform recipe families), the isolated-cost choice had about
65% measured regret at 50% and 90% writes, despite only 0.3% at 5% writes.
Actual trace weights were used; this was not a comparison against arbitrary
per-operation hybrid recipes. **Isolated costs can propose candidates; final
selection needs representative mixed-plan probing.** The cause has not been
assigned through PMU evidence. A measured subset winner supplies no validated
pruning rule for the exhaustive universe; preserve alternative structures when
choosing what to probe. The [analyser guide](tuplepack-reference/README.md#checks-and-what-remains-open)
records the interpretation and its scope.

One invocation of a scan includes its declared row count; do not weight a
nanosecond-per-row scan as though it were one point access. Preparation includes
map validation, control construction/allocation and, when applicable, compilation.
Charge shared compiled artifacts once, with their actual reuse. Keep executable,
control-state and temporary-memory sizes separately visible. Initially report
steady-state costs and break-even reuse counts; the experiment need not choose a
horizon or permit migration. For two otherwise comparable recipes, preparation
difference divided by per-call saving gives a break-even reuse estimate only
when the saving is positive and persists in the measured context.

Record actual load/store spans, old-payload reads, padding permissions and required
ownership alongside each recipe. A logically full replacement may still require
preserving spare bits; conversely, explicitly disposable padding should not force
an unnecessary old-byte load. Coalescing contributions from two packets can remove
such loads. Full-byte stores still need ownership and issued-span reporting under
the [integration contract](../../../ikea/docs/integration.md).

Keep three measurements distinct:

1. **Operation cost with resident data and fixed placement:** map controls,
   extraction/insertion, preservation, calls and an actual consumer.
2. **Placement/working-set behavior:** the same logical operation across dense
   rows, padding and planes, with footprints and actual access extents recorded.
3. **Complete caller path:** preparation reuse, dispatch, required effects and
   consumer together, as those features become executable.

These are attribution controls, not additive components whose isolated times can
simply be summed: memory-level parallelism and instruction overlap can change.
Port pressure, permutation counts, dependency depth and logical line coverage
are explanatory features, not measured time or cache misses. Preserve machine,
compiler, recipe and caller identity with cost evidence. The
[retained-plan idea](../../notebook/ideas.md#plans-that-keep-improving) and
[executable-placement observation](../executable-placement/evidence/checked-point-layout-20260911/summary.md)
make that evidence context consequential.

For the HyPer question, add a **nested specialization comparison** on selected
layouts/maps, without first building a JIT:

| Variant | What becomes constant or fused |
| --- | --- |
| Parameterized native kernel | Prepared shuffle/shift/preservation controls are runtime data; code is reusable across maps. |
| Specialized layout/map | The same chosen controls become constants, retaining the same consumer boundary. |
| Specialized consumer region | Extraction plus the order-sensitive consumer, or writer preparation plus coalesced insertion, can optimize together. |

Keep placement and logical output/effects identical across variants. Use existing
compiled fixtures to screen the available execution benefit, then time the actual
proposed preparation/compiler path before making amortization claims. Record
first-use latency, steady-state time, code/control size and total cost at
`1, 16, 256, 4096, 65536` reuses; compare one stable map with 64 rotating maps.
This distinguishes useful constant propagation from useful region fusion and
from cache/reuse effects. AOT fixture build time is not a proxy for hypothetical
JIT latency.

A retained plan could keep the shared recipe available while a more specialized
variant is prepared. Reuse identity must cover logical/ordered-map semantics,
physical layout, recipe assumptions, ISA and consumer/effect contract; a physical
buffer address need not be specialized. Replace a variant only at a completed
operation boundary with its required state preserved. Changing executable choice
does not itself permit changing stored representation or weakening mutation
admission. These are proposed experimental boundaries, not a chosen code cache
or plan-replacement API.

## SVE/SVE2 opportunities, still unmeasured

The strongest immediate candidate is **predicated byte memory access for sparse
writes and tails**. These facilities belong to SVE and are inherited by SVE2;
they are not all new SVE2 instructions. Predicated `LD1B`/`ST1B` can select bytes
within a contiguous vector span. Our inference: the hot-cluster operation below,
which writes 24 physical bytes within a 60-byte extent, could use a native sparse
writer where the first runtime capture has only a scalar writer. The predicate
must reflect issued byte coverage. Unselected bits inside an active byte still
need preservation; byte predication does not remove that obligation or coalesce
contributions from separate packets. The same mechanism can handle bounded tails
without accessing bytes beyond the admitted extent. This is an instruction-level
opportunity, not a measured speedup or cache-traffic reduction.
[Arm, *Introduction to SVE*, §4](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/SVE%20programmers%20guide/102476_0001_00_en_introduction-to-sve.pdf#page=12);
[Arm SVE architecture supplement, predication and memory access](https://kib.kiev.ua/x86docs/ARM/SVE/DDI0584B_a_SVE_supp_armv9A.pdf#page=22).

**Dense table lookup has a different tradeoff.** Neoverse V2 implements a 128-bit
SVE/SVE2 vector length. NEON's `vqtbl4q_u8` selects from four 16-byte registers;
SVE2's two-register `TBL` selects from two registers of the current vector length.
The capacities below follow directly from those instruction definitions:

| Instruction and vector length | Source table | Result per instruction |
| --- | ---: | ---: |
| NEON four-register `TBL`, 128 bits | 64 bytes | 16 bytes |
| SVE2 two-register `TBL`, 128 bits, as on V2 | 32 bytes | 16 bytes |
| SVE2 two-register `TBL`, 256 bits | 64 bytes | 32 bytes |

Thus V2 does not automatically improve an arbitrary 64-byte permutation: a
128-bit SVE2 recipe needs multiple lookups/combinations to address the whole
table. At 512-bit vector length even SVE's single-register `TBL` can address a
64-byte table. These are capacities, not throughput predictions; routing,
instruction latency and execution resources still matter. Table indices also
depend on vector length, so a prepared recipe must account for the effective
length. Keeping a NEON permutation inside a recipe that benefits from SVE memory
operations is worth considering, subject to compiler and register-transfer costs.
[Arm Neoverse V2 TRM, §14](https://documentation-service.arm.com/static/633fe1ec4c59b30b517730b8#page=96);
[Arm NEON intrinsics, table lookup](https://arm-software.github.io/acle/neon_intrinsics/advsimd.html#table-lookup);
[Arm SVE architecture supplement, §5.3.20](https://kib.kiev.ua/x86docs/ARM/SVE/DDI0584B_a_SVE_supp_armv9A.pdf#page=83).

Wider vectors could also reduce the registers needed for the compound operation,
but the observed NEON payload spills are a separate reason to simplify its
composed transformation. An ISA change alone does not eliminate the intermediate
64-byte payload → 128 code values → 64 assembled bytes. Compare separate packet
transforms, concatenated generic transforms, constant specialization and
algebraically composed byte transformations with identical packet inputs and a
common opaque operation endpoint. Selective-memory gains and composition gains
must be attributed separately. No SVE/SVE2 implementation or timing was produced
for this note; effective vector length, hot controls, generated code and payload
spills remain measurement conditions rather than assumed benefits.

## Small exhaustive reference

Use labelled codes **A:1, B:7, C:3, D:5**. Enumerate every non-overlapping,
byte-contained interval assignment in a two-byte or three-byte unit. Keep physical
byte order, code identities, interior gaps and unused bytes; do not quotient them
out by co-access or permutation symmetry.

| Unit size | Legal placements | Example conflict |
| --- | ---: | --- |
| 2 bytes | 8 | Complementary pairs `AB` and `CD` are forced; byte order and pair orientation vary. |
| 3 bytes | 2,808 | `A,C` can share a hot byte while `B` and `D` occupy separate bytes; density, edges and preservation compete. |

These counts were obtained with a short Linux Python enumeration. Reproduce by
recursing through widths in descending order, trying every byte and every shift
`0..8-width`, accepting a placement iff its bit mask does not overlap that byte's
occupied mask, then backtracking. Count every complete assignment. The three-byte
count includes layouts with an unused byte. There are **2,816 candidates**, not
2,816 equivalence classes under an assumed cost model.

The [executable analyser reference](tuplepack-reference/README.md), added 2026-09-12,
enumerates this universe, exports ordered operations and structural features,
and accepts a measured cost table to select operation recipes at a declared
reuse horizon. It reports heuristic regret and preparation/migration break-even
within the supplied candidate and recipe universe. A deterministic initial sample
retains all eight two-byte layouts plus 20 structurally diverse three-byte
layouts; that sample has no full-universe regret guarantee. Independent subset-DP
counts, bit-set checks and synthetic arithmetic cases validate the machinery.
The included cost fixtures are invented; the [measured mixed-plan experiment](../tuple-layout/viability.md)
is separate evidence and limits how isolated tables should select candidates.
See the guide for the TSV/JSON interchange and exact limits, including
shared-preparation and mixed-workload costs that are not inferred.

Use these exact outer maps, with unused positions through byte 7 set to 255:

| Operation | Outer map | Purpose |
| --- | --- | --- |
| `R_AC` | `[A,C]` | Common small projection; grouping competes with complementary packing. |
| `R_0` | `[A,C,B,D]` | All codes in one caller order. |
| `R_1` | `[B,A,D,C]` | Same selected set, different caller order. |
| `W_A` | `[A]` | Preserve a hot code's neighbors. |
| `W_AC` | `[A,C]` | Co-written small codes versus two complementary bytes. |
| `W_AB` | `[A,B]` | Complementary full-byte replacement competes with hot clustering. |
| `W_all` | `[A,C,B,D]` | Full logical replacement, with declared padding treatment. |

For writes, start by preserving every unselected bit, including spare bits; use
unique maps and admitted replacement widths. Repeat relevant finalists with
owned spare bits explicitly disposable. Report this as a different contract,
not a kernel optimization under unchanged obligations. Vary replacement values;
check selected values and unselected bits against the independent oracle.

For reads, pair exact materialization with an order-sensitive native consumer,
such as a weighted sum with distinct weights for outer positions. An ordinary
sum cannot distinguish these map orders. Use equivalent consumer semantics across
scalar and vector carriers and validate actual byte order independently.

Initially collect each candidate's seven operation costs once per implemented
recipe family. The scalar 8-byte route and the runtime native route are useful
families; compiled fixtures remain controls unless exhaustively included. Fixed
admitted 16-byte slots and a small common resident row set isolate operation cost.
Retest finalists with only their true payload extent admitted to expose any
hidden wide-load requirement, then with dense two-/three-byte strides.

Reweight the operation table with write fractions `0.05, 0.50, 0.90`. Within
reads use weights `(R_AC,R_0,R_1)=(0.50,0.25,0.25)`; within writes use
`(W_A,W_AC,W_AB,W_all)=(0.40,0.40,0.10,0.10)`. These are deliberately adversarial
synthetic workloads, not estimates of production traffic. Also swap the `R_0`
and `R_1` emphasis using read weights `(0.50,0.45,0.05)` and
`(0.50,0.05,0.45)` while keeping their combined frequency fixed.

Measure every legal candidate with every recipe family admitted to this bounded
comparison. The best is an **empirical reference within that space**, not an
optimum over possible machine code, placements or noisy future executions.
Compare density-first, edge-first, outer-order-first and co-access-first heuristics
by their selected candidate's excess measured cost over the reference. Break ties
deterministically and report tie sensitivity. Confirmation runs for each heuristic
choice and all near-winners should be independent of the screening samples;
otherwise minimum-of-many noise can manufacture regret.

The existing **1,4,3** schema supplies a second small control: six placements in
one byte, or 648 if all placements within two bytes are allowed under the same
enumeration. It tests whether giving the interior code an edge repays padding.
It should not be the only schema used to calibrate the model.

## Larger stress instance

Use 12 motifs. Motif `j` has codes `c[8j+k]`, with widths
`[1,7,2,6,3,5,4,4]`. This gives **96 codes, 384 payload bits**, leaving room to
compare 48- and 60-byte units within the proposed limit. Let `qk = c[8j+k]`.

| Candidate | Exact placement |
| --- | --- |
| Dense complementary, 48 B | Byte `4j+k` holds `q(2k)` low and `q(2k+1)` high, for `k=0..3`. |
| Reverse edges, 48 B | Same pairs/bytes, with their low/high order reversed. |
| Grouped pairs, 48 B | Same pair orientation as dense, but pair `k` of motif `j` occupies byte `12k+j`. |
| Hot cluster, 60 B | Bytes `5j..5j+4`: `{q0@0,q2@1,q4@3}`, `{q6@0,q7@4}`, `{q1@0}`, `{q3@0}`, `{q5@0}`; `@` denotes bit shift. |

The 60-byte layout introduces an interior 2-bit code while bringing three hot
codes into one byte. This deliberately conflicts with edge placement and density.
Use the four candidates, then a declared equal budget of local moves/random
restarts for competing heuristics. Report best-found results and small-instance
regret; do not describe the large result as optimal.

Concrete operations:

- **Hot read/write:** `m[4j+k]=c[8j+2k]`, giving 48 selected bytes; bytes 48–63
  are holes. Its opponent is `m_perm[t]=m[(17t+5) mod 48]`, the same selected
  codes in a permutation that crosses 16-byte regions. Preserve each requested
  map independently. Repeat with holes interspersed if sparse packets matter.
- **Complementary update:** select `c[8j]` and `c[8j+1]` for every motif. Dense
  pairing makes these complete destination bytes; hot clustering changes the
  preservation requirement.
- **Full replacement:** one input packet contains all even-rank motif codes,
  the other all odd-rank codes, each in 48 positions followed by 16 holes. Every
  dense destination byte receives bits from both packets. Compare sequential
  preserving writes with a single coalesced operation over both packets, charging
  the complete operation and reporting issued coverage. This is not a claim that
  either individual packet is a full-byte write.
- **Scan:** select `c[8j]` for `j=0..11`. Compare one tuple per transfer with
  four tuples in 16-byte materialized slots per 64-byte transfer, including
  gather/preparation and a real consumer. Report per invocation and per row.

For placement, compare dense strides 48/60, padded 64-byte slots, and the grouped
48-byte candidate stored as four 12-byte planes within a 64-row tile. Keep code
placement fixed for each placement comparison; record any necessary recipe or
address-generation changes. Use matched byte-per-code controls, including their
required multiple units/carriers for 96 codes. Their bound dispatch and consumer
must match the packed comparison as closely as the representation permits.

Establish resident-set tiers on the measured machine and include independent
point throughput, a dependency-constrained full-read control, and streaming scans.
Use the same logical values and row sequences across layouts. Add two segments
with opposing read/write mixes, each retaining its own layout; compare grouped
dispatch with alternating segment access and rotating maps. This exposes recipe
state/cache pressure without requiring migration. Pin the CPU and run cases and
repetitions sequentially under the [measurement conventions](../../benchmarks/README.md).

## Outcomes that would change the design

| Observation | Consequence |
| --- | --- |
| Same-set map permutations change layout winners | Retain ordered maps and recipe alternatives; co-access sets alone are insufficient. |
| Padding wins only with disposable spare bits or coalesced writes | Make preservation permissions and operation grouping visible during preparation. |
| Generic recipes flatten layout differences but compiled fixtures recover them | Improve recipe selection before building a sophisticated physical search. |
| Winner changes only with footprint/placement | Calibrate working-set/access geometry separately; instruction heuristics should not decide placement. |
| A heuristic stays close to the exhaustive reference across mixes | It earns use as a cheap candidate generator within those conditions, not a universal pruning rule. |
| Binding/control rotation removes steady-state gains | Prefer reuse/shared recipes or longer amortization; no automatic migration conclusion follows. |

## What the initial probe does not establish

The initial note already limits its claims appropriately. In particular, its
measurements cannot establish arbitrary-map costs, a ranking of physical layouts,
a server transactional result, or a general native calling convention. Additional
reasons to preserve those limits in subsequent summaries:

- The fixed 8,192-entry repeating trace limits the visited rows even when the
  allocation has 65,536 rows. Allocation size is not the accessed working set;
  derive the latter from the trace and actual load/store coverage.
- All cooperating maps use the same 16-byte packed input and full-region stores.
  They provide no comparison of clustering-induced address changes, narrower
  issued stores, cross-packet merges or plane gathers.
- The unpacked timing control is inline. Bound packed versus inline unpacked
  combines representation and call-boundary effects; add a matched bound control.
- The fixed, pre-admitted replacement packet prices neither its production nor
  changing-map preparation. Repeated identical replacements also say nothing
  about summary maintenance or change-sensitive ownership costs.
- The sum is insensitive to byte order, while zero-region elimination differs
  across inline and opaque boundaries. Preserve materialization and useful native
  consumers as separate comparisons. ABI feasibility alone does not price caller
  register liveness, metadata or spills.

This inquiry also revisits the notebook's
[warning about preparation exceeding saved work](../../notebook/ideas.md#when-less-logical-work-costs-more-cpu):
fewer masks, loads or logical writes are promising mechanisms only after their
construction and complete consumption have been charged.
