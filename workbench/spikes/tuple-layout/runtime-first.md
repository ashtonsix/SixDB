# Runtime maps and whole-operation composition

The [later viability round](viability.md) supersedes the open compound-fusion
question below; these measurements and limitations describe this first snapshot.

First runtime-map screen, 2026-09-11. This extends the favorable static
[`{1,4,3}` control](initial.md) to runtime schemas/maps, three native profiles
and a caller-owned compound mutation. It establishes feasibility and useful
failure cases, not competitive TuplePack performance or a production interface.

## What is implemented

[`runtime/model.cpp`](runtime/model.cpp) validates 1..64-byte schemas with up to
128 nonoverlapping, byte-contained codes. Preparation copies the information
needed by an operation; subsequent calls do not inspect a borrowed schema.
Reader maps preserve outer-byte order, allow duplicates and produce zero holes.
Writer maps reject duplicates, ignore holes, check value widths before writing
and preserve unselected bits. These are provisional experimental semantics.

Native bodies use explicit NEON table/shift code, AVX2 byte-shuffle routes with
multiply-based byte shifts, and AVX-512 VBMI permutation/multishift code. Short
final loads/stores stay within the tuple's declared extent. Native writers
currently support maps touching every physical byte, at all lengths 1..64;
sparse writes use the byte-coalesced scalar control. Full byte coverage does not
mean all bits are replaced: preserved neighbors can still require an old load.

The read body can be inlined or called through an opaque native function pointer.
AVX2 uses the `regcall` carrier established by the [ABI experiment](initial.md#carrier-feasibility).
This avoids the default SysV aggregate-return buffer for that specific carrier;
it does not promise spill-free execution inside arbitrary compositions.

## Measured signal

The [local NEON evidence](evidence/runtime-first-neon/provenance.json) and
[Zen 5 evidence](evidence/runtime-first-zen5/provenance.json) retain all
repetitions and binary/build identities in Git. Ordered schema/map descriptions,
full source, binaries and logs are [recoverable](evidence/README.md) through their
artifact references.
Each profile has 252 cases, five sequential repetitions, pinned to one CPU,
with preparation outside invocation timing. Working sets contain 1,024 or
65,536 rows; accesses use a repeated 8,192-ID trace. These are warm independent
point operations, not dependent lookup latency, cold accesses or scans.
The trace touches all 1,024 rows of the smaller allocation but only 7,671 of
65,536 rows in the larger one. Each case uses one stable binding. Physical bytes
are initialized directly; layouts are not repacked from identical semantic rows.

Illustrative medians at 1,024 rows, **CPU ns per tuple**:

| Case | NEON read bound | AVX2 read bound | AVX-512 read bound | AVX-512 native write |
| --- | ---: | ---: | ---: | ---: |
| 64 whole bytes, identity map | 8.222 | 2.760 | 1.745 | 2.699 |
| 16-byte `{1,4,3}` motifs, rank order | 3.338 | 2.726 | 1.299 | 3.843 |
| Same motifs, cooperating kind order | 3.269 | 2.729 | 1.297 | 3.832 |
| 48-byte complementary packing, 48 selected codes | 5.856 | 2.820 | 1.611 | 3.636 |
| 60-byte packing, hot codes clustered | 7.447 | 3.661 | 2.172 | unsupported |

The two x86 profiles run on the same Zen 5 worker. AVX-512 wins all 32 bound-read
case/row-count combinations and all 28 supported native-write combinations;
median AVX2/AVX-512 ratios are 1.68 and 1.44 respectively. This compares complete
profiles, including target flags and calling conventions, not vector width in
isolation. The local NEON machine is an Apple-hosted VM, not Neoverse V2; its
numbers are not server ARM evidence.

Clustering is already a conflicting objective. In the last two table rows,
native read coverage grows from 48 to 60 bytes while selected scalar-write
coverage shrinks from 48 to 24 bytes. Zen AVX-512 scalar writes improve from
49.300 to 28.848 ns. But the clustered candidate lacks a native writer, so these
numbers cannot establish the best layout for a read/write mixture. Recipe
availability is part of the comparison, not an intrinsic layout cost.

On Zen, read preparation takes roughly 140–272 ns and write preparation
294–584 ns. Every prepared reader occupies 960 bytes; every writer 7,040 bytes.
The first representation stores several ISAs' controls and eight possible write
rounds even when fewer are active. These are measured baseline costs, not a lower
bound or an acceptable public representation. Compact per-backend controls,
shared schema admission and rotating-map behavior need comparison.
Allocated recipe size is not a count of hot bytes loaded by an invocation.

The generic scalar64 loop is not a strong specialized baseline. Scalar8 reads
only the first eight outer positions, so it is not equivalent work to a full
packet. `read_inline` still uses runtime controls. `weighted_bound` has an opaque
reader followed by a native order-sensitive consumer; it is not constant
specialization or fusion of reader and consumer. Writes include local value
checks and a tuple-relative coverage bitmap, not an Engine/Loom journal or
publication. No compound-mutation timing has been collected here.

Reproduce the offline tables without a worker or download:

```sh
python3 workbench/spikes/tuple-layout/runtime/analyze.py \
  workbench/spikes/tuple-layout/evidence/runtime-first-neon \
  workbench/spikes/tuple-layout/evidence/runtime-first-zen5
```

## The compound pressure test

[`composition_check.cpp`](runtime/composition_check.cpp) uses 128 codes to
represent the bytes of 32 unsigned 16-bit values. The caller binds one operation:
replace those values and return the modulo-uint64 aggregate delta. Code readers
and writers still know only byte fragments. The caller owns byte assembly,
little-endian uint16 interpretation and the sum.

The same caller contract works with reordered physical bytes, swapped bit edges,
and either contiguous storage or two independently leased 32-byte children.
The composer flattens child coordinates for a native region; each child also
has an independently valid schema. This is binding substitution across
representations, not migration or a universal flattening rule.
It does not execute through separately bound child endpoints, so arbitrary
recursive binding substitution remains an open probe.

The shell admits both packets, destination ownership/generation, nonoverlap and
effect capacity before any write. The no-fail before-write hook records two
qualified `(region ID, byte offset, length)` spans. A shared native body then
loads old payload, computes the summary delta and merges both packets before
issuing stores. There is no inter-packet failure or suspension point. It reads
old values for the aggregate even though the write replaces every stored bit.
An added partial-union witness leaves high fragments unselected in two thirds
of the bytes. The merged write preserves the intersection of both writers'
preserve masks; its summary comes from actual post-merge values, including
untouched bits. OR-ing the two new code packets alone would fail this case.

The test owner retains the binding, inputs and leases while awaiting invocation;
resumption rechecks the generation. Invalid input in packet two, stale mapping,
missing write ownership, insufficient effect capacity, overlapping destinations
and pre-entry cancellation leave data, summaries and effects unchanged. Completed
work is not replayed or undone by late cancellation; the owner explicitly decides
publication after the summary is ready. This models ordering and ownership only,
not Loom scheduling or Orbital/Engine visibility. No mandatory beforeimage or
immutable destination is needed: page COW remains compatible with the contract.

The initial 512 full-union compound checks pass in captured
[NEON](evidence/compound-neon-check/provenance.json) and
[AVX2/AVX-512](evidence/compound-zen5-check/provenance.json) runs. The suite now
has 1,024 successful full/partial compound substitutions and their rejection
scenarios, in addition to 6,144 schema/map checks, 6,144 scalar writes and 1,324
dense native writes. The expanded suite passes locally in optimized code and
in a captured [NEON ASan/UBSan run](evidence/compound-partial-sanitized/provenance.json),
and in captured [AVX2/AVX-512 runs](evidence/compound-partial-zen5/provenance.json).
Validation is recorded separately from the earlier timing snapshot; worker check captures use
one-iteration benchmark smoke runs, which are not performance evidence.

**Negative result:** inlining these generic recipes is not a good enough fused
implementation. The first optimized NEON compound entry occupies 3,712 bytes,
has a 240-byte stack frame and spills payload vector registers, in addition to
callee-save registers ([binary identity and stack accesses](evidence/compound-neon-entry.json),
reproducible with [inspect.py](runtime/inspect.py) on the recovered check binary).
This is the full-union snapshot, before the added preservation witness.
Its native ABI has not forced an output materialization,
but the composed body still creates too much live state and control flow.
Correctness of the composition therefore does not establish performance.

## What this changes in the next experiment

[HyPer's useful precedent](../layout-analyser/tuplepack-search.md) is specialization of a useful
processing region and measured amortization. Compare the same compound operation,
packet inputs, admission/effects and opaque outer endpoint in four forms:

| Variant | Distinction |
| --- | --- |
| A | Separate packet transforms and caller assembly |
| B | Concatenated generic transforms in one native body |
| C | The same composition with compile-time controls |
| D | Compose byte selections, shifts, masks and OR before lowering |

A/B tests boundary removal, B/C control specialization, and C/D algebraic
composition. The caller needs 64 bytes; expanding them into 128 code bytes and
reconstructing them may be avoidable. The simplifying rules must apply generally,
not recognize the fixture layouts. A wider caller-input contract is a separate
experiment, so it cannot silently account for a gain attributed to fusion.

Keep the complete operation, cold preparation, code/control size and reuse count
visible. AOT fixtures can establish achievable steady-state code, but their C++
build times are not predictions of JIT latency. The [retained-plan idea](../../notebook/ideas.md#plans-that-keep-improving)
remains relevant; this evidence does not choose JIT, CPS granularity, a search
algorithm or a sensing cadence.

Multi-tuple gather shapes, point/scan crossover, sparse SIMD mutation, wider
record composition and real layout-search regret remain unmeasured. The
[small exhaustive layout reference](../layout-analyser/tuplepack-search.md) is a promising way
to evaluate an analyser once competing operation families are credible.
