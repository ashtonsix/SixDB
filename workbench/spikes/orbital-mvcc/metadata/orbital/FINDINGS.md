# Orbital metadata: frozen implementation and alternatives

This is investigation, not an API. Baseline is commit `7f73c81`, measured with
Clang 21.1.8/C++23 on the Linux AArch64 guest. `census.cpp` reports actual sizes,
alignment and triviality; `abi.cpp` inspects ordinary external return boundaries.
`backing.cpp` measures requested metadata through the actual frozen Store.
Build outputs stay in `build/orbital-mvcc-metadata/`.

Repeat with `orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/orbital/run.py`.
The shared experiment helper captures current candidate sources while baseline
sources come from the named Git cut. Its receipt, source archive, JSONL and ABI
IR stay in the ignored run directory. These probes count metadata and check
fixture words; the owning [performance panel](../../performance/README.md)
measures actual Ikea operations against LMDB.

## The footprint is larger than Version

| Private record | Bytes | Fields and actual responsibility | Frequency and lifetime |
| --- | ---: | --- | --- |
| Base | 32 | Domain pointer, reference count, retirement link, destructor function | Repeated on every independently allocated metadata record; ownership transitions, never needed per decoded value |
| Domain | 80 | PMR virtual resource, Lane, count/budget/peak, retirement queue | One Store family; survives exported closures |
| Info | 200 | Base; 88-byte description; two 32-byte PMR vector headers; external dependency owner | One Representation; copies logical/format metadata, owns interpretation dependencies |
| Part | 200 | Base; 80-byte Leaf; Info reference; PMR metadata vector; shared external owner; 32-byte Allocation | One Part; independent retention, both backing variants occupy space even when one is inactive |
| Rep | 160 | Base; Info reference; three PMR vector headers; 24-byte Access | One Representation; owns Parts, duplicate pointer arrays and optional PartId lookup |
| Leaf | 80 | Description pointer, 16-byte PartId, body and interpretation spans, padding/alignment, Part owner pointer | Embedded in Part; hot resolved access, but most fields are qualification/discovery metadata |
| Access | 24 | Leaf-pointer span and Rep pointer | Embedded in Rep; descriptor directory/owner route |
| Version | 88 | Base32; ObjectId16; cut8; two Rep references16; older8; flags/padding8 | One changed Object per publication; duplicates identity and common ownership/retirement machinery |
| Entry | 56 | ObjectId16, head, debt links, flag/padding, prepared-user count | Stable catalog node, plus unordered-map node/key/buckets outside this sizeof |
| Catalog | 224 | Base; PMR hash index; maintenance cursors; snapshot list; pending owner capability; counters | One Store; cold construction/maintenance, current selection reaches head here |
| Snapshot | 96 | Base; Catalog reference; alternative-capture vector; links; cut | One retained discovery enrollment; copies of the public context share it |
| Edit | 80 | Base; Snapshot reference; change vector; consumed flag | One private Draft; payload includes Version records and vector capacity |
| Change | 24 | ObjectId and Version reference | Per changed Object, vector capacity charged separately |
| Acceptance | 96 | Base; Catalog reference; pending vector; basis/occurrence; disposition | One accepted finished result; no compulsory command log |
| Pending | 16 | Entry pointer and Version reference | Per prepared Object, pins destination through install/abort |
| Build | 104 | Base; Rep reference; PMR bit-vector header48; writer count/flags | Private construction; actual writer bits and complete Representation are additional |

These are fixed record sizes, not total costs or malloc/RSS estimates. The
88-byte Version is not a universal lower bound: Base32 and ObjectId16 alone are
over half of it, and catalog-owned history need not independently own either.
The reverse link already removed is only a small part of the problem.

Actual complete closures make the distinction visible. Each test Part owns eight
external body bytes and eight copied interpretation bytes; shared logical and
physical interpretation are eight bytes each:

| Parts | Store setup | Captured closure + setup | Completed catalog + closure | External bodies |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 304 | 680 | 952 | 0 |
| 1 | 304 | 904 | 1,176 | 8 |
| 4 | 304 | 1,576 | 1,848 | 32 |
| 17 | 304 | 4,624 | 4,896 | 136 |

The one-Part closure alone adds 600 requested bytes. The four-Part closure adds
1,272. These include the real records and vector allocations, not caller fixture
vectors, external shared-pointer control blocks, heap overhead or backing bytes.
`backing.cpp` also exposes private edit/staged/accepted peaks. A small public
handle does not remove this backing state.

## Public packets and ordinary return ABI

All listed packet alignment is eight except enum/Result<void> alignment four.
Ownership-bearing wrappers are nontrivially destructible even when pointer-sized.

| Packet | Bytes | Use / lifetime | Observed AArch64 return |
| --- | ---: | --- | --- |
| BorrowedContext | 16 | Store pointer + completed cut, uninterrupted selection | Two words |
| Selection / Result<Selection> | 16 / 24 | Version pointer + fixed descriptor; contextual borrow | Result uses hidden result pointer |
| View / Result<View> | 8 / 16 | Leaf pointer; scoped native access | Result uses two words |
| Bytes | 16 | Resolved pointer/length borrow | Two words |
| RetainedView | 8 | Owns Part, not merely its address | Hidden result pointer despite sizeof8 |
| RetainedSelection | 16 | Owns Version and fixed Representation | Hidden result pointer |
| ReadContext / Result<ReadContext> | 8 / 16 | Owns 96-byte enrollment plus discovery protection | Result uses hidden result pointer |
| Representation / Builder / Draft / Prepared / Store | 8 each | Cold owning handles with much larger backing state | Their 16-byte owning expected results use hidden result pointers |
| WriteView / Result<WriteView> | 16 / 24 | Owns Build + Part index, scoped private access | Nontrivial result uses hidden result pointer |
| Result<MutableBytes> | 24 | Validated writable span; not lifetime ownership | Hidden result pointer |
| Description | 88 | Borrowed identity, replica, format, metadata spans | Large aggregate; cold inspection |
| RepresentationSpec | 104 | Description inputs + dependency owner | Construction input, not per-point result |
| PartSource / PartPlan | 80 / 72 | Capture closure / allocation and metadata plans | Construction inputs, not native return packets |
| Statistics | 96 | Budget, lifetime and disposition observations | Hidden result pointer; diagnostic path |
| Result<void> | 8 | Explicit failure status | One word |

ABI facts come from emitted LLVM IR, not an assumed <=16-byte rule. `std::expected`
adds a discriminator; nontrivial destruction can force indirect return even when
size fits registers. Native qualification should yield a compact reusable borrow;
construction and explicit retention are different frequency classes.

`packet_candidates.cpp` demonstrates two actual alternatives. Removing a raw
Version pointer from a selected physical binding permits an 8-byte descriptor
borrow and a 16-byte status result with ordinary two-word return. Logical identity
and eligibility then need to live in the real immutable descriptor/catalog, not
disappear. The Version candidates exercise that relocation.

The same probe contrasts ordinary owner8 (indirect) with Clang `trivial_abi`
owner8 and owning status16 (register returns), checking success/failure, move and
by-value destruction counts. This is a toolchain-specific ABI option, not a
memory reduction: backing ownership still exists. It changes where parameter
destruction occurs and needs real external-call lifetime tests before use. It is
not a proposal to annotate all Ref types or rewrite every API to out parameters.

## Actual first candidate, including capacity

The [descriptor](descriptor/README.md) has two allocations: a frozen description
and Part directory, plus the complete backing-owner directory. Its one/four-Part
closures are 224/464 requested bytes, versus 600/1,272. `Selection` and `View`
are ordinary eight-byte borrows. Rich inspection remains cold; native callers
borrow a noncopyable description header or return spans. The
[history adapter](candidates/candidate_store.h) removes per-Version ownership
headers and uses a nonmovable 32-byte caller-resident Scope. Actual AArch64 IR
returns bind8, alternative result16, borrow16 and status8 in registers. Cold
owning Prepared results still use a hidden result pointer.

`compact_backing.cpp` measures the complete requested state through publication,
late discovery, old-reader retention and release. This captured control is
`20261009T203757409672Z`, source digest
`e38b92c50c12960436a4247d6575f54f24f2e01a17cd27e1c7de2e3d5705ce44`:

| Four-Part fixture | Baseline88 | Direct32 | Indexed16 | Chunked8 |
| --- | ---: | ---: | ---: | ---: |
| One Object, initial publication | 1,848 | 3,032 | 2,120 | 1,120 |
| 64 Objects, initial publication | 93,480 | 37,208 | 36,800 | 36,304 |
| Eight dense publications, old reader held | 702,856 | 259,528 | 255,536 | 251,512 |
| Old reader released and drain completed | 93,480 | 51,656 | 47,664 | 39,944 |

These include Core, Object map nodes/buckets, descriptors, history allocations,
directories and retained capacity. The slab candidates lose the singleton case:
the first 64-node page is charged in full. Indexed16 additionally needs an
eight-byte primary-descriptor slot, before capacity slack. Chunked8 shares its
cut in a 16-byte header; a surviving node pins the publication chunk. Erasing
history does not refund retained slab or directory capacity.

Candidate caller state includes Memory40, Scope32 (optional fixture storage40),
and the input Change array: 24 bytes singleton or 1,536 for width64. Baseline's
Scope handle8 owns an allocated 96-byte enrollment already in its totals; its
input changes live inside its charged Draft. Source arrays, external body/control
blocks and malloc/RSS overhead remain outside both totals. The runner emits these
exclusions, copied bytes, peaks and allocation calls explicitly; baseline calls
are unknown, not zero. Drain work units differ: baseline includes queued record
destruction, while the candidate counts history examination/removal. They are
logical counters, not a CPU comparison.

The separate `escape_backing.cpp` keeps one Part after its siblings and Store
release. Candidate metadata is 232/424/1,256 bytes for one/four/17 original Parts,
plus its caller resource40. Baseline is 504 in all three cases, including its
surviving Domain. Each keeps only eight external body bytes. Export40 releases
sibling bodies but retains the whole candidate description directory: it is a
loss for the 17-Part escape case. Weak owner witnesses check actual destruction,
not merely a modeled charge.

## Shared consumer proof and compact Object directory

[Shared shape](shape/README.md) is a consumer-specific physical profile, not a
general Orbital schema contract. It admits actual Tuple/SeriesPack descriptions
once, shares invariant geometry and read controls, and keeps count, addresses
and genuine ownership with each immutable realization. Orbital's catalog issues
the exact Object/cut association. Its cold export cannot relabel an old borrow.

`shape_backing.cpp` constructs actual encoded bodies, ends Schema/source Shape
before resident observations, and checks old/current data through eight dense
publications. Both writer-basis treatments pass: retained through install, or
closed after accepted preparation and before install. They have identical
resident costs in this separate-node control. This does not establish the same
result for an inline-current alternative.

Captured native `shape-accounting/20261009T211153393575Z` and fully instrumented
ASan/UBSan/LSan `20261009T211446366566Z` pass the complete matrix, including
independent/grouped shared owners and real Allocation owners. Probe source is
`9018978654058821bf3e06d3595ad7c75b90bb069e47f8bf4e5ba96d3798a563`;
source digests are `1144d20dc33f787c259261ee104507af43f3ace430cab9cfcb744b0491d08a1c`
and `b114c0db6332d463c3e2bcea33494ea251a7bf779b1a59673e22523eabc03cbc`.
The receipts, source archives and full JSONL remain ignored. No timings are
claimed. All rows below use 64 Objects, three records each and compact directory
with 16-byte separate history nodes:

| Initial owner profile | Object/history/realization + shared proof | External shared control blocks | Charged Pool metadata | Logical body bytes |
| --- | ---: | ---: | ---: | ---: |
| One Tuple Part, shared | 7,395 | 3,584 | — | 4,416 |
| Four independent Series Parts, shared | 11,960 | 14,336 | — | 11,520 |
| Four grouped Series Parts, shared | 8,888 | 3,584 | — | 11,520 |
| One Tuple Part, Allocation | 6,883 | — | 30,616 | 4,416 |
| Four independent Series Parts, Allocation | 9,912 | — | 34,392 | 11,520 |
| Four grouped Series Parts, Allocation | 8,376 | — | 34,392 | 11,520 |

The first column includes 4,096 B catalog state: Core480, Object directory2,064,
history capacity1,040 and primary-pointer registry512. The shared proof is227 B
for Tuple or184 B for columns. These are actual requested allocations and
reserved capacities, not a per-record sum that omits slack. Shared control-block
sizes describe this fixture's PMR-backed deleters, not a universal `shared_ptr`
cost. Memory40, Scope32, optional Scope storage40, transient input Change
capacity, malloc overhead and observation records are separate caller costs.

Pool counters omit fixed Arena/Pool heap and thread ownership. The separate
[actual allocation census](allocation-profiles.md) measures752 B setup plus
the retained Group pointer vector, Lane256 and OS stack/guard VA separately.
New-Pool state and the marginal cost in an existing Pool are different questions;
the setup and Group state do not disappear because they are outside the
catalog's PMR resource. A fresh four-Part collection still misses the4–8 KiB
target even before this external allocator state is added.

An old cut preserves exactly old/current bodies. Eight dense publications of
independent columns nevertheless grow the real Pool to three Groups:
103,176 B charged metadata and196,608 B mapped payload remain after release,
including idle inventory. Selected-copy escape then leaves168 B catalog/codec
metadata and one actual Storage. Its held extent is20 B independently or224 B
with grouping. Grouped ownership retains sibling bytes in the same allocation.
Tuple escape is307 B metadata and69 B held body. The complete source checks
escaped contents/binding after Store/proof teardown and then releases everything.
The allocation census separately exercises idle trimming; high-water control
state survives until Group retirement.

## What remains worth changing

The compact directory removes duplicate Object keys and hash-node overhead, but
every current head still pays history and primary-registry capacity. Inline
current binding is the next comparison. Independent four-Part ownership must
remain a first-class case; grouping has different escape/retention behavior.
Allocation metadata is also reopened: eager trim inventory, separately rounded
control mappings and repeated hint padding are concrete opportunities. Their
experimental changes must preserve physical pins, foreign returns, preparation
and late completion. Whole-operation caller measurements decide which seams
are useful; the census alone does not establish a performance win.

Oldest-floor retention still keeps every intermediate version newer than the
oldest reader, even when only two cuts are selectable. The separate embedded
live-cut comparison retains 128 rather than 512 versions in the same 64-Object,
four-Part, eight-publication fixture. Requested held state falls from 255,536 to
70,120 bytes; external bodies from 16,384 to 4,096. Initial heads remain 36,800
in both, so this does not solve the initial 4–8 KiB collection target.
The helper allocates cut scratch from the same bounded resource: eight bytes
here, but eight times all enrolled Scopes before deduplication. Full-budget
refusal preserves debt rather than guaranteeing progress. `sweep_cpu_ns_*`
includes enumeration, scratch and destruction plus clock cost; it is one guest
observation, not an adequately sampled algorithm or caller comparison. Narrow
history-visit counters exclude those costs. This is not a Journal or distributed
retention policy.

The bounded candidate resource must outlive scopes, descriptors and exports.
Foreign-resource descriptors are not charged to the destination Store. Cold
conversion authority must preserve Store incarnation and exact source binding;
numeric Object/cut equality is insufficient. Physical-source retention need
not promise obsolete history stays eligible for adoption. Native/sanitizer
histories falsify these choices; no local census establishes a performance win.
