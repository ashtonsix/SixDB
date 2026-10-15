# What I would do if I could start again

The mistake was allowing a provisional implementation to become the question.
We repeatedly asked how to improve its lookup, field extraction or ownership
costs. The original question was how to divide storage and work across a packed
set/map/multimap so that complete operations beat strong ordinary containers.

Starting again, I would design the changing container, its execution recipes and
its measured callers together. I would make the first performance comparison
discriminate between different organizations of the hierarchy. I would not begin
by rebuilding the present N16 table behind cleaner interfaces.

## What the work actually establishes

The historical short-remainder wins are real but conditional on domain, capacity,
value size and platform. Small maps deserve a genuine list regime. Dense deletion
and some construction/churn workloads have meaningful advantages. Actual Loom
staging wins large-footprint throughput on Zen5; fresh V2 throughput and hot
serial reads remain below the requested bar. Neither outcome generalizes to every
shape. See [the retained comparisons](RESULTS.md).

Primitive admission and bounded native transfers removed substantial overhead.
The residual direct/composed gap is often small, while both implementations lose
the complete workload. However, the direct arm shares much of the same addressing,
routing and ownership machinery. It is a transfer-cost control, not an independently
optimized lower bound. I should neither keep blaming the primitives nor declare
their composition finished on that evidence.

The eager byte-plane and native-word exact-match candidates did not recover hot
latency. That rejects those recipes in the tested shapes. It does not reject exact
remainder matching for short domains, every bucket capacity, or the original brief.
Capacity cliffs, retained versions and preparation change the answer materially.

The final bounded growth experiment also matters: its N16 mutation/publication
lifecycle is near parity or modestly ahead on the three completed targets, while
multi-bucket hot reads still lose. Requested payload savings coexist with about
11 KB of shared preparation and larger owner metadata. A payload-only comparison
would overstate the memory result. The live source's later point-dispatch fix has
local checks and disassembly, but no native timing claim.

## Accidental constraints to remove

- N16 rows, a secondary one eighth the primary, and an 80% primary threshold.
  The brief specifies neither those values nor identical organizations in both
  tiers. The physical read/write unit and actual spill history should determine
  viable capacities together.
- A wide-key scalar map as the template for every other operation. Batch 4/8,
  sets, singleton multimaps and worker-local processing should shape operation
  boundaries while those boundaries are still cheap to change.
- A fixed `table<P,Q>` as the ordinary caller interface. It is a useful diagnostic
  instantiation, but it hid changing geometry and preparation costs. Stable-cohort
  binding and point access both need explicit, measured execution paths.
- Whole-allocation cloning, `shared_ptr` checks on each write, a fixed directory
  shape and fixed/full link families as presumed requirements. These are owning
  policies. Orbital owns physical storage/lifetime; the headless primitives do
  not impose these choices. Scratch authority can be admitted once per batch.
- A single flattened table as the required result of worker-local processing.
  Disjoint key partitions may be adopted behind a router. Ordinary baseline
  tables must receive the same freedom. Same-seed merge remains a separate idea.
- A structural solver frontier presented as executable design breadth. The
  successor must lower and measure the recipes it can actually choose, including
  their preparation, physical windows, spill and mutation costs.

## Ideas we have not earned the right to discard

The primary-side summary of downstream fingerprints has not received a comparable
test. Neither have explicit SIMD get4/get8/set4/set8, partitioned-key routing with
exceptions, aligned same-seed merge, geometric links versus a grown buffer, or
multimap singleton promotion and skewed fanout. The dense-domain advisory is still
owed. The current large Ring map contains too few chained keys to choose a chain
policy.

Loom's qualified adjacent-forward observations exist; a per-address heat oracle
does not. Caller occupancy authority, caller-held heat, schema-independent damage
substitution and replica-coordinated fresh entropy must remain visible composition
questions. Failure to implement an idea is not evidence against it.

## A different first question

Compare how the two tables divide work and memory: a compact primary whose slot
count follows a small physical access unit, with more secondary capacity, versus
a fuller primary that spends more bytes to reduce secondary visits. Add a
primary-resident downstream filter as a measured choice, with deletion and aging
costs included. Use one short-remainder and one wide-remainder family. Keep the
permutation fixed initially to isolate this question; retain its distribution
counterexamples and make no stronger security claim.

Every candidate must execute full construction, dependent and independent reads,
mixed misses and churn against ordinary Abseil from its first runnable version.
Include growth/lifetime and real overflow in that same body. Point, packet and
Ring execution consume the same representation and operation semantics. Do not
build three maps that drift apart. The independent fused implementation is a
diagnostic control with identical storage, not a competing production codec.

If a compact primary's fast hit disappears under actual secondary traffic, or its
secondary allocation erases the memory advantage, reject that organization.
Do not rescue it with a new sequence of isolated field optimizations. Conversely,
do not abandon a workload win because a single kernel loses: keep latency,
throughput, memory, mutations and lifetime visible separately.

Worker and multimap consumers then inform the same decisions promptly. For worker
aggregation, compare routed/adopted partitions with consuming same-seed merge;
charge ingestion, skew, exception routing, duplicate combination, coexistence,
post-result reads and retirement. A cheap merge over distinct entries cannot
rescue expensive updates over a much larger input stream. Report that break-even.

The replacement investigation starts in [hashtable-design](../README.md).
This directory becomes reference evidence. Do not transplant its implementation
or generated plans into the new directory. Retain baselines, invariants, counter-
examples, qualified measurements and useful benchmark techniques by reference.
