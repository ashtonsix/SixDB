# Boundlist and StreamPack: retained design

Boundlist owns ordered subdivision geometry and explicit optional rank selection. StreamPack adds
byte values, optional used lengths, value validation, packing and selected
transfer. The owner supplies allocation, coherent versions, read protection,
publication, sorting policy and rank repair. This record supersedes the proposed
portfolio at `b6e4ebd5`; [qualification](PORTFOLIO.md) owns measurements and recovery.
The [API cleanup](API.md) unifies borrowed and compact geometry and removes the
paired-extrema helper; this record retains the encoding/composition decisions.
The public [reference](https://github.com/ashtonsix/SixDB/blob/719b0f5709c9190bd592888cf8fbbbd379bce960/ikea/docs/boundlist/reference.md) and
[usage](https://github.com/ashtonsix/SixDB/blob/719b0f5709c9190bd592888cf8fbbbd379bce960/ikea/docs/boundlist/usage.md) explain the resulting interface.

## Curated choices

| Choice | Why retained | Cost / limit |
| --- | --- | --- |
| Absolute Ends | Short rank path; direct endpoint comparison; especially compact when the whole extent fits a narrow carrier. | Full endpoint directory up front; resizing translates the suffix. |
| Sizes + G16 checkpoints | Narrow per-rank sizes, cheap resizing and size-only work. Remains the default. | Random starts need a prefix; inverse lookup pays prefix construction. |
| Relative Ends + G16 checkpoints | Direct local lookup and native endpoint comparisons without global-width fields. | Local ends can widen beyond sizes; resizing updates a group suffix. |
| Optional U in StreamPack | Values can occupy less than their reservation. Omitting it removes bytes and work when U=R. | Independent carrier, validation, effects and retention when present. |
| Grouped R/U | Complete-value and native packet locality; length storage grows by assigned groups. | U-only work walks geometry too. |
| Geometry-first planar R/U | Contiguous U for established U-only consumers; simple geometry addressing. | Full geometry prefix even at low occupancy. |
| Fixed size, zero geometry, immortal | Omit facts the schema proves redundant. | Operations cannot introduce an omitted fact. |

This is a portfolio, not a claim of universal Relative Ends superiority. Relative
Ends is particularly useful when its local carrier equals the size carrier and
both are narrower than global coordinates. Sizes remains valuable when local
ends widen. Absolute Ends remains valuable for short point paths and tight extent
bounds. No new default or automatic workload classifier is introduced.

The curated capacity is **256**, group width **16**. Generic ordered sources do
not inherit that capacity: counts and domains are caller-defined, including wider
integers and ordered nonnumeric keys. Engine's small nodes can borrow native or
packed endpoints without a 256-slot header. Unique-key routing uses one boundary
search; duplicate handling adds no mandatory second search.

## Schema bounds and stored bytes

M is maximum slot size; B is an independent extent bound. New carriers are the
smallest native u8/u16/u32 proving:

- size: min(M,B);
- global end/checkpoint: B;
- relative end: min(16M,B).

U follows its own value bound, including a tighter fixed reservation. It does not
inherit a wider Relative Ends carrier. Bounds stay fixed as a block grows; a
smaller allocation is not permission to narrow a persistent schema.

Full geometry-only bytes include optional D (present here) and controls:

| M | B | Absolute Ends | Sizes | Relative Ends |
| ---: | ---: | ---: | ---: | ---: |
| 255 | 65280 | 546 | 326 | 582 |
| 4095 | 1048320 | 1058 | 614 | 614 |
| 4096 | 1048576 | 1058 | 614 | 1126 |
| 65535 | 65535 | 546 | 582 | 582 |

There are ceil(assigned/16) meaningful origin checkpoints, no terminal
checkpoint. The directory reserves all 16. Variable checkpointed control stores
count and extent. Local fields grow in initialized G16 groups; inactive tail
lanes are zero and readable but are not assigned ranks. Relative fields reset
at each group and are not globally ordered.

Boundlist has directory, lengths and control windows. StreamPack adds payload;
its source-qualified effect planes are payload=0, directory=1, lengths=2,
control=3. Boundlist's planes start at directory=0. Windows can share an owner
allocation but must be disjoint. No allocation wrapper or persistent native
prepared object is introduced.

### One planar order, natural alignment

Grouped storage places 16 geometry fields then 16 U fields per group. Planar
storage reserves the full geometry prefix, then assigned U groups. Without U,
or without geometry in lengths, placement is normalized away. New alignment is
the widest actual local carrier; 48 B/96 B group strides do not require those
alignments. D remains before the geometry directory; no interleaving/padding menu
was retained.

A U-first plane was implemented and measured. It reduced partial initialized
prefixes, but required a geometry-base field and added address work to common
point and write paths. Narrowing prepared strides also prevented useful folded
addressing. We restored native word strides and chose geometry-first, eliminating
the extra geometry base altogether. Final prepared layouts are 28 B Boundlist /
40 B StreamPack, typed accesses 16 B. These are native ABI observations, not wire
sizes. We did not retain both plane orders as another schema choice.

The cost is explicit. For M255/B65280 with U, a 17-rank Relative Ends block uses
166 B metadata grouped versus 614 B planar (including D/control); at 256 ranks
both use 838 B. A literal independently authored fixture checks the planar U
carrier and 17-rank byte extent. It caught an initial length formula that counted
the R carrier for U; that candidate's jobs were cancelled and replaced.

### Descriptors and names

New standalone Boundlist version 4 and StreamPack version 3 distinguish these
carrier laws and Relative Ends. Legacy descriptors retain their original bytes,
carriers, placement and alignment, and re-encode their original version. New
noncanonical flags are rejected. Conversion builds new contents, never retags an
old representation. Literal fixtures and full old/new conversion histories are
part of qualification.

Maintained names are Absolute Ends (`absolute_ends`), Sizes (`sizes`) and
Relative Ends (`relative_ends`). `direct` and `width` remain source-compatible
aliases of values 0/1, not additional formats. Generic checkpointed sizes are
`delta_source`; `relative_end_source` accepts checkpoints and local ends.
Boundlist uses `fixed_size`, `location{offset,size}`, `slot_size`, native `sizes`
and `position_owner`; StreamPack retains its byte-value vocabulary.

## Shared bodies and mutation laws

The common seam is geometry, not StreamPack's value packet. It consists of:

1. An original-geometry walker that captures original position/size before a sink
   can overwrite checkpoints or local ends. Retired and zero-size ranks remain
   part of the physical stream.
2. One carried sequential encoder that writes chosen sizes/local ends/origins,
   initializes readable groups and finishes controls once. It chooses no sorting,
   movement, retirement or rank policy. Redundant one-off author helpers were
   removed.
3. Native G16 decoding into a fused consumer. Relative Ends subtracts adjacent
   local ends rather than reconstructing a prefix. Start+U does not decode R
   unless the consumer needs it.

StreamPack's temporary value encoder composes geometry and value writes without
self-referential effect state; ordinary
append and selected transfer share it. It owns no resource, cannot escape/copy/
move, and is only used after whole-run admission. Transfer retains bounded 2 KiB
coordinate scratch to avoid codec-pair template multiplication. Packing fuses the
original walker, movement, U policy and encoder. It stays entirely in StreamPack.

Two numeric operations are deliberately distinct:

| Operation | Absolute Ends | Sizes | Relative Ends |
| --- | --- | --- | --- |
| Move E[r], preserve later endpoints | Store E[r]. | Change R[r] and R[r+1]; adjust next checkpoint at a group edge. | Store local end; at a group edge move the next origin and adjust that next group's locals. |
| Resize R[r], translate suffix | Update E[r..]. | Change R[r] and later checkpoints. | Update this group's local suffix and later checkpoints. |

Both preflight bounds, ordering and effects; refusal preserves all bytes/effects.
Explicit SIMD updates describe the actual issued footprint, including preserved
lanes. They move no payload/children. StreamPack does not expose these as
uncoordinated value edits. Middle insertion, sorting, merging and deduplication
are owner-selected rebuilds with explicit rank repair.

## Search, packets and scheduling

Ordinary search keeps conditional-select narrowing and explicit SIMD convergence.
Opt-in scans now include byte carriers; u16 Sizes inverse uses a full in-register
prefix with narrowing only when a proven span permits it. AVX-512 carries byte
masks across all 64 lanes. NEON retains vector carries. Exact unaligned tails do
not overread. Lower/upper results, duplicated endpoints, zero groups, eligibility
holes and nonzero/wide origins share the same semantics.

Reusable numeric bindings keep the existing context-sensitive prediction/repair
strategy for larger sources. Lerp/shotgun internals remain implementation choices,
not public algorithm flags. Arbitrary ordered domains keep comparisons. The
portfolio does not force arithmetic distance onto every source.

Prepared phases expose one real unresolved local read, with compact pending
state; ready work remains inline. Eligibility selection borrows the original
named source rather than a temporary typed dispatch. A local-window hint is not
a complete read grant: prediction repair and population selection may read
elsewhere. One retained coherent version covers boundaries, count, eligibility,
backing and any derived search model through completion.

Native packets and scoped CPS chains stay inline at the useful grain. Complete
packet consumers and checked transfer are measured beside ordinary reads and
writes. Cold tests separate C1 latency, matched-concurrency direct/manual/Ring
throughput, hints, frame/scheduler bytes and setup/drain. Ring does not make a
single dependent lookup low-latency, and its scheduler overhead is reported.

## Allocation, protection and versions

Effects name the original source and actual writes. They are neither read
protection nor beforeimages. Owners admit exclusive authority over every issued
byte and can coarsen effects or erase them for authorized scratch work. Hooks
cannot fail, allocate, suspend or invalidate admitted inputs during stores.
Layouts, named views and borrowed phase/effect sources stay at stable addresses.

The real Loom/Orbital fixture exercises both independent old/fresh copies and
append-shared payload prefixes with private metadata. A copied Allocation handle
is not reader retention: each newly exposed prefix has an actual range Hold,
shared through the source backing. Previously published payload bytes and padding
are not reissued. Holds permit in-reservation growth and refuse relocation while
old captures remain. Relinquishing writer tail authority plus dropping a later
reader allows physical tail reclamation while an early reader remains valid.
This fixture validates lifetime/publication, not a universal owner allocation
layout or owner throughput. Count its Parts, holders, metadata and retained tails
separately from packed bytes.

## Evidence and limits

[Portfolio qualification](PORTFOLIO.md) records absolute times, bytes, losing
cells, source captures and exact recovery. Local release plus ASan/UBSan tests
cover independent mutation laws, no-effect refusal, literal bytes, conversion,
aliases, fixed/full/immortal schemas, native packets and source lifetime. Actual
material histories exercise effects, copy and sharing, remaps and retained
readers. Native runs disable loop/SLP vectorizers on AVX2, AVX-512 and NEON.

Earlier U-first/36 B and U-first/44 B campaigns are diagnostic controls, not the
final format. The cancelled first geometry-first run is not evidence. Large
allocated populations do not establish DRAM residence: active lines, cache
conditioning and available counters determine what can be claimed. The final
report labels unqualified large cases memory-sensitive rather than pretending
nominal RSS proves a cache level.

No universal search index, heat field, compaction timer, sort order, string prefix,
checkpoint/retirement interleaving, allocator facade or second planar ordering
was promoted. Extra Absolute Ends checkpoints remain a caller-derived source
composition for a coherent version, not mandatory stored state.
