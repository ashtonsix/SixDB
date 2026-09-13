# Tuple layout and bound operations

Can byte-contained codes provide excellent transactional access, useful scans,
and enough compositional freedom for Engine to choose representations from
observed workloads? This investigation is intended to inform both Ikea's
TuplePack primitive and Engine's first representation analyser. The analyser remains spike-only. The curated
[Ikea TuplePack implementation](../../../ikea/docs/tuplepack/usage.md) now accepts manual
layouts; these are a temporary facility for dependent spikes, not the proposed
policy of higher-level production components. Historical measurements here
remain measurements of the captured spike sources.

The [implementation starting point](viability.md) consolidates what is viable,
what changed after adversarial review, and what remains open. Native composition,
checked mutation, scan batching and a measured-layout analyser now have executable
probes. The analyser must test complete mixed workloads: isolated kernel costs
can choose the wrong layout.

The [evidence guide](evidence/README.md) distinguishes offline measurements from
archived intermediate sweeps and explains how to recover either captured source
or inputs accepted by the report tools.

[Implementation decisions](implementation-decisions.md) record the selection of techniques for Ikea.
The [grouped-packet probe](batching/groups/README.md) investigates keeping
caller-selected byte groups together across rows for materialization and mutation.

Earlier evidence: [static extraction and ABI](initial.md),
[first runtime maps and whole-operation composition](runtime-first.md).
The [literature/design note](../layout-analyser/tuplepack-search.md) connects HyPer, HYRISE,
PAX, H2O and ByteStore to the experiments. The [small analyser](../layout-analyser/tuplepack-reference/README.md)
keeps an exhaustive reference and explicit measured subsets.

## Starting hypothesis

Ashton's proposed TuplePack unit holds 1..64 packed bytes, with 1..128 defined
codes. A code has width 1..8 bits and a fixed bit offset within one byte. Codes
are byte-sized pieces of the caller's values; the primitive need not understand
16-bit fields, signed values, floats, application structs, or their semantics.

A caller binds a reader or writer using an outer-byte-to-code-rank map. For
example, `{5,6,255,9}` selects codes 5, 6 and 9 into output bytes 0, 1 and 3;
255 denotes an empty position. Extracted bits occupy the low end of their byte.
This map can differ between operations against the same physical schema. The
meaning of an empty byte (zero, unspecified, or preserved by a merge helper),
read duplication and conflicting writer mappings still need deliberate choices.
The initial experiment zeros empty output positions and uses unique write maps.

The proposed native interface transfers up to 64 materialized bytes, plus an
8-byte scalar interface. Fully processing 128 codes can require two transfers.
Candidate batch shapes include 64 bytes × 1 tuple, 32 × 2, 16 × 4, and smaller
projections over more tuples. Here those byte counts describe the materialized
packet, independently of each tuple's packed size and physical stride. Gathering
several tuples can use different instructions on different ISAs.

Placement remains independent: a 96-byte record could contain two units at
offsets 0 and 64 with stride 96. Separate 16/32-byte planes can favor common
scan projections. Engine can choose different representations for different
segments, including segments which never migrate. Binding another reader does
not migrate stored bytes. Application-byte assembly and struct coercion belong
to the caller, with convenient public helpers a prospective part of TuplePack.

The intuition to test is that layout choice can exploit byte edges, complementary
widths, permutation locality, padding and co-access. A low-edge code can sometimes
use only a mask, a high-edge code only a shift, and an interior code both. The
best placement depends on which codes cooperate in an operation, its consumer,
available instructions, and read/write frequency. Execution-port counts motivate
comparisons; they are not a calibrated operation cost model.

Data distributions, patching and the cadence/locality of sensing and migration
are open follow-ups. The suggested 4,096/65,536-row decision scales are hypotheses.

## First experiment

[ABI probe](abi.cpp) checks the proposed carriers and an opaque consumer boundary
with pinned Clang. [The edge probe](edge.h) enumerates all six orders of widths
`{1,4,3}` within a byte. Sixteen independent triples form a 16-byte tuple with
48 code ranks. Its deliberately cooperating maps put each code kind into one
16-byte output region. This is a favorable extraction control; arbitrary mixed
schemas, byte permutations and gather shapes are outside this first control.

For every nonempty selection of code kinds, the same native read/write bodies
are exercised inline and through a separately compiled function-pointer binding.
An unpacked, 48-byte tuple supplies the same logical values and output map.
The comparison covers a native sum consumer, materialization into 64 output bytes,
and replacement of selected codes. Partial writes coalesce into one 16-byte
load/store region; full writes need no old payload load. Bit-at-a-time reference
checks exhaust old and replacement byte patterns and verify preserved neighbors.

These bound functions are compiled fixtures for known layouts/maps. They do not
solve the important problem of efficiently binding an arbitrary runtime map.
Likewise, these trusted bodies do not implement ordinary mutation admission,
effect journals, summaries, transactions or an analyser. Their issued write span
is the full 16-byte region, including preserved bits; a later owner wrapper must
report that span and secure its write ownership before invocation.

Read [initial observations](initial.md) for evidence and the next uncertainty
suggested by it. Source, experiment and conclusions stay together here as the
question develops; there is no predetermined sequence of promotion stages.

The [runtime follow-up](runtime-first.md) exercises arbitrary ordered maps and
caller-owned whole-operation mutation. Its [run guide](runtime/README.md) covers
local NEON and captured AVX2/AVX-512 worker runs.

## Composition questions in the experiment

Engine supplies semantic field decomposition and workload evidence. Ikea supplies
admissible physical operations and descriptions of what they require. A useful
provisional seam is a candidate physical layout plus bound operation recipes,
with applicability and measured cost kept distinct. A layout search should be
able to reject a candidate because its actual generated/bound operation is poor,
even if an instruction-count surrogate preferred it. Preparation, executable
size and amortization also matter when maps change.

The hot path should run an already bound operation. Representation analysis,
sensing and plan improvement do not belong in each tuple access. Representation
substitution must preserve the caller's semantic and coordinate contract while
allowing the physical schema, placement and execution recipe to change together.
The [retained-plan idea](../../notebook/ideas.md#plans-that-keep-improving) provides
context without requiring an equivalence-graph implementation for this probe.

Partial writes need an inverse map with explicit preservation and conflict rules.
Two native input packets can contain codes sharing a destination byte; their
execution order and coalescing deserve measurement. Splitting a record among
multiple TuplePack units also makes operation-level admission, failure and
visibility important. The [owner integration contract](../../../ikea/docs/integration.md)
is the reference for those obligations, including in-place mutation with Orbital
page COW. Its current SeriesPack journal and expression types are not mandated
for TuplePack. Suspension belongs at a completed operation boundary with the
required bindings, inputs, leases and partial effects retained by the owner.

## Run

Use Linux and pinned Clang 21.1.8; prefix commands with `orb -m ubuntu` on the Mac.
The ABI probe cross-compiles without a target sysroot. The initial executable
kernel probe requires AArch64 NEON.

```sh
python3 workbench/spikes/tuple-layout/abi.py
cmake -S . -B build/tuple-layout/neon -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=tuple-layout \
  -DSIXDB_MARCH=armv8-a+simd
cmake --build build/tuple-layout/neon \
  --target tuple_layout_check tuple_layout_bench -j 2
build/tuple-layout/neon/workbench/spikes/tuple-layout/tuple_layout_check
python3 workbench/spikes/tuple-layout/screen.py \
  --output build/tuple-layout/screen --cpu 2
```

`--filter` selects Google Benchmark cases. Each case processes 256 independent
lookups per iteration through an 8,192-entry repeating row-ID trace, with 1,024
or 65,536 rows. Replacement values are pre-admitted and reused. Five repetitions
run sequentially per case, CPU-pinned, with binding outside timing. These are
warm repeated point operations, not dependent lookup latency or streaming scans.
The sum consumer sums unsigned code bytes, not decoded application values.
The compact CSV preserves every repetition in nanoseconds per tuple. Raw logs
and benchmark JSON remain under ignored `build/`.

## Prior work

[Calico point-layout](../../../../calico/workbench/prototypes/point-layout/README.md)
separated footprint/placement from decoder granularity. Its fused decoder improved
whole-row access without changing bytes; projection clustering sometimes worsened
partial updates. Its equal-width fields, scalar accessors and local timings do
not establish results for this code schema or for server CPUs. SixDB's
[SeriesPack](../../../ikea/README.md) demonstrates composition and owner contracts;
it also taught us to measure complete ordinary operations early, rather than
infer their quality from inner kernels.

The [readiness review](readiness-review.md) checks the module against this proposal;
[packet experiments](batching/README.md) investigate multi-row mutation, physical
transfer grain and the crossover between tiny, small and large tuples.
