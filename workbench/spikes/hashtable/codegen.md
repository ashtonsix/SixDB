# Complete-caller code generation

Inspect the executed `dependent` and `independent` consumers, not just field
wrappers. These observations qualify the timings; instruction counts do not
measure cycles or assign a fraction of elapsed time to an operation.

## Local reduction, byte addressing and prepared link layouts

These are **code-generation diagnostics, with no timing claims**: pinned Linux
Clang 21, local AArch64 Release flags, explicit SIMD, K16/K40 maps at A10.
They do not establish the native V2, Zen5 or Granite result.

The local-reduction capture
`a2e73e165b21780b0aa334f714649121e71d4be017aeead95cb5194681f4406b`
keeps the checksum in a loop-local register instead of updating a borrowed
accumulator. Candidate primary seeds and bases still reload for every key.
Ordinary Abseil also receives this source change; its serial shape check moves
outside the loop. This is not an additive measurement of checksum-store cost.
Selected complete callers and binary identities are in
`build/hashtable-design/reduction-review/{qualification,selected}.json`.

The byte-coordinate-only comparison uses the preserved binaries
`build/hashtable-design/reduction-before-byte-address`
(`5e29e1d9aed2aa1620b3d58091a8af4b946198bbc8b9f0bc621efa1e8e008582`)
and `build/hashtable-design/reduction-byte-address`
(`27ac683dfa0b8abd288582c694f61b091d23c2bb20140a3971f9a64f3c20f00c`).
`recipe_view::at` computes byte-addressable fields in byte coordinates; bit
fields retain bit coordinates. K40's primary value address loses an arithmetic
instruction, but its three-byte suffix gains one address instruction relative
to the old multiply-add/shifted-add lowering. K16's primary value path has the
same instruction count. Complete composed serial helpers shrink 1096→1092 B
(K16) and 1208→1168 B (K40); direct controls change similarly. Metadata reloads
and cold link-format guards remain. Exact function ranges, hashes and the
extraction script are in `build/hashtable-design/reduction-byte-review/`.

The isolated pointer experiment retains its spike-header snapshots,
source variants, compiler commands, objects and assembly in
`build/hashtable-design/read-pointer-review/`; `qualification.json` identifies
every source and object by SHA-256. These are explicit-instantiation probes,
not the final linked benchmark. Capturing the body pointer by value leaves both
serial helpers byte-for-byte unchanged; independent helpers only exchange
closure-field offsets. Copying that capture to a loop-local pointer hoists the
owner-pointer load, but leaves seeds and bases inside the loop.

The smaller successful probe caches the immutable shared `formats().data()`
pointer in each `links` constructor and makes `bind` use it. Its modified header
is `read-pointer-review/prepared-snapshot/hashtable-design/links.h`, alongside
the unchanged `snapshot/` control. All-tier read loops then contain no cold
format-initialization calls, inline into `format_trial`, and load primary and
secondary bases/hash facts before iteration. Original-reference and local-pointer
whole-trial assembly is identical for both widths: no table read-view API is
needed to obtain this result. K40 still spills a derived primary value base on
the hit path. Linux `sizeof(links)` grows 120→128 B and the promoted map owner
288→296 B; layout storage remains shared, with no per-block pointer.

The actual linked prepared binary `build/hashtable-design/reduction-prepared`
has SHA-256
`2c01f7852ae98271675e09b5d9b55114cdd32f3b22dbeaa6564add1379ef7122`;
its source manifest is
`2013b46d6f86a483ebf11d70ab78e6dffcd5a5b2c702eb57ec3eba8c6756238e`.
The subsequent local audit covers K8/A6 and K16/K40/K64/A10, all three container
kinds, composed/direct exact buckets and both ordinary Abseil baselines: 144
hit/miss/serial regions plus 48 update regions. Every selected read either
inlines or calls a whole-loop helper whose lookup and value consumption have
no calls. The cold format guards are gone, including MM bag traversal. Baseline
hot search remains fused. This does **not** mean all metadata hoists: K40/K64
MM serial callers hoist primary base and input/bucket seeds but still load the
selector seed and conditional downstream metadata. K16 MM serial also
reconstructs the four-instruction hash constant inside the loop, where the
byte-only control kept it outside. Such register-allocation changes remain
charged and need timings; a guard-free loop is not automatically optimal.

Mutation fusion is still incomplete. All 24 selected candidate update loops
call `promoted_table::try_set`'s inner lambda once per row, including sets and
maps. For K40 map/composed the call is at `0x6ddacc`, before bucket search.
Ordinary Abseil fuses its update search; its remaining calls are on insertion
or growth branches, plus the MM vector's append slow path. Constructor
admission alone does not close this boundary.

Recovered local excerpts, hashes, CFG extraction and DWARF clock locations are
in `build/hashtable-design/prepared-linked-review/`. Its `boundaries.py` expands
outlined `format_time` calls before assigning source-order phases, checked
against `formats.cpp`'s DWARF callsites. Five prepared timer wrappers moved out
of their root functions; the older fixed-clock-ordinal audit can consequently
mislabel updates as serial reads. Lambda numbering is not a phase identifier.
These linked findings remain local generated-code evidence, not native results
or measured performance attribution.

## Local mutation-closure probe

The subsequent isolated mutation-closure probe is retained under
`build/hashtable-design/mutation-inline/`. Both frozen binaries use admitted bag
append and local read reductions. Marking the immediate mutation lambda
`always_inline` moves every sampled per-entry boundary outward to the
unannotated scratch `try_set(key,value)` overload; it does not fuse search into
the caller. All twelve K16/K40 × set/map/MM × A6/A10 candidate updates still call
before searching. The modified binary also outlines overflow `find` and
`commit_record`; all four MM cases retain the preparation callback even on
primary hits. Ordinary Abseil's hot search stays fused. DWARF qualifies all 48
candidate/baseline timer endpoint pairs in the two binaries.

Local CPU1 ABBA runs retain 7,776 raw phase rows and eighteen samples per
variant/case. Logical bytes, backing, metadata and populations are identical.
Several map updates improve, including K16/A10 7.67→4.91 ns and K40/A10
7.66→6.61 ns; MM updates range from .922 to 1.050 of control. These are local
OrbStack diagnostics with unchanged-body placement controls, not target-native
claims or a demonstrated general fusion fix. The attribute is not promoted to
the live mutation body.

## Historical native receipt

Source capture: `a70cee947eafddc5a50305ffcf1d94b60aa86a7bd9a2a3f52f69ed754677f2e7`.
Each retained evidence directory contains an `artifact.json` recovering its full
bundle, including `assembly.gz`, `source.tar.gz` and `source-manifest.json`.
Locally those files are under `build/workers/<job>/results/`.

| Target | Job | Evidence |
| --- | --- | --- |
| V2 NEON | `20261013T110047Z-e61cace7` | [first-neon](evidence/first-neon/artifact.json) |
| Zen5 AVX2 | `20261013T110057Z-32c22741` | [first-zen5-avx2](evidence/first-zen5-avx2/artifact.json) |
| Granite AVX-512 | `20261013T110104Z-76b51a05` | [first-granite-avx512](evidence/first-granite-avx512/artifact.json) |

Complete identity admission removes prepared TuplePack offset/shift/mask loads
from primary and secondary lookup. K16/A9 fuller composed/direct dependent
callers have identical instruction sequences after normalizing local targets:
120 instructions on NEON, 109 on Zen5 and 108 on Granite, including cold paths
and padding. Their independent callers likewise match apart from relocations
to equivalent link helpers/statics. Primary and secondary kernels are fused;
chain lookup/resolution can still call outlined helpers.

K40 has small equivalent differences: NEON reverses the order of the same two
bounded loads assembling a three-byte suffix; x86 reverses operands of equality
comparisons. These are not generic field-control interpretation.

Granite K16/5000 half-miss independent timings separate fuller and direct
(approximately 5.74/4.35 ns), despite matching executed instructions and branch
structure. All twelve fuller repetitions contain zero chain entries, so the
different link-helper/static addresses are not exercised. The separation
persists across arm rotations. Its cause is unresolved; code/data placement
and predictor effects are hypotheses. **Do not attribute it to primitive
overhead.**

There is a baseline qualification: both x86 receipts outline ordinary Abseil
`find` in the K16 and K40 dependent callers. Granite K16, for example, stores the
key on the stack and calls `find` at `0x2d800` on every iteration. Independent
Abseil callers inline it; NEON dependent and independent callers also inline it.
The original x86 serial results therefore do not establish a matched-fusion
advantage. They remain observations of that build, not the preferred baseline.

On V2, the candidate reloads table bases/seeds inside the dependent loop while
Abseil hoists its immutable context. Occupied-prefix filtering adds a count load
and dynamic mask; the 16-byte match uses `SHRN` before scalar lane extraction.
Fuller's K16 value plane starts at byte 21, so eight-byte values are unaligned
and slot 5 crosses a 64-byte line. Compact also sends substantially more keys
downstream. These are observed instructions, geometry and route counts—not
measured contributions to the hot gap. Both primary paths use multiply/fold
hashing; the evidence does not support blaming an extra candidate multiply on
every primary hit. No PMU or llvm-mca decomposition accompanies this review.

## Composition implications for implementation

The later scan32 comparison confirms identical full-width composed and direct
selector loads in selected native loops. Complete identity admission above also
removes TuplePack's runtime offset/shift/mask interpretation. These observations
separate shared hash, occupancy, candidate-walk and ownership work from actual
generic primitive costs; a new primitive API cannot be assumed to recover the
remaining shared container deficit.

Three authoring opportunities remain, with different evidence:

- `fields.h::identity` proves prepared controls and reconstructs TuplePack
  internal controls to make them compile-time constants. The measured admission
  benefit should become maintainable through a small author-level facility,
  preserving the original projection and exact transfer footprints. Prepared
  state belongs to shared geometry, without per-bucket binding. Nonidentity
  packed projections must shape that facility too.
- A composition of projections currently calls each child reader. An admitted
  register result could instead serve suffix confirmation and subsequent field
  consumption. The [row experiment](lookup-consumption.md) exercises this idea,
  but local complete-operation timings are mixed. Sets need a predicate; maps
  may consume selected fields; tagged multimaps select inline bytes or an
  external reference. Wide values still need bounded multiple transfers, without
  requiring a complete temporary record for every operation.
- Exact 17–31-byte selector tails may benefit from a bounded native group path
  using overlapping AVX2 or masked AVX-512 loads, avoiding assembly from two
  128-bit reads. Read permission must remain separate from semantic lane count.
  Full 32-byte windows already lower well; complete-operation benefit for tails
  remains unproven.

Damage substitution remains with the owner: observe a bucket before executing
its field bodies, or erase that observation for scratch. Exact byte/RMW
footprints still support finer-grained callers. Bag blocks, directories and
allocation changes retain separate identities and publication events. These
are implementation recommendations and hypotheses, not new primitive contracts.

The [wide-value consumer control](value-consumers.md) now compares full-byte
reduction with consumption of each value's final eight bytes. Abseil borrows its
stored value; independent full-byte validation remains outside timing. The final
local audit covers all 144 complete read roots across six K/W/kind cases, four
contenders and scalar/4/8-key callers. Every root is call-free. All 36 paired
composed/direct streams are identical after root-relative branch normalization,
including registers and immediates. Direct isolates bucket field transfers and
shares routing, overflow and bags; it is not an independently routed container.

Although the source spells full record assembly, selected consumers eliminate
unused payload transfers. The K40/W64 map explicitly loads one selected word;
the multimap loads one selected word per bag value. Admitted selector windows
can incidentally include neighboring payload bytes. This does not require a new
projection API in the measured exact byte-plane layouts, nor establish the same
result for non-identity packing, other callers or native x86 code.

The audit also exposed a composition cost above the primitives: two benchmark
forwarding callbacks and the multimap owner's record-to-value callback could
remain outlined for the full reduction. Their scoped forwarding bodies now
explicitly preserve inlining. Marking an enclosing method alone had not ensured
the callback body fused. Initial timings with those outlines are diagnostic;
the final comparison uses the corrected callers. Full/tail comparisons change
the workload and must not be subtracted to invent an isolated lookup cost.

## Baseline fusion correction

Capture `0058b1cc27a01347b4be85714e2d1b80e708335943609b2d189d492affd53903`
uses the pinned ordinary Abseil implementation directly, with its normal hash,
equality and the same counting allocator. A Clang statement `always_inline`
attribute applies to the actual `map.find` assignment; a wrapper attribute had
not been sufficient. No dependency implementation is modified.

Group `20261013T112238Z-db15fdec` completed, but **the first correction did not
fuse x86 dependent lookup**. In its final `assembly.gz`:

| Target / job | K16 dependent | K40 dependent | Independent callers |
| --- | --- | --- | --- |
| NEON `20261013T112245Z-2e4164ce` | Fused | Fused | Fused |
| Zen5 `20261013T112251Z-8e36a263` | Calls `find` at `0x35cf0` | Calls `find` at `0x5611f` | Fused |
| Granite `20261013T112258Z-670ec371` | Calls `find` at `0x358b0` | Calls `find` at `0x5528f` | Fused |

The pinned `raw_hash_set` const `find(key)` forwards to
`const_cast<raw_hash_set*>(this)->find(key)`. The call-site attribute fused the
forwarder, leaving its non-const implementation outlined. An adapter-only remedy
is to target that implementation directly through the same const-cast used by
Abseil, retaining the const result and ordinary read semantics. Its generated
code still needs verification; a source attribute is not proof of fusion.

The local NEON `build/hashtable-design/local-final.asm` also has no calls in
either Abseil consumer. This never established the x86 result. The corrected
panel adds density/filter cases, so it is not solely a baseline-change
experiment. K40 still uses the ordinary native `uint64_t` key, not an exact
five-byte key: its payload comparison retains native-key padding. These limits
remain attached to the timings.

## Direct-overload correction and superseded packet qualification

Capture `40360db7a83614546879bccfe4b86a20783915dbbae983e244d3099bcab50bd0`
targets the non-const `find` implementation explicitly. Group
`20261013T113014Z-af1ea25a` (`build/hashtable-design/native-packet/group.json`)
supplies these final assemblies:

| Target | Job | Receipt state |
| --- | --- | --- |
| V2 NEON | `20261013T113020Z-f99694c6` | Complete |
| Zen5 AVX2 | `20261013T113027Z-9c4b6d0c` | Spot-interrupted after assembly capture; timings partial |
| Granite AVX-512 | `20261013T113033Z-795f6b9f` | Complete |

Abseil K16/K40 point, cohort-dependent and independent consumers contain **zero
calls on all three targets**. This verifies the baseline fusion correction.
The interrupted Zen5 job still supplies valid built-code evidence, not a
complete timing panel.

Packet4/8 consumers are not fully fused. Each Abseil packet calls one outlined
`ht::lanes` templated-lambda body; the actual `find` bodies inline inside it.
Candidate stages also outline: the K16/A5 fuller packet8 consumer has five
lane-stage calls on both NEON and Granite, in addition to cold chain calls.
`common.h` marks the outer `lanes` function inline but not its expansion lambda.
The explicit two-level inline pattern already used by SeriesPack's
`detail::each` is the relevant shared-driver correction. Those packet timings
charge these calls and materialized stage state; they cannot select a batch
width or establish the cost of a fully fused packet consumer. This qualification
does not undo the verified point/cohort correction.

## Fused packet and residence receipts

The subsequent packet-fused capture
`6d73bce2708954a99bcb7e081cfb10086af8ec6f34cd08a4a44389fb84f3d3ff`
and residence capture
`434a0418ffe3873bc7427317b8d9707f6ceba91fbaa0e9d56a84b9af336488e3`
supersede the outlined packet qualification above. Their collected
`codegen.json` reports zero calls in all ten baseline consumers and zero
outlined packet stages in all 55/75 candidate consumers, respectively.

| Target | Packet-fused job | Residence job |
| --- | --- | --- |
| V2 NEON | `20261013T114613Z-4f3a6ca7` | `20261013T115518Z-27d9bee9` |
| Zen5 AVX2 | `20261013T114620Z-4806037f` | `20261013T115525Z-06af327b` |
| Granite AVX-512 | `20261013T114627Z-be8d8dfc` | `20261013T115531Z-20c1f185` |

These guards establish call fusion, not instruction quality, absence of spills,
cache traffic or a causal explanation of timing differences. K40 retains the
native-width ordinary Abseil baseline described above.

## Local staged Ring review

The local Linux ARM Release executable `hashtable_design_ring` was inspected
on 2026-10-13. Ignored diagnostic `build/hashtable-design/ring-local.asm` has
SHA-256 `47d618efd9dce17e945c1db023dc5e30e6a56fb2345b2978734bd3c340fee144`.
This is local generated-code evidence, not a native V2 or x86 timing receipt.

All 28 ready callbacks (seven geometries, four hint choices) have no ordinary
calls and no stack references. Each cold owner-to-StreamPack continuation has
a 16-byte frame and one `links::data` call. Thus resolution's frame does not
tax every primary/secondary/payload visit. The ready callback tail-transfers
to resolution without adding another Ring visit.

Ordinary Abseil `find` and its public prefetch fuse in the direct and prefetch
callers for C=1/8/32. C=1 is frame-free. C=8/32 have stack-resident lane state;
C=32's only two calls are initialization `memset` and final result reduction,
outside the lookup loop. This does not claim zero cohort bookkeeping cost.

The source is privately owned and immutable until drain: no mutation,
relocation, physical polling or external reentry occurs while staged pointers
are live. `Ring::spin` itself does not poll Orbital. Hints stay within the
complete bucket unit, owner-directory entry or initialized StreamPack link
reservation; single-line hint codes do not silently extend those windows.
The driver now checks emitted-window containment against an independent owner
inventory outside timing; sanitizers do not normally validate prefetch addresses.
It also admits only workloads with at least one present key per scheduled lane.
This panel establishes no retained MVCC read or mutation/growth behavior.

`check_ring_codegen.py assembly[.gz]` applies the call/stack checks to the final
demangled binary before timing. It requires all 28 ready callbacks and six
baseline arms, permits and reports only baseline initialization/reduction calls,
and rejects other ordinary calls or tail transfers to outlined Abseil lookup.
It passes this local assembly. Injected ARM/x86 callback calls and stack
accesses, an outlined baseline `find`, and absent symbols each fail the guard.
It does not prove window validity, lifetime, instruction quality or performance;
the permitted outer call sites still require source review. Native Ring
callbacks require this qualification on their own captured binaries.

## Local aggregation fusion correction

The first aggregation binary
`bab1d0c509ddb9e47b0e431fa319677bae9b625b7c5cf186d66fef59939a89fb`
outlined Abseil's private `find_or_prepare_insert` on every input row, despite
the public `try_emplace` call-site attribute. It is superseded for comparisons.
The corrected local ARM binary is
`1d890ce3f4207c6d55bfb8ac9814e118f76374614a1b167fe461cce54a0f678b`;
`build/hashtable-design/aggregate-local-final.asm` has SHA-256
`2b82e75a90bd4066f96c609e0f7316949db35c5ba0a927bd8d15a34f870388fd`.
Its reviewed aggregate, baseline, table, links, fields and CMake sources match
native capture `8cd931e84ad5445796fbf8d0fabfe7de0176cd2888e2e3f9682bfd90c94ee451`.

The aggregate CMake target applies `-mllvm -inline-threshold=1000` equally to
all arms. The baseline still uses ordinary public `try_emplace` and the pinned,
unchanged dependency; ineffective adapter/outer `flatten` attributes were
removed. All nine accumulation and six read consumers pass the named hot-call
guard. Abseil's search fuses; `PrepareInsertSmallNonSoo` and `PrepareInsertLarge`
remain genuine insertion calls. Candidate search and combination fuse too.
The larger budget also inlines chain traversal/resolution and brings static
format-initialization guards and their cold bodies into the caller. Do not
mistake call fusion for smaller code or a measured performance contribution.

`links::update` resolves the entry once before existing TuplePack get/put.
In the fuller accumulation caller, the chain update ends with one old-value
load, addition and store at `0x15100`–`0x15108`. In the two-search control's
subsequent `set`, the chain store at `0x17c28` has no old-value load; primary
and secondary `set` likewise only store. This removes the earlier discarded
resolution as well as preserving dead-load elimination. These addresses and
observations belong to the stated local binary; native receipts must repeat
the generated-code checks with their own compiler/ISA settings.

## Native aggregation qualification

The same capture's native assemblies confirm actual fused hot searches and
combination, beyond the named-call guard's nine accumulation/six read checks:

| Target | Job | Fuller / Abseil accumulation code bytes |
| --- | --- | --- |
| V2 NEON | `20261013T130015Z-7338bda5` | 1824 / 480 |
| Zen5 AVX2 | `20261013T130021Z-9d051c57` | 1696 / 416 |
| Granite AVX-512 | `20261013T130028Z-ef1323ec` | 1712 / 416 |

All three use Clang 21.1.8, explicit ISA bodies with both vectorizers disabled,
and the common aggregate-TU inlining threshold. Code bytes are the hot-shape
`accumulate<false>` symbol extent through the next symbol, including cold paths
and alignment padding; they are not executed instruction counts or cycle costs.
The larger candidate body includes inlined chain traversal/resolution and
static format-initialization guards. Its x86 hit updates use memory-add
instructions for all three tiers; no outlined combiner or second value
resolution remains in the update body.

Abseil retains `PrepareInsertSmallNonSoo` and `PrepareInsertLarge`. The latter
is reached on a new-key insertion, **not only on rare capacity growth**. Its
cost belongs in the complete ingestion comparison. Its ordinary search and
existing-value update are fused; the read caller's only remaining calls are
failed-oracle diagnostics. The repeated/skewed ingestion losses persist on
all three targets after this correction, so those losses cannot be dismissed
as the earlier outlined-baseline problem.

This is a known-final-cardinality scratch aggregation comparison. Construction
and baseline `reserve(n)` are charged to setup; candidate geometry is fixed
before ingestion. The reported read follows validation and a complete warmup
lookup pass. The 110,000-key panel owns about 2.13–2.23 MB of requested table
payload: its roughly 28 MB repeated input stream does not make the index
DRAM-resident. The small panel's approximately 35 KB candidate versus 70 KB
native-key Abseil may occupy different cache levels. Compare requested bytes
directly; Abseil's zero `owned_capacity` denotes unreported allocator backing,
not zero memory. Shared preparation is also separate from per-table metadata.
These receipts do not establish unknown-size growth, publication, churn or
multi-writer performance. The matched two-search candidate is an attribution
control; ordinary Abseil remains the performance baseline.

## Local two-worker caller review

Local binary `de8df84087a9afbd1f6ca09759867044296f98ef890ca5ab4fdc5ffdd7216954`
was reviewed through `build/hashtable-design/workers-local.asm` (SHA-256
`df8a424fd73f8026647bf5e548e1cc3162eef8173d44466785e1c35f6bb42cb0`).
Abseil's actual searches fuse inside `route_local`, `accumulate`,
`rebuild_merge` and both `read_result` variants. Remaining calls include
ordinary insertion preparation, outgoing-vector allocation/copy and failed
checks. Candidate per-entry search and combination also fuse.

The candidate reconstruction control outlines `table::each` once per whole
merge and `links::each` once per secondary bucket. The upsert callback fuses
inside those helpers; these are not per-key callback calls. A final-binary
guard must inspect their bodies as well as the seven rebuild roots, seven
accumulation roots, four routing roots, eleven read roots and six timed
`merge_from<ht::add>` bodies. Check-only merge instantiations are additional.
Do not apply the Ring callback's no-stack rule to these ordinary consumers.
This receipt preceded the `upsert_miss` split and the census correction below;
it does not qualify their changed binary automatically.

The barriers provide stable borrowed source/result windows; each owner keeps
allocation, mutation and destruction on its original lane. There is no
concurrent-reader publication claim. Lifecycle timing includes table-phase
barriers, with runtime creation and Pool drain separate. CPU indices alone
do not prove distinct physical cores, and the unpinned coordinator shares the
allowed CPU set. Native receipts need topology qualification. Sampled table
coexistence excludes routing buffers: compare their sum when evaluating the
memory cost of routing versus merging. Neither that sample nor the retained
table footprint is a transient allocation peak or RSS measurement.

In that initial source, the before/after `footprint()` census occurred between
the per-worker timer stop and the coordinator's completion barrier. Consequently
wall-clock intake/finish and lifecycle included that diagnostic work while the
per-worker work fields excluded it. Candidate census walks link blocks; baseline
census is constant-time. **Initial `native-workers` wall timings are superseded.**

Corrected capture
`a7069bf6134aaae26cf0519753cf795b0d1d99e0b897a51f4865660d4e24f1fe`
places each census in its own synchronized phase outside operation timers.
Worker selection now requires distinct `(physical_package_id, core_id)` pairs,
preventing two SMT siblings from being reported as separate worker cores.
The coordinator still shares the allowed CPU set. Local binary
`f1db05bf3fa4d76c53c42e5ae79042d378f8a57dc9a77692b399ccc786143a40`
and `workers-census-local.asm` (SHA-256
`ce07b2ac407a67e6c11ba47e1d550a0b76a5a5654aa9877cfc92e60b8b3d7b0c`)
pass the final guard: eight merge bodies including checks, eleven reads, four
routing consumers, seven accumulators and seven rebuild roots, with no outlined
hot search.

Corrected `native-workers-census` receipts are now qualified:

| Target | Job |
| --- | --- |
| V2 NEON | `20261013T132919Z-9d45a20c` |
| Zen5 AVX2 | `20261013T132925Z-5e74034c` |
| Granite AVX-512 | `20261013T132932Z-27f80b71` |

Their final assemblies retain fused Abseil routing/accumulation/rebuild searches
and both result-read variants, plus fused candidate upsert/merge combination
inside the traversal helpers. Remaining transfers are ordinary new-key
insertion, routing-buffer growth/copy, link operations, static-format guards
and diagnostics. Topology identifies CPU 0/1 as distinct cores in one socket
and NUMA node on every target. The guard does not establish equal register
pressure, code size or cache traffic.

The 110,000-key distinct local-sized merge's 32,973 chained keys and lifecycle
loss therefore survive matched fusion. Reserving union geometry on both owners
reduces chains to 38 and improves Zen5/V2 lifecycle, but sampled coexist payload
is about 4.458 MB versus Abseil's 3.342 MB; retained payload is near equal at
2.230/2.228 MB. Keep that memory tradeoff alongside the timing result.
Repeated/skewed complete lifecycle still loses despite encoded merge improving
on reconstruction. These findings do not select a general capacity policy.

`drain_ns` is specifically the coordinator's `join()` wait **after** the release
barrier. Owners can already be draining before that timer begins; it is not the
full physical Pool-drain duration. Runtime creation and this remaining join
wait are reported separately from the table lifecycle. Corrected operation
timers exclude the synchronized diagnostic censuses.

## Local bound-mutation boundary review

`build/hashtable-design/mutation-local.asm` (SHA-256 `a1caeb04d986b3ac728918a57f2ad1d7000901aefe653e2adb8b01b7de8b31be`)
is a local generic-ARM Release receipt, not a native V2/x86 result. Both ordinary
and bound consumers retain the same hash, physical plans and single-search
semantics; ordinary Abseil's actual search remains fused.

K40/A8 bound accumulation loads the input/bucket/selector parameters and primary
base once at `0x33c1c`–`0x33c28`. The common primary-hit loop
`0x33c80`–`0x33d40` contains no owner-context reload, stack access or call. The
ordinary loop reloads those facts at `0x319c0`, `0x319c4` and `0x319fc`. K40/A14
has the corresponding bound shape. Both variants still compute the same two
independent folded products, filter occupied lanes and compare the suffix.

The primary-miss block alone materializes the 24-byte permutation parts on the
stack, then calls `continue_upsert` at `0x33c64`. The continuation uses those parts
and does not repeat primary hashing or primary matching. It retains downstream
duplicate detection before filling a primary vacancy and updates counters only
after admission. Every new insertion and downstream update pays that call; this
is not a claim that outlining is free or that complete ingestion is faster.

The mutator borrows a stable scratch owner and copies only primary context.
Geometry/base relocation or reseeding ends that borrow; the caller must rebind.
Chain-only growth is allowed because the continuation reacquires its own
StreamPack windows, with no escaped slot pointer. The infallible combiner must
not reenter or mutate the owner. Current checks exercise fixed geometry,
all template key widths, chain growth and allocation refusal/retry; they do not
establish geometry-growth/rebind behavior or MVCC publication.

`mutation-report.py` checks equal requested bytes and tier totals for each paired
ordinary/bound repetition. Together with identical construction and insertion
order this supports the intended same-placement comparison; totals alone are
not a bytewise or per-key placement proof. The native checks below qualify this
capture's bound path separately from the local receipt.

## Native bound-mutation qualification

Reviewed the final `assembly.gz` from V2 NEON job
`20261013T134927Z-c0706ef6`, Zen5 AVX2 job `20261013T134934Z-64aabcbd`
and Granite AVX-512 job `20261013T134940Z-d46c1243`. Complete symbol
extracts and compressed-assembly hashes are retained under
`build/hashtable-design/mutation-review/`, including `native-provenance.json`.

For both K40/A8 and K40/A14, binding hoists the primary base and three hash
parameters out of the input loop. V2's bound primary-hit loop contains no stack
access, owner-context reload or call; the A8 example is `0x3ddb0`–`0x3de78`.
The ordinary V2 loop reloads context but has no corresponding hot spills.

The x86 bound loops are **not stack-free**. Zen5 and Granite store three immutable
seed snapshots once in the prologue, then read them through three stack operands
per hit. Their primary bases remain in registers. This replaces the ordinary
loop's five per-input stack stores, two value/accumulator reloads and four
owner-context loads. The bound hit path has no stack stores, and its incoming
value and distinct-count accumulator remain in registers. A8 examples are
Zen5 `0x43900`–`0x439b5` and Granite `0x420b0`–`0x42165`, plus each loop's
counter/backedge block. Both geometries retain the same two folded products,
occupied-prefix filter and suffix search; binding did not simplify the hash.

Only the primary-miss path packages the 24-byte permutation parts for the
outlined continuation. Its ABI transfers are real costs: x86 copies the
aggregate to the call stack; ARM passes a stack aggregate by address. On all
three targets the continuation consumes those parts, reads primary descendants,
and computes secondary hashing as needed. It neither rereads primary hash
parameters nor repeats primary hashing or primary matching. Downstream duplicate
detection still precedes filling a primary vacancy. Every new-key insertion or
downstream update pays the continuation call.

All three ordinary Abseil accumulation bodies retain fused existing-key search
and combination. Their remaining calls are `PrepareInsertSmallNonSoo` and
`PrepareInsertLarge`, belonging to new-key insertion. This assembly review
qualifies the intended boundary experiment; it does not attribute measured
runtime changes to individual instructions or establish growth/rebind or MVCC
behavior. The high-bit/all-domain and dense-deletion checks added after these
captures are separate Release/ASan evidence, not checks executed by these native
binaries.

## Local geometry-transition review

Reviewed `build/hashtable-design/transition-local.asm` (SHA-256
`04757cd791c818cba51334dcba74eaadfa9f3c969d563966c8efc6bf42b30b3c`).
This is local AArch64 Release evidence, not native V2 or x86 qualification.
Selected complete bodies and the guard receipt are under
`build/hashtable-design/transition-review/`.

The guard finds 23 migration roots, 21 reencoded merges, two same-layout merges,
three accumulation bodies, one Abseil consuming merge and five final reads.
Manual inspection includes its 46 specialized table/link `each` bodies: the
callbacks contain destination hashing, search/admission and value handling,
with no per-entry callback or search call boundary. Migration calls the table
traversal once; table traversal can call a link traversal per secondary bucket,
including empty chains. Link append, format initialization and diagnostics
remain separate. These calls must not be described as one call for the entire
map in every case.

Known-absent migration genuinely omits duplicate matching. For A7 to A8,
`0x8fc0c` reconstructs source keys and directly admits through the destination
permutation; it contains no fingerprint-match loop. The ordinary counterpart
at `0x89b6c` retains fused matching. Different geometry and fresh seeds require
source inversion followed by destination hashing; this is not a radix split
or encoded-byte relocation. No duplicate primary hash/search was found.

There is an avoidable secondary recomputation in ordinary admission: after a
downstream miss with a full primary bucket, the secondary products used by
lookup at `0x89de4`/`0x89dec` are computed again for append at
`0x8a0b0`/`0x8a0f8`. Reencoded merge has the corresponding shape at
`0x86990`/`0x86998` and `0x86d34`/`0x86d80`. Thus the known-absent arm measures
a complete admission specialization, including removal of that recomputation;
its advantage cannot be attributed solely to skipping duplicate comparisons.

Abseil consuming merge at `0x85150` fuses iteration, search and addition;
remaining calls prepare new-key insertion. Final reads retain fused searches.
Explicit reserve is inside its recorded timer and follows the pinned Abseil
resize implementation, whose ordinary per-entry hash-slot and transfer policy
calls remain indirect (`0xac210` and `0xac1e4`). They are not wrapper-induced
lookup outlines. Natural growth and explicit reserve both charge their actual
rehash work.

The guard is a named-call check: it records nested traversal bodies but does not
assert their count or reject every indirect/unknown callback. It also does not
cover reserve. Manual inspection supplies those qualifications for this binary;
native captures require their own checks. This review does not establish equal
memory traffic, an optimal growth policy or an isolated search-cost attribution.

## Native geometry-transition qualification

The final transition binaries were reviewed for V2 NEON
`20261013T141419Z-e8c80bfd`, Zen5 AVX2 `20261013T141426Z-98e60ba7` and
Granite AVX-512 `20261013T141433Z-f5191f23`. All share worker source digest
`dc84c981fcec782cd8fc2d52297fbd06af0d5f5c2678117f1d1daf0285288a04`
and source archive SHA-256
`ebf91fdeb2e17754ae6389b12ea83ff8432e9964ae6331642655697d10121788`.
Complete selected symbols, captured source files, guard receipts and assembly
hashes are retained under `build/hashtable-design/transition-native-review/`.
This qualification does not cover subsequent live `ValueBytes`/set changes.

Source construction is real and fused. Both A7 and A13 candidate accumulation
bodies retain hashing, occupied-prefix lookup and primary/secondary admission
inside the input loop; primary insertion writes fingerprint, suffix, value and
count without a call. StreamPack append remains an ordinary overflow call.
Abseil accumulation also retains its complete lookup and combination in the
loop, with the normal `PrepareInsertSmallNonSoo`/`PrepareInsertLarge` calls on
new-key insertion. Representative hot roots are V2 `0x8bca0`/`0x8ac00`,
Zen5 `0x919b0`/`0x90870` and Granite `0x90920`/`0x8f800`
(candidate/Abseil). Both owners' builds remain between their build timer calls.
No omitted build or wrapper-induced baseline search boundary was found.

These build traces are unique within each source, so they measure insertion;
final successful reads measure a different path. Candidate inline append and
Abseil's insertion preparation are actual instruction-level differences, not
an attribution of the measured gap. The reported lifecycle excludes the
separately measured final read. Consequently a lifecycle win and read loss are
not contradictory, nor does the build result establish repeated-update parity.

All three captures retain the expected roots and 46 specialized nested `each`
bodies, without per-entry callback/search outlines or indirect callbacks in
those consumers. Known-absent table and link traversals contain no fingerprint
match instructions; ordinary migration and reencoded merge contain fused
matching. Source inversion and destination hashing remain charged for changed
geometry/seeds. The redundant secondary hash after a downstream miss remains:
for example, Zen5's ordinary A7-to-A8 migration computes lookup products at
`0x954f6`/`0x9550c` and admission products again at `0x95857`/`0x95882`.
The local review's complete-admission, rather than isolated-search, attribution
therefore also applies natively.

Abseil consuming merge and all five final-read bodies have fused searches.
Explicit reserve remains inside the prepare timer, invoking the normal pinned
Abseil resize implementation with per-entry hash/transfer policy indirection.
Natural growth is charged within merge. The guard does not inspect reserve or
prove arbitrary callback fusion; the manual native inspection supplies those
qualifications for these binaries. This is code-generation evidence, not a
model of operation time, peak RSS or a general growth-policy selection.

## Local set-consumer qualification

Reviewed `build/hashtable-design/set-local.asm` (SHA-256
`f314466ce22fa0d9e62b349dedaaf5f5725d1252b219c9c96d5128fcc5dcca70`),
the corresponding `set.cpp`/`set_baseline.h` and zero-value table/link paths.
This is local AArch64 Release evidence only. Complete consumer extracts and
their transfer inventory are in `build/hashtable-design/set-review/`.

All 48 expected roots are present: twelve each for fill, independent membership,
serial membership and turnover. FlatHashSet's actual searches are fused in all
four families. Fill retains normal new-key insertion preparation; turnover
retains `EraseMetaOnly` and insertion-preparation calls. These are ordinary
container operations, not extra wrapper lookups. Candidate matching remains
explicit SIMD, and its remaining helpers perform StreamPack append/erase,
format initialization or diagnostics. The target disables loop and SLP
autovectorization.

Serial membership has a real Boolean-to-next-address dependency. For example,
Abseil K16 selects the successor address at `0x3f8c4`, loads it at `0x3f8d4`,
then forms the next record address at `0x3f8dc`; the fuller K16 candidate has
the corresponding sequence at `0x46ffc`, `0x4700c`, `0x47014`. The independent
input construction gives one cycle through every present and absent query;
each complete traversal has exactly 50% misses. The incorrect-answer edge is
distinct, and untimed full membership/oracle checks precede the timed walk.
Neither lookup nor the successor dependency is elided.

The common source is a **24-byte record**, not a 24-byte load each step.
Abseil K16 loads a two-byte key while the candidate loads a word; both load the
selected eight-byte successor. The graph occupies `48*n` bytes, independently
of the set. Its dependent loads, address arithmetic and cache residence are part
of this workload, so the timing is not isolated set latency or proof of a named
cache residence for the set alone.

`ValueBytes=0` removes payload storage from both bucket recipes and StreamPack
links. Link entry strides are two bytes for K16 and five for K40; inspected
append bodies store those key bytes and link/count metadata, with no value
store. The K40 example writes four key bytes at `0x2d3f0` and the fifth at
`0x2d3f4`; there is no retained eight-byte map payload. These observations
qualify the intended set comparison, not any native performance conclusion.
