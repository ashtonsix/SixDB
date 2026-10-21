# Native directory increment, 21 October 2026

Actual C++ is in `engine/include/engine/directory.h` and
`engine/src/directory.cpp`; `engine/examples/directory_driver.cpp` exercises
independent actual Segment material and one joined Orbital publication.
This is a performance-led physical-prefix increment, not the full ordered
index directory. Logical Boundlist seek/continuation, ordinary/hash choices,
representation schema/extras and membership insertion/deletion remain owed.
It neither assigns physical record keys nor changes the supplied five layers.

The discarded stable-name implementation and API-bound row driver/test were
removed. The independent logical and row input/oracle headers survive under
`checks/`. Functional receipts for the discarded code establish no property of
this replacement. The earlier paper pause is superseded.

## Implemented operations and ownership

`open` makes one immutable native Layout (one 576-byte Part) and a small caller
handle for a fresh native index. Reopening existing material and admitting
historical routing formats are not implemented yet. It borrows the actual caller
Store, Loom Worker and metadata resource;
the resource and owner Lane outlive every descendant, Scope and exported Part.
A layer's root ObjectId is `(directory_namespace, layer+1)`. No directory child
is a logical Object; its ownership is physical interpretation of that root.
No Engine MVCC, enrollment, undo, journal or second publication mechanism exists.

`build(layer, sorted unique entries)` checks every prefix against that layer's
domain before making private state. Sixteen consecutive original prefix
positions form a packed group; only present entries occupy bytes. Groups retain
their original 16-bit masks. F32 routing uses five bits per step over the
remaining group number. The root's common high prefix is compressed, with an
inclusive group minimum/maximum guard; child levels then decrease shift by five.
This bounded radix path needs no occupancy rebalance for replacement. It does
not yet implement directory membership changes or logical routing balance.
Only the completed root is captured. The caller publishes it.

`read(scope, layer)` validates same-Core scope admission through Orbital and
selects one root. It checks the cached interpretation once. The 8-byte `Read`
is a borrow for that Scope; `find(prefix)` checks the root range, follows native
pointers, tests the original suffix bit, then computes its packed rank. There
are no per-hop Object selections or public cursor copies. An absent root or
prefix yields no entry. Other layer/Core/interpretation errors are explicit.
The same Scope must protect the independent Segment material reached by a
locator, including first discovery after later publications. Root ownership
alone does not preserve that logical data cut.

`replace` selects the original root and validates existing membership, records
at most six routing nodes, copies the packed group and each path body, patches
all 64 dirent bytes and the changed child pointer, then captures one finished
root. All bodies of this scalar path share one finite native Allocation, at
most 4,480 bytes. Deep/wide paths above Loom's current3,072-byte finite cutoff
use the actual Window allocation path; they do not inherit the2,176-byte
example's acquisition cost. Immutable Proof nodes have separate reference counts, even
inside one metadata allocation. A dead node releases its own outgoing edges
immediately; a surviving child can retain backing bytes but does not preserve
dead ancestors' dependency trees. Unchanged child Proofs are retained directly.
There is no predecessor-root link. Refusal/discard publishes nothing.

`replace_many` accepts sorted unique final intents, checks sorted/domain
admission, then validates membership while visiting the union of old paths. A refused private candidate unwinds without publication. Each
affected group and routing node is copied once; there is still one root capture.
This increment gives each changed group/node its own native Allocation. That
avoids making one rarely changed group retain a whole dispersed batch's bodies,
but its acquisition/release cost is visible and currently too high. Caller-side
repeated commands are coalesced inside the measured interval in both arms.
The public directory method deliberately accepts final unique intents.

The closed fixed64B producer qualifies actual Segment/locator association
outside the clock for every entry. Its same-Scope native pointers can then
avoid redundant per-write Object lookup. It allocates and constructs the
finished independent Segment Part, captures it, derives its byte
address, edits the directory and joins all Segment changes plus the root in
ONE `prepare/install` packet against the original Scope. It releases local
owners and pays bounded catalog maintenance after the Scope ends; residual
maintenance is also inside the complete write-arm clock. This is a native
64-byte moving-Part producer, not a row-update engine or an opaque numeric
locator replacement.

## Byte and field ledger

These are native 64-bit ABI sizes, not a persistent wire format.

| Item | Bytes and meaning |
| --- | --- |
| Layout | 80: owner-Lane references8, resource pointer8, namespace8, representation4, private kind4, immutable PMR geometry vector32, immutable interpretation shared pointer16. Geometry payload and shared control blocks are extra. |
| Geometry | 16: extent8, alignment4, padding4. One entry per Part. |
| Control | 24: Layout pointer8, full Object name8, first ordinal4, projected flag1, owner recipe1, reserved2. Definition is one retained Layout pointer8. |
| Directory handle/state | 8-byte unique state pointer; state40 (Store/Worker/resource references24, namespace8, Definition8). Entry72 (prefix4, ABI padding4, Dirent64); Work48 (six u64 counters). |
| Dirent | 64: six u64 Part locators at 0..47; representation u32 at48; mapped flags u16 at52; extra rank u16 at54; extra block u32 at56; reserved u32 at60. Current producer uses native locator0 and representation1; other words are preserved opaque metadata, not implemented schema/extras. |
| Packed group | `64 * popcount(mask)`; original suffix rank selects the dirent. Its mask resides in the parent slot. No group header or hidden slot map. |
| Routing body | 576: count u16 at0; shift u8 at2; reserved5 at3; group low/high u32 at8/12; write-side Proof pointer u64 at16; reserved40 at24; 32 slots at64. |
| Routing slot | 16: child byte pointer8, original suffix mask2 (only at shift0), reserved6. Null pointer means absent. |
| Proof header | 24: Bundle pointer8, owner-Lane reference count8, outgoing slot count4, ABI padding4. A routing Proof has 32 following 8-byte child Proof pointers (280 total); a group Proof has no outgoing slots (24 total). |
| Bundle header | 64: resource reference8, native Allocation32, live-node/guard count8, metadata allocation extent8, used metadata bytes8. It owns one real native Allocation. Dead Proofs release outgoing edges separately; the whole body/metadata allocation frees only after its last node. |
| Root physical owner | One PMR `allocate_shared<Owner>` with an 8-byte root Proof handle; shared-control-block size is library-dependent and is not included in Work.proof_bytes. Orbital capture retains this backing. |
| Root Representation | Control24 plus Orbital header8 plus independent shared Part slot24 =56 requested bytes. Layout and catalog/version/packet metadata are additional. |
| Segment Representation | Control24 plus Orbital header8 plus native allocated Part slot16 =48 requested bytes for the one-Part producer. Segment body64 is an independent real Allocation. |
| Read | 8-byte borrowed root pointer. |
| Path scratch | Six Node pointers48, six unsigned slots24 and scalar counters. Replacement Dirent copies are full64. |
| Batch caller scratch | At most64 final entries and65 finished changes, plus command ordering/before-material pointers. Stack/capture/catalog costs remain charged in actual timings. |

`Work` counts native body requests/bytes, copied body bytes, root captures,
Proof bytes and actual unchanged-child retains. It is a logical work ledger,
not total heap/RSS: Bundle headers, shared-control blocks, Layouts, PMR pool
capacity, actual capture/catalog/version/packet metadata and native allocator
rounding are extra. `settled_native_footprint` uses actual Pool statistics after
catalog drain. `mapped-idle` includes active rounded native mappings, not PMR
metadata or process RSS, and is not exact logical body size. A surviving sibling
can keep unused portions of a scalar path's backing alive; this remains a cost.

## First measurements and decisions

Pinned Clang21.1.8, Linux AArch64/neoverse-v2 tune on the existing local machine,
CPU1, optimized assertions-on build. Four counterbalanced samples per screen,
complete per-arm read warm-up, 262,144 uniform queries, one Scope/read transaction
per64 queries, identical full64-byte XOR8/sum output. LMDB0.9.31 uses native
`mdb_get`, native reserve/write and commit. `NOSYNC|NOMETASYNC|NOTLS`: these are
memory-only CPU comparisons, not durability parity, latency percentiles or
complete Engine results. Old/fresh readers first discover data AFTER publication
and verify every material byte plus actual Engine binding/locator association.

The first per-node-capture version had fast reads (~4.7–5.0ns at8K) but warm
complete scalar moving-Part writes ~1.8–1.9us, near LMDB's ~1.8–2.2us. It was
replaced immediately with one root capture and individual physical Proof
lifetimes. This preserves the direct-read route and removes unnecessary private
Representations, but does not establish sufficient write headroom.

The one-root screen at8K dense entries had reads4.64–4.83ns versus LMDB171.9–174.4ns.
Warm scalar full writes1.74–1.93us versus LMDB2.10–2.23us; first trial2.87us.
These are loop means, not p99. One scalar directory attempt copied2,176 bytes in
one native Allocation, captured one root, created584 Proof bytes (plus Bundle
and capture/control metadata) and retained46 unchanged child edges.

B64 dense8K copied the union: first observed batch77 native directory allocations,
71,232 copied/body bytes, one root capture,6,200 Proof bytes,452 unchanged retains.
Full producer cost0.83–0.96us per OFFERED command versus LMDB0.83–0.98us. Both arms
coalesce duplicate commands and perform one atomic publication per batch. These
results miss the headroom objective; do not grow an ordered directory around an
assumed write win.

A separate instrumented warm diagnostic (phase clocks perturb the operation)
attributes scalar ns roughly178 basis/validation,206 data allocation/copy/capture,
877 directory copy/capture,176 prepare/install,511 local cleanup plus paid
maintenance. B64 per offered command:133,235,336,94,262 respectively. Do not
subtract these from a different timed trial or infer an irreducible component
floor. Source owners are checking for misuse of existing Orbital/Loom facilities.
The directory must reduce real acquisitions/proof work; batching alone did not
solve the full composition cost.

Selected [current-source screens and qualification](evidence/native-increment-20261021/README.md)
retain the final executable source identity and recovery of source/binaries.
Raw local logs and superseded experiments are in ignored
`build/engine-native-path-20261021/`. Earlier source revisions and instrumented
diagnostics are not the final comparison. Calico
has not been rerun for this native library increment. Earlier Calico controls
remain separate historical evidence.

Build the checks normally. The comparison executable is opt-in so ordinary
Engine/unrelated configure has no LMDB dependency:

```sh
orb -m ubuntu cmake -S . -B build/clang/engine-directory -DSIXDB_ENGINE_DIRECTORY_DRIVER=ON
orb -m ubuntu cmake --build build/clang/engine-directory --target engine_directory_check engine_directory_driver -j1
orb -m ubuntu taskset -c 1 build/clang/engine-directory/engine/engine_directory_driver 8192 1 4096 4 normal 64
```


## Current source and the multi-row caller

The current relocation peer is stronger than the first screen above: native
u32 `INTEGERKEY`, one cursor with `CURRENT|RESERVE`, trial-distinct afterimages
and independent sequential full-row expected states. Final8K B64 samples span
0.80–0.93us Engine versus0.69–1.05us LMDB per offered command. At262K they span
4.00–4.61us versus1.84–2.17us; reads35.98–45.79ns versus317.7–423.7ns. The large
native read path has not reproduced the earlier captured graph's21.7ns result.
Allocation placement, native layout and caller differences remain unresolved;
these are separate source/semantic contexts, not a causal attribution. Relocation
writes and large-directory headroom are unresolved losses, retained alongside
common-path wins. Do not infer complete Engine acceptance from either.

A tested3072-byte directory batch packing variant reduced the8K witness from77
to26 native requests, but copied the same71,232 useful bytes while requesting
79,872 and increased settled active mapped bytes from1,159,168 to1,658,880.
Its one exploratory screen was slower; that variant was removed. This is a
narrow negative observation, not a universal rejection of bounded packing or a
source-qualified performance ranking. Current batch code again uses independent
group/node backing. Per-node dependency retirement remains separate from backing
retention. Driver PMR pools344/648-byte bundles with a1024-byte cutoff; a65-change
packet's1040-byte identity scratch and1040-byte Mutation array still use upstream
allocation. There is no new Loom facility or hidden per-Segment scheduling cost.

The actual multi-row caller is `engine/examples/segment_driver.cpp`. It has
multiple records per base Segment, unique48-bit physical keys (prefix*17,
16-bit slot) and stored logical IDs in duplicate runs of3, preserving full
logical order. Timed point requests address physical rows; logical equality and
duplicate continuation remain owed with logical directory routing. All five
layers hold actual nonempty Orbital Segment Objects and directory roots.

Each native Segment has independent64-byte header and body Allocations.
Base body rows are64bytes: logical ID plus seven u64 payload fields. Base header
stores row count u32, with60 zero reserved bytes. Upper header similarly stores
cell count; upper body has352-byte cells: position u16/reserved6, records u64,
first/end ordinal u64 each, then8 fields of40bytes (min/max/sum low/sum high/
not-null, allu64). Cell-body allocation rounds to64bytes with zero padding.
These are driver formats, not a settled representation schema.

Field3 updates use an ordinary Orbital Object per64 physical slots:
`ObjectId(3,(physical_key>>6)+1)`. Its576-byte COW Part contains dirty mask u64,
56 zero reserved bytes, then64 u64 aftervalues. It stores database afterimages;
Orbital alone supplies version selection and retirement. New page capture and
one `prepare/install` packet implement the atomic batch. The native base material
and all directory facts stay unchanged; outside-clock checks verify all five
root Object versions unchanged. No Engine undo or separate MVCC is present.
The driver reader starts through the Engine directory, then selects correction
pages under the SAME Scope and caches up to8 selected/absent page borrows within
that Scope. Base and summary interpretation includes this correction recipe.

Full rows are compared with independently applied sequential expected updates
and native LMDB. All upper cells/all eight native fields use independent wide
arithmetic expected results. Effective field3 summaries recompute the selected
row interval through snapshot-correct afterimages, including exact min/max and
SUM128; aggregate throughput is not measured here. There is no claim that this
recomputation is the final aggregate policy. Late old/fresh first reads, scalar
and atomic batches, duplicate final intents, cross-Segment spans and all65536
slot positions are checked, including sanitizer/leak runs.

Initial common-path screen,2 Segments ×1024 rows, same local compiler/CPU,
Scope64/full-row output, native u64 `INTEGERKEY` plus cursor `CURRENT|RESERVE`,
coalesced duplicate final intents in one LMDB transaction, paid Engine maintenance:

| Workload | Engine read ns | LMDB read ns | Engine update ns/offered | LMDB update ns/offered |
| --- | ---: | ---: | ---: | ---: |
| Hot64 rows, B1 |8.03–8.71|86.9–90.4|411.6–428.7|1848.8–1893.7|
| Hot64 rows, B64 |8.13–8.46|88.4–129.4|18.2–19.9|113.0–155.5|
| Uniform2048 rows, B64 |28.7–31.4|126.2–135.1|191.6–215.3|408.7–487.0|
| Uniform10240 rows,80×128, B16 (2 samples) |26.4–26.9|156.5–158.4|377.5–414.1|1293.7–1323.0|

Hot64 fits one correction page and strongly amortizes capture/cache work;
uniform/dispersed cases exert different pressure. These are short screens with
retained repetitions and state transitions, not percentile/precision evidence,
durability results or comparison with the strongest possible LMDB layout.
No current Calico run exists. Common-path headroom is promising; the uniform2048
write envelope is tighter, the relocation loss remains, and a general ordered
index and complete metadata/backing/RSS accounting remain owed.

## Staged physical access in the real library

`Read::find_many` now exposes independent physical probes in input order,
returning borrowed Dirent pointers/null misses. Output capacity is checked before
any mutation; source and destination storage must not alias. It keeps one
32-lane512-byte `{Node*,key}` bank and completes a demand-load pass across all
lanes at each internal radix level before final original-mask/rank selection.
The caller then stages native locator and full64-byte consumption separately,
under the SAME Scope. It makes no catalog call or ownership change per probe,
and adds no prefetch, Task, Ring or new reader protocol.

The earlier21.687ns result used this level-wavefront strategy; the first native
library driver used serial `find` followed immediately by material consumption.
Comparing36–46ns serial with21.687ns staged did not isolate a layout regression.
The current library implementation restores the strategy through a generic
public API. It does not call the retained fixture-specific kernel.

The [source-qualified ABBA screen](evidence/staged-library-20261021/context.json)
keeps all samples:262K scalar38.768–44.277ns versus staged21.333–36.306ns; the
second staged process gave21.333/21.390ns. This is a short throughput screen,
not precision or a p99 claim. Same root, data, Scope64, full64-byte output,
material/locator association and allocation strategy; write counters and settled
native mapping agree. The64-command write intervals in this read screen are
not new write-throughput evidence. Existing relocation losses remain.

Emitted-path review confirms per-level lane advancement, no scalar `find` or
prefetch in the staged hit path, and no helper call there. Expected<void> returns
in a register. The generic API has extra pointer-bank traffic compared with the
old integrated leaf: actual peak caller+callee stack is1,328bytes (caller736,
callee592), including banks, ABI saves and spills; the old integrated leaf used
512bytes with caller-saved registers. This cost is measured rather than erased.
Independent tests cover every radix shift0/5/10/15/20/25, tails through97 probes,
empty roots, MAX prefixes, misses/repeats, old/fresh snapshots and unchanged output
on capacity refusal, including ASan/UBSan/leak qualification.
