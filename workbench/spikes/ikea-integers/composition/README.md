# A 12-bit read with real children, placement and retained values

This bounded composition exercise reads 64 unsigned 12-bit integers, selects
positions whose value is below a cutoff, and sums the selected values. It
compares authored inline execution with three separately compiled CPS stages:
one selected parent read region, a common filter, and a common masked sum.

The two meaningful changes from the bitset exercise are **actual nested child
substitution** and **a decoded vector used twice**. The vector survives the
filter's mask production and is consumed again by the masked sum. No decoded
buffer is an implicit handoff. This is not a general graph execution system or
a proposed production Ikea interface.

Read the [authoring example](#authoring-and-physical-placement-are-separate),
then its [recorded scope](#recorded-scope-and-correspondence-with-execution),
[placement admission](#enclosing-placement-is-a-checked-obligation), and
[measurements](#checks-and-measurement). [Back to the spike](../README.md).

## Direction after review, 2026-09-10

The user found the authoring ergonomic at a glance but the examples too small
to establish scaling. The 16–57% CPS overhead is too steep as a blanket solution.
The factorisation target is `A × B × C × D → A × B + C × D`: curate fine
substitutions within inlined regions and permit freer substitution at coarser
boundaries. This is not yet a selected preset collection or universal boundary.

The independent [operation-granularity investigation](../../ikea-composition/operation-granularity.md)
develops matching and mismatched grains, multi-source coordinates, retention,
and iterator-like alternatives across several spikes. It also broadens locality
to block laws such as sorted unique appends, and distinguishes physical write
coverage from logical deltas and invalidations for future filter/aggregate/MVCC
integration. These interfaces are not implemented here. The [width-56 follow-up](../wide56/README.md)
now demonstrates matching native 8/4/2-value consumption and a separate encoding
region; it does not settle the harder handoffs.

## Authoring and physical placement are separate

[authoring.h](authoring.h) publishes the logical children:

```cpp
Column<Local4> local{101, {}};
auto scan = local.with_tail(Scan4{}, 202);
// local.values.body / local.values.tail
// scan.values.body  / scan.values.tail
```

`Body8` means the eight high bits of a 12-bit integer. `Local4` and `Scan4` name
the actual four-bit formats and refer to the root `LocalPack<4>` and
`ScanPack<4>` data definitions. They do not select an ISA. The same authored
read executes over either child's type:

```cpp
auto high = ops.read_body(source.body, group);
auto low = ops.read_tail(source.tail, group);
return ops.join12(high, low);
```

The consumer, also shared between recording and inline execution, reads once:

```cpp
auto values = read12(ops, source.values, group);
auto selected = ops.less_than(values, cutoff);
return ops.masked_sum(values, selected);
```

**Replacing a tail format is a data-layout change.** Moving from Local4 to
Scan4 here also rearranges the enclosing body's bytes. The replacement source
ID names a complete new encoding, and both encodings can remain live. The
helper does not migrate bytes, reinterpret the old payload, or implement an
execution-only rewrite. The scalar fixture actually creates the different
payloads from the same unsigned integers before binding either one.

The parent placement is an independent explicit choice supplied to `place`:

| ParentKind | Logical children | Concrete 96-byte extent for 64 values |
| --- | --- | --- |
| `packets8` | Body8 + Local4 | eight repetitions of `[body8, tail4]` |
| `body64_tail32` | Body8 + Scan4 | `[body64, tail32]` |
| `body32_tail32_body32` | Body8 + Scan4 | `[body32, tail32, body32]` |

The last two have the **same Scan4 child** and unchanged tail bytes; parent
placement moves them and the body's two halves. Neither body geometry nor the
choice between those parents is owned by `TailKind`. They select distinct
named compiled parent read regions. Adding the third parent required placement
descriptors, admission checks, native address calculations and wrapper
registration. It required no new tail format, tail kernel or consumer. The
authored function remains unchanged. For example:

```cpp
auto where = place(storage, ParentKind::body32_tail32_body32,
                   scan.source_id, 32, tile_count);
prepare(scan, where, 2048, Execution::cps, executable, error);
```

Using the new parent requires bytes already encoded for that placement. It
does not move an existing body-first source. The benchmark keeps all three
physical representations and all six prepared executions alive together.

## Recorded scope and correspondence with execution

[graph.h](graph.h) records five operations. The joined value has two edges:

```
%0 = read high8 child values.body
%1 = read low4  child values.tail
%2 = Join12(%0,%1)
%3 = LessThan(%2, cutoff)
%4 = MaskedSum(%2,%3)
```

`Group16` and `Cutoff` are explicit symbolic *external inputs*, not stored
constants or sampled values. One graph invocation covers sixteen integer
positions. Its enclosing environment supplies group index 0, 16, 32 or 48 within
a 64-position tile and a cutoff from 0 to 4096. The driver owns tile/group
repetition and the sum of these partial results; this graph does not record
that outer loop.

The binder supports one named read/filter/sum expansion and three explicit
parent read implementations. It checks the recorded graph's source, result,
operation identities, child paths and dependency lists exactly against that
expansion before installing either inline or CPS execution. This validator
remains intentionally exact. An edited graph or a changed authored expansion
is rejected until a matching compiled program is provided. It cannot retain an
arbitrary graph beside the old execution table.
This is deliberately an explicit compiled-program mapping, not general graph
lowering or a claim to support arbitrary branching and reuse.

The selected read regions themselves invoke the shared authored `read12`
function through `NativeOps<Tail, Parent>`. They combine bounded body loads
with the root's immediate `local_pair<4>(p0,p1)` or `scan_read16<4>(tail,group)`.
The local pair accepts the two nonadjacent four-byte tails; the contiguous-tail
`local_read16` interface would be wrong for `[body8,tail4]` repetitions.

## Enclosing placement is a checked obligation

[prepared.h](prepared.h) and [prepare.cpp](prepare.cpp) contain target-independent
storage ownership and placement descriptions. Published body/tail addresses
record first offset, repeat stride, repeat value count and repeat byte count;
these are repetitions *within* the enclosing 64-value, 96-byte tile. The next
outer tile follows the enclosing stride, not an extrapolated child stride.

Cold preparation requires the actual source ID, compatible tail format and
explicit parent, exact 96-byte outer stride, exact 64-value grain, complete
readable tile extents, identical retained storage ownership for both children,
and the correct nested offsets/repetitions. It rejects detached tails, changed
child extents, padded outer strides and non-32-aligned starts. Cutoff and scalar
sum capacity are checked here too. The native build's target remains an
executable requirement; the small binder is not a portable multi-ISA dispatcher.

The retained storage must have no mutable aliases while used. Each prepared
object owns the enclosing allocation and its immutable instruction table.
The hot call receives erased attached metadata; it performs no validation,
allocation, `std::expected`, schema traversal or child-pointer lookup.

With a 96-byte stride and admitted cache-line phases of 0 or 32 bytes, all three
parents fit each point and aligned group of 16 positions in at most two adjacent
64-byte lines. Local groups consume exactly 24 contiguous bytes. Scan groups
consume 16 body bytes plus 16 tail bytes; the body-first maximum envelope is 80
bytes, while the tail-middle maximum is 48 bytes. The enclosing phase is
essential to the body-first proof; an arbitrary distant tail pointer is not an
equivalent placement witness. The [locality audit](../locality/README.md) gives
the wider width/residue bounds independently of these kernels.

## Real native handoff and its cost

The ordinary ABI carries these arguments at every stage:

| Operand | x86 System V AVX2 | AArch64 |
| --- | --- | --- |
| cursor, tile, group, cutoff, accumulator, mask | RDI, RSI, EDX, ECX, R8, R9D | X0, X1, W2, W3, X4, W5 |
| sixteen decoded u16 values | YMM0 | Q0, Q1 |
| scalar completion | RAX | X0 |

The driver makes one indirect call per group. Read and filter stages advance
the three-entry instruction table with a genuinely indirect `musttail` jump.
The sum consumes the original decoded values plus the new scalar position
mask, adds the partial result to the accumulator and returns to the driver.
All stage TUs compile independently without LTO. Local inline structures never
serve as opaque mixed aggregate return types.

Decisive release instructions from pinned Clang 21.1.8:

```asm
# AVX2 filter: preserve decoded YMM0, write mask R9D, tail jump
vpcmpgtw %ymm0, %ymm1, %ymm1
vpmovmskb %ymm1, %eax
pextl %r9d, %eax, %r9d
movq (%rdi), %rax
addq $8, %rdi
jmpq *%rax

# AArch64 filter: preserve Q0/Q1, form mask W5, tail jump
cmhi v3.8h, v2.8h, v1.8h
cmhi v2.8h, v2.8h, v0.8h
# weighted reductions into W8/W9
orr w5, w8, w9
br x6
```

The sum's disassembly then masks the same YMM0 or Q0/Q1 before its horizontal
sum. There is no decoded-vector spill/reload between stages in the inspected
ARM and AVX2 release builds. The AVX2 local read does save/restore RBX while
performing its scalar transpose fallback; scan reads, filter and sum have no
stack frame. ARM stages have no frame. Driver frames save ordinary loop state,
and drivers zero unused incoming vectors/mask before every read invocation.
These findings concern these actual bodies, not a general register budget.

The common scalar mask also has a visible cost: the filter packs native
predicates to sixteen bits, and sum expands those bits to native masks. The
current inline executor uses the same scalar-mask semantic carrier; this probe
does not yet compare a native-predicate carrier or a fused filter/sum region.
Dispatch, constant placement and register pressure remain real differences;
register residency alone is not a performance conclusion.

## Checks and measurement

The check executable covers every 12-bit value at both phases in all three
layouts, all 65,536 position masks, zero/one/many tiles, cutoff boundaries,
retained lifetime, incompatible parents, corrupted child descriptors and edited
graphs. Native ARM release, native ARM ASan/UBSan and AVX2 under QEMU pass
1,536 decoded groups, 432 prepared execution combinations and 24 exact footprint
probes. During ASan checks, all unrelated storage bytes are poisoned around
each direct `read12` invocation; only its exact 24-byte local span or two 16-byte
scan spans are unpoisoned. This checks actual generated accesses in addition
to the independent byte-dependency calculation.

The parent adds this directory and builds `ikea_integer_composition_check` and
`ikea_integer_composition_bench`, linking `ikea_integer_composition`.
Standalone CMake also works with Linux Clang 21.1.8. The benchmark accepts a
tile count (4,096 by default), pins one allowed CPU, calibrates each case to 20 ms,
and executes five repetitions sequentially. All six cases perform the same
filter/sum on the same integers. Fixture encoding and preparation are outside
timing. CSV records extent, phase, repetitions and checksum. Repeated warm
execution is a resident control, not a proof of a particular cache level.

The three-target resident run gives these medians in ns per integer (five
sequential repetitions, 4,096 tiles, 393,216 payload bytes per active source):

| Target | Local inline / CPS | Body-first Scan inline / CPS | Tail-middle Scan inline / CPS |
| --- | ---: | ---: | ---: |
| Zen 5 | 0.1013 / 0.1257 | 0.0821 / 0.1148 | 0.0819 / 0.1257 |
| Granite Rapids | 0.1401 / 0.1731 | 0.1178 / 0.1622 | 0.1178 / 0.1846 |
| Neoverse V2 | 0.4376 / 0.5069 | 0.2700 / 0.3182 | 0.2700 / 0.3227 |

The decoded value really survives across both consumers, but these CPS
boundaries still cost 16–57% relative to the authored inline execution. They are
an observed tradeoff, not an accepted performance budget. The native-predicate
carrier and fused filter/sum comparison remain open. Changing body placement
has little effect in the inline cases; the x86 CPS difference warrants further
attention before assigning a placement cost independently of generated code.

Individual repetitions, checks, hardware context and the complete recoverable
source/assembly are retained for [Zen 5](../evidence/composition-zen5/provenance.json),
[Granite Rapids](../evidence/composition-granite-rapids/provenance.json) and
[Neoverse V2](../evidence/composition-neoverse-v2/provenance.json). The
[access audit](../access-audit.md) covers these hardware read bodies.

Full assembly belongs in the parent run's ignored raw outputs. This note keeps
only the decisive handoff instructions. Hardware numbers and captured run
provenance belong to the parent experiment; development-host checks and QEMU
execution do not establish target performance.
