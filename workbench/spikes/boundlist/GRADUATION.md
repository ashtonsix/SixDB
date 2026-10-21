# Boundlist implementation

This records search qualification and the original extraction boundary. The
[ownership follow-up](OWNERSHIP.md) supersedes its U-bearing sidecar schema,
value-packet and compaction ownership. The [portfolio qualification](PORTFOLIO.md)
subsequently adds Relative Ends, independent carriers and the shared write bodies.
The later [API cleanup](API.md) supersedes the public names, paired-extrema helper
and implicit selection described here.

Boundlist extracts reservation metadata from StreamPack and adds reusable ordered
point/range search. The implementation lives in Ikea.
The closed prototype [findings](FINDINGS.md) remain separate from the production
comparisons below.

## Architecture

There is one ordered search contract: lower/upper boundary and first/last
occupied-extrema overlap. Sources provide count and ordering, with population
independent of endpoint equality. Native endpoints, checkpointed narrow deltas,
reservation metadata and caller-composed packed/ordered projections serve that
same interface. No public algorithm menu or universal integer/delta encoding is
required. A unique-key directory with exact occupied maxima performs one search;
duplicate handling does not add a second search to it.

`boundlist::sidecar` owns the curated existing reservation format. Direct/Width,
G16, optional U/D, fixed/full/immortal schemas, the version-2 eight-byte description
and the 256-rank capacity are preserved. Those are this physical recipe's laws,
not a cap on ordered key domains or directory population. Generic sources use
unsigned local ranks/count, explicitly bounded at UINT_MAX; larger directories
compose local sources with wider outer coordinates. Numeric queries are classified
before narrowing. Native unsigned lanes receive explicit ISA comparison; other
ordered domains use comparison without requiring subtraction or a metric.

Endpoint mutation remains with its representation owner: Engine maintains key
ordering/uniqueness, and a packed source can retain its SeriesPack/TuplePack writer.
Boundlist does not require every domain to implement the reservation writer.

StreamPack adds payload admission, byte reads/movement and payload effects. Its
existing names remain usable through aliases/wrappers. It depends on Boundlist;
Boundlist neither links nor includes StreamPack. Shared prefix/search bodies have
no dependency on the curated byte schema. Production typed accesses retain two
pointers, actual named source and shared immutable layout; the rejected prototype's
flattened seven-word adapter stays experimental.

One metadata law owns append, fixed-run append, packing and content validation.
StreamPack adds infallible payload work at the admitted callback. Append captures
count/extent before payload stores, avoiding alias-driven dependent reloads.
Metadata effects map from standalone planes0/1/2 to StreamPack planes1/2/3 through
one typed hook, preserving the original named source. There is no temporary
sidecar identity in published effects. Payload remains plane0.

The cold seam is a prepared local phase rather than a scheduler dependency or
an exposed group index that callers must reinterpret. Width routing removes the
global coordinate/checkpoint width, leaving a 16-byte selected-width pointer,
first rank and local threshold. Ordinary search inlines the same finishing body.
Population composition borrows the named source for final eligibility. Raw
geometric phases snapshot routing instead of borrowing the source object. The
owner retains all coherent completion dependencies; `local_window()` is a
useful hint target, not a full read grant or readiness guarantee. Raw direct-source
preparation completes inline; a numeric search binding can defer endpoint reads. Owner-specific frames and Loom selection
remain outside the primitive.

Physical ownership remains distinct from logical live search. Reverse includes
reservation slack and retired bytes. Physical overlap skips zero reservations at
its edges and can include dead storage. Logical occupied-extrema overlap returns
a candidate rank window with holes/local gaps still requiring owner checks.
Neither endpoint equality nor U==0 means an unpopulated keyspace rank.

The maintained [usage](../../../ikea/docs/boundlist/usage.md),
[reference](../../../ikea/docs/boundlist/reference.md) and
[examples](../../../ikea/examples/boundlist/) explain these boundaries independently
of the prototype. Engine owns directory policy, separator maintenance and full
index assessment; this does not graduate an Engine tree representation.

## Qualification

All five native profiles pass production semantic/boundary checks and actual
Loom/Orbital material histories. Local tests and both new examples pass under the
pinned Clang21 build with loop/SLP vectorization disabled. ASan/UBSan passes the
same five targets plus the actual material fixture and compact cold-frame lifecycle. Independent linear/widened
oracles cover mixed queries, all native counts0–513, exact tails/unaligned arrays,
partial/strided Width groups, high origins, duplicate endpoints, sparse/inverted
population and all four range closure policies. Custom sources include strings,
128-bit ordered values and a virtual top fence. Actual interleaved eight-query
searches and prepared finishing match the expected ranks.

Standalone reservation histories independently model R/U/dead state, replay
source-qualified effects and check packing/selected transfer/refusal. Existing
StreamPack checks and its literal boundary/schema fixtures still pass, including
full256×65535 blocks. All22 hashtable checks/examples pass locally. Thirteen
Boundlist and ten StreamPack ordinary/author headers compile independently.
Both existing StreamPack executable examples also pass after extraction.

All192 actual Loom/Orbital material histories pass locally in ordinary and
sanitized builds, covering old/fresh/late readers, count/tail initialization,
original-source effect replay, packing, transfer and coherent holder repair.
These histories use full private copies as a safety control. They do not prove
selective-sharing efficiency or end-to-end MVCC mutation performance.

Linked-consumer audits caught outlined shared carried traversal, append and batch
wrappers before selection. Explicit inline boundaries fixed those costs. A later
logical-range audit likewise caught and fixed the ordinary overlap wrapper.
Disassembly is checked on complete callers; leaf-kernel equivalence alone is
insufficient. The existing bounded llvm-mca diagnosis remains a model of isolated
native bodies, not a substitute for complete native measurements.

## Search implementation

The ordinary interface is lower/upper boundary and occupied-extrema overlap.
There is no strategy enum. Native unsigned arrays default to explicit cmov/csel
bisection followed by a bounded SIMD convergence window. Wide reads stay inside
the assigned array, including unaligned and protected-page tails. Other ordered
domains use comparison-only bisection; custom sources can supply fused boundaries
or prepared phases without becoming numeric/delta encodings.

`author::bind_search(direct_source)` prepares repeated searches of one unchanged
u16/u32/u64 source. Source setup caches extrema and a reciprocal. Private count
and sampled-rank-error rules choose a checked interpolation bracket or ordinary
search. Current admission uses N>=4096, at least32 and at most128 slots in the
proposed bracket, and bracket*32<=N. Both bracket edges are checked before
narrowing; a miss corrects with ordinary bisection. Preparation samples are a
profitability estimate, never a correctness premise. Constants resolve from
cached extrema. Any endpoint/count/placement change invalidates the binding.

The conservative minimum matters: seven quantiles can alias duplicate runs.
An experimental4-slot bracket looked exact at the sampled positions but caused
frequent correction and severe batch losses between them. Minimum32 preserves
the useful fixed-bracket result. Longer duplicate runs also exposed unprofitable
256-slot brackets in upper-bound consumers; those decline interpolation. These
are private tuning decisions, not keyspace or source-count limits.

Interleaved native batches retain the shotgun idea: independent queries issue
probes at each level before dependent convergence. They accept ordinary or
prepared numeric sources and preserve input order. This is distinct from a
multiway search of several pivots for one query. The old one-lerp implementation
paid wide integer division and retained a broad correction interval; rejecting
it did not justify discarding cached-reciprocal interpolation.

A full four-accumulator SIMD scan remains an opt-in source for small hot lists.
Its C1 and C8 rankings vary with lane width and ISA, so it does not replace the
general default. Caller heat and independent-query availability are composition
facts; Boundlist does not acquire cache residency or enter Loom itself.

For the curated Width sidecar, admission proves sixteen checkpoints readable.
A fixed SIMD selector masks inactive results and feeds the shared narrow-width
finishing body. Generic Width sources retain exact assigned-checkpoint reads.
Typed sidecar accesses expose the same16B geometric phase, avoiding a separate
cold adapter. Search/reverse protection must cover the complete padded checkpoint
window plus selected widths/population; a point-value metadata qualifier is not
a search grant.

Ordinary numeric completion and suspended phases share one classification/model
law, but ready results return directly on the immediate path. This avoids forcing
every hot lookup through a null-pointer phase representation. Ordinary source
composition prefers an available fused boundary; authored preparation prefers its
phase. Their geometric answers must agree.

## Performance and adoption

The [production scorecard](evidence/graduation/scorecard.md) shows absolute ns
for both controls and adoption. The [selected matrices and recovery](evidence/graduation/README.md)
retain all qualified cells, including losses, min/max/repetitions, metadata/frame
bytes, exact captured sources and raw worker archives. Native hosts run pinned
Clang21.1.8 with loop/SLP vectorization disabled in consumer TUs: Zen5 and Granite each have AVX2
and AVX-512 profiles; Neoverse V2 uses NEON.

For N4096/u32 positive geometry, complete dependent point lookup improves from
28.68 to23.28ns on Zen5 AVX2, 26.60 to21.60 on Zen5 AVX-512, 32.41 to28.86
on Granite AVX2, 29.49 to27.73 on Granite AVX-512 and40.50 to33.73 on NEON.
The fair throughput comparison is **actual batch8 against actual batch8**:
7.88→6.18, 6.96→5.73, 10.64→7.83, 9.65→7.00 and10.85→8.53ns/query,
respectively. N16384/u64 also improves across all five profiles. Per-query
preparation and child/hash consumption are timed; source-level setup is separate.

This does not make numeric preparation the default everywhere. At N256 the
binding provides little benefit and can add batch overhead. With long duplicate
runs interpolation declines, yet the generic bound-batch path can still cost
0.2–1.3ns more than the raw batch. Most significantly, the N4096/u32 sparse,
duplicate extrema-range consumer changes from43.97/39.17 to48.41/42.40ns
(serial/eight-way) on Zen5 AVX-512, and53.48/47.34 to60.88/57.75 on Granite
AVX-512. That consumer selects two prepared source descriptors per node and
executes both boundaries; a point-kernel win does not establish its crossover.
On NEON the same range consumer improves77.98/72.30 to56.39/48.83ns.
Keep ordinary sources for the losing regimes; numeric preparation stays opt-in.

Padded checkpoint reverse is a broad adoption decision: every candidate Width
cell across all five profiles improves over the exact-checkpoint selector in the
qualification cohort. Final complete W16 range lookup changes61.02→55.82ns
serial and45.78→38.41ns eight-way on NEON, removing the initial extraction
regression. Zen5 AVX-512 changes37.97→23.28 and21.61→8.32ns. Direct remains
the ordinary native-array path; stored bytes do not change.

Actual typed sidecar preparation is close to the independently staged padded
geometry body, but the raw group-index control can still win. In the large fixed
NEON range consumer, raw Loom16 costs46.24ns/query and typed Loom16 costs51.46;
for mixed counts these are38.31 and37.81. Serial typed costs403.31 and295.99ns,
respectively. Full/no-hint/manual Ring controls remain in the other matrices.
The phase is useful without claiming every staged consumer beats raw fusion.

Shared append/replace composition remains near its frozen pre-extraction bodies:
profile median ratios are0.997–1.002. The worst NEON append cell adds about1.1ns;
a scattered replacement adds about10ns to161ns. These are composed issued-effect
mutations, not end-to-end MVCC timings. Broader complete readers are generally
close to independent fused controls, but are not uniformly equal: one large NEON
Direct/omitted-U dependent reader is228.71 versus173.59ns; x86 scattered Width
reader gaps reach roughly6–15ns. The existing compact access is retained, and the
flattened seven-word experimental adapter is rejected. Those cells remain in
`reads.csv`; extraction is not a claim of universal fused-reader parity.

## Direct StreamPack parity and cleanup audit

The first production cohort compared readers with fused controls but lacked a
frozen pre-extraction access control; mutation timing covered append/replace but
not packing. The [follow-up scorecard](evidence/parity/scorecard.md) closes those
gaps without changing the search selection. Actual access/writer bodies are
frozen from4fb8fcea and checked against their source. Public view/layout and
unchanged native leaves remain shared, isolating the extraction instead of
claiming a build of the whole historical repository.

Across five profiles, 1,200 paired reader cells have profile-median current/before
ratios0.999–1.000; 320 mutation cells have0.997–1.000. These are descriptive
medians across distinct cases. All180 matched compiled point/projection consumers
have identical normalized instruction sequences after relocation/padding removal.
Large x86 samples nevertheless swing substantially in both directions; for example
one Zen5 AVX-512 Direct chain reports75.36→161.01ns with overlapping
58.43–178.33 and54.01–181.11ns ranges. Those observations do not demonstrate a
signed extraction cost. They remain visible. Carried traversal has a different
body; its largest median loss is0.121ns/value, alongside gains up to0.222ns.

Packing initially exposed Granite serial losses. The follow-up holds journal
placement constant between treatments and captures the immutable assigned count
before stores. It does not separately isolate those two contributions. With seven
repetitions and real restored fragmentation, profile-median packing ratios are
0.977–0.987 across320 paired cases. Individual regressions remain, up to122ns
per256-rank block; this is broad parity/improvement, not a claim that every
individual median improves. The first packing matrix is retained beside the
follow-up. Tests independently check every reservation, live byte, tombstone and
extent, and consume the actual source-qualified publication journal. Packing's
independent-eight-block stream is not per-slot interleaving.

StreamPack now has one small compiled TU for payload admission/validation.
Boundlist owns schema preparation, metadata validation, geometry/native bodies
and the shared schema switch. Three private StreamPack forwarding headers and
its old schema TU are removed. Basic StreamPack views no longer include search
or native-reader kernels; the small extent helper belongs to Boundlist. The
unused core-library link dependency is removed. Public names,16B typed access,
8B schema and stored bytes remain unchanged. The stored-byte specification has
one documentation owner in Boundlist; StreamPack documents the payload addition.

The selected parity evidence includes2,040 audited compiled consumer bodies with
no ordinary kernel helper calls. Local sanitizers cover the final count capture,
shared-output packing oracle and192 actual retained-reader histories. The
preceding dependency-only extent move produced byte-identical local complete
reader/mutation/packing executables; the later packing change has its own native
cohort. Header and CTest checks pass after the cleanup. Exact source/host/archive
identities, repetitions and report replay live with the evidence.

## Memory and measurement limits

Stored sidecar bytes are unchanged. Both typed accesses remain16B. A raw Width
phase is16B; generic population selection adds a source pointer and requires that
binding to survive. Numeric bindings total40B for u16/u32 and48B for u64, versus
16B for a raw native source; their query phases are32B. Range fixtures report both
prepared extrema bindings and population state separately from stored endpoints.
The binding's source preparation is timed as a repeated cache-hot loop, outside
steady-state lookup. It is not a cold-source construction cost or free work.

The original owner frame retained headers, phases and child results together,
costing96B for two phases versus72B for raw group indices. Reusing fields after
their last use puts both controls in64B frames. Those are actual C++ objects with
explicit union lifetimes, tested under sanitizers. Compact frames improve owner
state density; measured lookup time barely changes. They do not manufacture a
search-kernel speedup.

Append-with-reset operates on the first eight blocks even when its fixture
allocates64/4096; only replacement varies that benchmark's active random set.
Large read fixtures retain complete cycles and actual traces. Logical operands,
readable grants, allocated alternatives and process RSS remain separate.

Granite reports480MiB L3. The large cold Width operand bound is288MiB and Direct
416MiB; the combined544MiB includes the unused alternative store. Thus these
Granite cells establish large random-consumer performance, not above-LLC/DRAM
residence. NEON/Zen5 report36MiB/8MiB L3 respectively. Per-load service remains
unclassified. Loom comparisons use the actual scheduler and line-covered hints,
with manual staging and no-hint controls retained independently.

The main cold Method7 originally omitted its odd repetition because a trial-order
loop was not updated. Its two-sample cells are excluded. Original controls0–6
have three repetitions; all following cohorts enforce exact repetition coverage.
This was a harness defect caught before retaining production comparisons.

This graduates leaf search and reservation composition, not an Engine index.
The retained-reader histories use full private copies and establish safety;
they do not qualify selective-sharing efficiency or end-to-end MVCC mutations.
Neoverse V3 and SVE2 remain unqualified by this native panel.
