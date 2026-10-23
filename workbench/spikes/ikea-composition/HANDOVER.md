# Ikea cohesion: handover account

Ashton asked for a useful stopping point and broad coverage of outstanding
problems, without prescribing their solutions. This account separates observed
failures, evidence gaps and consequential contract questions. It is not a new
implementation plan. The three investigations already in flight are now closed:
direct TuplePack binding, the experimental SeriesPack shared controls and the
one-image x86 predicate control. Their outcomes do not establish uniformly
regression-free graduation.

## Work already established

The [interface decisions](interface-proposal.md) and
[current interface guide](../../../ikea/docs/interfaces.md) describe the adopted
vocabulary, storage-byte contract, selections, description framing and effects.
The broader revision is `876a8af0`; subsequent qualified corrections include
strict active-unit reads (`85fdfca9`), header dependencies (`80b93a5f`), copied
predicates (`d5fe5a8d`) and direct validated-layout binding (`cd55974d`).
The [complete-writer correction](evidence/interface-cohesion-v2-20261023/tp-api-final.json)
also removes accidental preserving methods from `writer<128>`. Complete
construction's spare-bit clearing remains distinct from preserving replacement.

TuplePack's general reader remains a two-pointer plan/view borrow. Its separate
predicate copies one code and borrows only the named view. Direct binding accepts
an authoritative validated layout and full-size code index without preparing
packet controls. The layout, plan and intermediate reader may expire; the
view's value, address and backing remain stable. Observed predicate size is
16 bytes on the tested targets; its checked result still uses return memory.

The [Intervals graduation audit](../intervals/AUDIT.md) records removal of the
public Boundlist compatibility module, migration of useful checks to their
owners, replacement of the guides from the stronger old material, corrected
and rendered diagrams, sparse sources and the three actual consumer seams.
Intervals owns borrowed search/geometry, while StreamPack owns compact images
and retirement. The old public module and broken interval-image Markdown are
corrected issues, rather than outstanding defects. Historical names in retired
research are deliberate source references.

The [qualification account](evidence/interface-cohesion-v2-20261023/README.md)
distinguishes source-specific core/header checks, sanitizer proofs, integration,
foreign compile-only checks, object comparisons and native timings. The
[execution survey](reusable-execution.md) supplies the wider mechanism context;
its earlier proposals are research history, not directions for this handover.

## Observed performance problems and unresolved attribution

| Area | Observation still relevant | Evidence boundary |
| --- | --- | --- |
| SeriesPack preserving writes | R4 retains roughly 5–7% AVX2 substituted all-row losses. Some AVX-512 ordinary point samples are sharply bimodal. | [Native R4](evidence/interface-cohesion-v2-20261023/native-r4.json) and [coverage audit](evidence/interface-cohesion-v2-20261023/coverage-guard/README.md). Packed-payload stores remain equal; selected coverage helpers still add work. |
| TuplePack body writes | The AVX-512 two-field body-write comparison loses roughly 15%. The timed loop and selected 61-byte kernel have equal bytes but different placement. | [Point-write audit](evidence/interface-cohesion-v2-20261023/tuple-point-codegen.json). Placement is a hypothesis, not an established cause or recovered performance. |
| Original cohesion filters | The f153-to-R4 comparison retains eleven x86 filter losses: seven AVX2 and four AVX-512, including about 10.9% for a tight width-five less-than independent-eight case. | [Native R4](evidence/interface-cohesion-v2-20261023/native-r4.json). These are separate unresolved observations from the later projection comparison. |
| Copied predicate filters | Seven x86 predicate-route cases and twelve shared-decode controls lose over 5% in the separate-process projection cohort, despite all 324 complete block cases improving. | [Projection findings](handle-boundaries/PROJECTION-NATIVE.md). The smaller handle and reduced selected work do not explain away the losses. |
| Focused predicate control | Selected op2 losses persist at about 6–8% in one process/image after strict identity checks. Some duplicate/shared controls also change. | [One-image study](handle-boundaries/PREDICATE-PANEL.md). This is a different cohort and a separately emitted old-code duplicate, with different function/state addresses. |
| Direct predicate binding | All 81 cases beat rebuilding a generic plan. Against an already shared plan, ARM has twelve losses over 5%, reaching about 12.1%; x86 has no loss over 5%. | [Complete preparation commands](handle-boundaries/direct-bind/README.md). All ARM one-query cases improve; repeated-query losses remain despite a shared filter body and the same query-loop mnemonic sequence. |
| Experimental shared dense controls | Fresh shared AVX-512 `read16` loses about 22–23% against fresh copied bindings at 64 queries per block. The direct-column route is faster in those cells; retained shared readers have a much smaller loss in those same cells. Separately, three retained-shared one-query point cases lose about 6–7% across the three profiles. | [SeriesPack control comparison](handle-boundaries/series-controls/README.md). This prototype changes no public SeriesPack API; handle size and shared tables alone do not determine complete command cost. |
| StreamPack | An ordinary AVX-512 reader loss persists with equal retired work. A common-alignment control obtains parity by slowing the baseline. | [Stream diagnostics](evidence/interface-cohesion-v2-20261023/stream/README.md). That control did not recover candidate cost. |
| Bec256 | Random-encode rankings reverse under fixed-iteration/common-argv0 controls despite equal relevant code and retained addresses. | [Bec diagnostic](evidence/interface-cohesion-v2-20261023/bec-diagnostic.json). Original negative timings and later reversals both remain; no general causal attribution follows. |
| Intervals G64 | Independent-prefix work narrows the random independent gap but does not eliminate it; the recorded NEON comparison remains about 3.316 versus G16's 1.917 ns/request. Other request topologies favor serial execution. | [G64 study](../intervals/RAISE-BAR.md). Spacing, execution grain, latency and independent throughput have different tradeoffs. |

The SeriesPack prototype also exposes consequential operation-grain costs.
Its runtime-selected typed one-query point control costs roughly 1.33–2.14 times
direct columns across the profiles. Typed range and some typed `read16` controls
also lose; two NEON one-query direct-column point cases lose against retained
copied bindings. The [complete comparison receipt](evidence/interface-cohesion-v2-20261023/series-controls-native.json)
retains all 62 declared route-pair cells with losses of at least 5%, alongside
the full 900-case table. These are costs of specific complete experimental
controls, with their distinct admission/reuse boundaries.

The rejected cached-tight predicate and three journal controls are useful
negative results. Cached state shortened selected paths but substantially hurt
complete preparation/convenience commands. The journal controls did not remove
the selected append-path instruction additions in their unlinked objects; the
local-cursor variant increased those counts from 63/66 to 65/68. They were
rejected without native timing. Neither object size, whole-object text totals nor selected instruction
counts serve as a general timing model.

Original f153-to-R4 comparisons, the strict-access correction, the isolated
24-to-16-byte projection comparison and the final controls have different source
cuts and questions. Multiplying their ratios does not qualify the final tree
against f153. Equal bytes do not establish equal execution conditions, while
different bytes do not identify the cause of a timing difference. Unlinked
instruction allocation also differs from actual linked callers in recorded
cases, including the AVX-512 independent-eight path.
The f153 label identifies the library baseline: its timing capture also includes
the recorded independent point-oracle harness amendment. A bare checkout is not
the complete measured source recovery.

## Reuse and ABI questions still open

- **Boundary cost versus resident state.** Raw two-word return/pass, checked
  `expected` return, const-reference reuse and crowded callers have different
  costs. A small borrowed handle leaves its named view and shared controls
  resident elsewhere. Some current APIs consume readers by reference, so a
  smaller value alone does not change their ABI.
- **SeriesPack's public endpoint.** The shared-control experiment compares
  specific admitted headless dense laws and existing operation bodies. It does
  not establish one public shape/placement authority, general mismatch semantics,
  or a replacement for current expression, placed and typed bindings. The
  prototype's public index field is an experimental admitted-value precondition.
  Measured fresh, retained and direct-column routes have materially different
  results even when they select the same existing operation bodies.
- **Simultaneously live state.** The SeriesPack fixture retains 64 bindings and
  visits them sequentially. That is not seven register-live bindings, independent
  request streams or a qualification of crowded shared-binding calls.
- **Metadata and executable footprint.** All read/output-carrier tables,
  initializer columns, extent facts, typed controls and fixture bindings coexist
  in the prototype. Reported table or handle bytes do not establish an optimal
  resident representation or an allocator-class saving.
  The prototype observes 24-byte shared versus 40-byte copied dense bindings,
  alongside 15,872 bytes of static columns; these are target-specific facts.
- **Different useful operation grains.** Frame already has operation-major
  catalogues, and its typed bind-and-immediately-call wrappers can disappear.
  Runtime erasure and generic preparation have different remaining costs.
  The same small object cannot be assumed to remove equivalent work everywhere.
- **Other reuse surfaces.** StreamPack's existing retargeting requires the exact
  shared layout object. Bec's span/population/length are observable proof state.
  Owning Hashtable captures retain backing; a staged reader borrows its outer
  capture. Their costs and refresh rules
  are not established by TuplePack's predicate results.

## Current preconditions and unresolved composition boundaries

The following proof and lifetime requirements are current contracts. The
remaining question is their maintenance and visibility in owner compositions;
the source-specific witnesses do not establish arbitrary combinations.

**Proof scope.** Attachment proves placement, rather than authenticating the
interpretation or contents of arbitrary bytes. TuplePack views retain extent,
and their binders rely on the owner supplying the authoritative law. StreamPack
attachment and content initialization/validation are separate. Bec content
validation cannot authenticate an unrelated address/population/length
association. These are consequential differences behind similar factory names.

**Borrowing.** General TuplePack readers borrow plan and view; predicates borrow
only the view. SeriesPack expression erasure can borrow a reader, while dense
erasure copies pointer/count proof. Owning Hashtable captures retain backing,
while `native_reader::borrow()` returns a pointer-only `staged_reader` dependent
on the outer capture. Other operations retain different metadata by value.
`reader` therefore denotes a role, not one
interchangeable lifetime or relocation model.

**Content-derived state.** Equal shape does not establish the current dictionary
edition, Bec contents, selected logical version, validity/override association
or initialization. Existing old/new/old and retained-owner witnesses qualify
particular source epochs, rather than arbitrary future caches or representations.

**Effects and retargeting.** Journal entries borrow actual source identities and
plane mappings. Coalescing can update the previous last record, so `used` is not
an append-only event cursor. A changed placement or backing generation can
invalidate unresolved entries. The reuse measurements do not qualify such
transitions merely because a handle or shape remains unchanged.

**Mutation meaning.** Issued stores, preserved-neighbor reads, logical destinations
and summary dependencies remain different facts. Effects do not provide
beforeimages, transaction-wide pre-notification, isolation or publication. Raw
author stores deliberately omit ordinary effects/summary maintenance and may
omit construction's tile-slack work. The initializer experiment does not qualify
preserving/masked replacement, general summaries or unresolved multi-generation
journals.

**Capacity versus coalesced output.** Conservative additional-record admission
can exceed final journal occupancy. The new boundary fixture exposed a valid
Striped5/257-row initialization requiring ten records of admission capacity,
despite an initially assumed eight-record fixture. This was a corrected fixture
assumption, not a core refusal bug. The relationship between coverage traversal,
admission cost, final records and owner resolution remains part of complete
mutation cost.

**Coordinates.** Ordinary row masks, native packet-byte masks, interval ranks
and Frame block masks retain different origins and meanings. Shared `selection`
names and `bitset256` do not carry field/group/NULL/presence associations in a raw
mask value. An external Intervals rank filter does not extend neighboring
intervals into excluded coordinates.

**Schema and bytes.** Shared framing validates individual child laws; parent
counts, placement, sidecars and coherent version association remain outside it.
Opaque storage and numeric packets/maps deliberately retain distinct types.
Portable wire definitions do not constitute native big-endian or additional-ISA
execution qualification. The global execution-profile guard also reaches
metadata-oriented headers through `bytes.h`, narrower than a reader might infer
from target-neutral signatures alone.

## Actual consumer and workload coverage

| Consumer or workload | Remaining boundary |
| --- | --- |
| Engine Directory | Uses direct u32 endpoints and exact K-byte comparators under a retained Scope, rather than a SeriesPack width catalogue. Predicate or shared-reader wins are not measured Directory wins. Physical upper-bound and logical left-lower routing remain distinct. |
| StreamPack | Uses fused geometry/value paths, assigned compact-image law, initialization, tails and retirement. Generic interval reconstruction and a different checkpoint portfolio are not uniformly faster complete value operations. |
| Frame | Has its own catalogue, parent count/grant, dictionary editions, prediction and exception algebra. The settled five-check receipt at `4d0c7b32` covers a particular SeriesPack/Intervals boundary, not subsequent live algorithms or whole-Frame throughput. |
| Strict sparse TuplePack | The active-only correction has direct poison/guard correctness evidence. The later projection timings use Rows16; they do not establish Rows32/64 sparse throughput after that correction. Explicit common-window access is a separate owner grant. |
| Direct binding | Starts from validated layouts and warm resident payload. Complete layout validation, cold tables/data, allocation, publication and actual predicate-only application owners are outside the command fixture. |
| Shared dense controls | The native fixture covers u64 headless Local/supported Striped point/range/read16 and initialization across three profiles. Narrow carriers have correctness coverage; headed, placed, composed, preserving and summary-bearing performance remain outside this cohort. |

The merged allocator's current owner checks establish functional integration at
their recorded source cut. Faster allocation and physical Holds/Sections do not
establish initialized contents, a coherent logical version or wider read
permission. The predicate/control timing loops allocate their fixtures before
measurement and do not measure allocator speed.

Composite failure provenance also remains owner-specific. Module error meanings
and `describe` signatures are aligned, but a parent can collapse distinctions:
the composed metadata material maps a failed TuplePack attachment to
`Error::invalid_argument` in
[its boundary code](../orbital-mvcc/metadata/composed/material.cpp). The common
child vocabulary does not establish how much diagnosis survives composition.

## Documentation and research navigation

The major naming/ownership and image-rendering defects have recorded corrections.
Documentation quality still varies: the Ikea README foregrounds a component
table and abstract architecture; dependency/composition relationships and the
cross-cutting proof vocabulary take substantial reader effort. Hashtable usage
still contains the historical insert-rate comparison, and consumer guidance
retains experimental history. Module guides, usage tours and reference pages
have heterogeneous organization. These are documentation questions, rather than
new runtime failures established by this work.

Numerous source cuts make the word “current” easy to misread. Historical receipts
remain useful only with their own sources and limits. Full matrices, independent
headers, sanitizers, local owner tests, foreign objects and native timings prove
different facts. The earlier 417-function receipt is not the final source's
qualification. Existing unrelated navigation hints in allocator/Frame/catalog
documents are recorded separately from this task's owned links.

## Stopping state and recovery

**Storm / reduced laptop power (2026-10-23, 12:29 UTC).** Ashton requested
cancellation of any running or queued laptop benchmarks or heavy compute.
The local process check found none owned by this task, and no heavy work was
queued, so there was nothing remaining to cancel. All native runs had already
finished and their five instances were terminated. No further local benchmark,
build or heavy analysis is launched during closure; the remaining work is
documentation and Git handover. No unrun measurement is presented as completed.

The final direct binder is committed as `cd55974d`; its functional fixture and
the representative old-caller codegen bridge have separate receipts. The
one-image x86 study is committed as `cb15b150`. The final
[combined native cohort receipt](evidence/interface-cohesion-v2-20261023/execution-controls-native.json)
owns the direct/SeriesPack source capture, protocols, raw trials, actual linked
executables and recovery references. All builds and independent checks precede
timing; all five samples per case remain recoverable.
The two completed command studies, reproducible readers and curated evidence
are committed as `015bf9b2`. All 210 monitored experiment inputs still match
the native capture, including all 193 Ikea implementation/header files.

The final documentation check covers 693 Markdown files, 5,589 local links and
669 heading references. Six existing hints concern allocator, Frame and the
spike catalogue; none concerns this task's owned links. The scoped diff check
passes. Public Boundlist/block256 and the broken interval-image Markdown have
their earlier completed source/rendering audits above.

The first combined launch failed in runner receipt-path handling after CMake
configuration and before study compilation. Its two archives retain that
failure; they contain no timings. The corrected runner passed a success/failure
receipt smoke check before the successful launch. All five instances from this
closure—failed pair, successful pair and one-image control—are confirmed
terminated. There is no active experiment or compiler owned by this task.

The two earlier selective archives retain 195 and 46 local objects; the direct
codegen archive adds 140 files. The native workers retain their own source and
binary archives. Full compressed bytes and every member were verified for these
recoveries. The existing root `.keep-local` remains because its scope also
contains uninspected intermediates and final owner-check directories outside
the selected archives. No blanket deletion was qualified or performed.

Peer Frame changes remain owned by its task and are outside this handover's
implementation commits. Later Frame algorithms and throughput remain separate
from the settled source-specific owner checks above. No push, PR or additional
experiment is implied by this account.
