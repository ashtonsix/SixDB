# Directory performance budget

These are aggressive proposed ceilings, not measurements of a new directory.
The relative headroom requirement overrides a looser absolute number.

The current instruction is paper review before more implementation. Existing
read/producer results qualify their exact captured operations; they do not price
the unfinished mutable directory. The current complete algorithm is in
[Pseudocode](PSEUDOCODE.md), with its [byte ledger](DESIGN.md#compact-candidate-byte-and-ownership-ledger).

Ashton's subsequent steer makes useful steady-state THROUGHPUT the primary
decision metric. Compare completed useful operations/s at matched ready window,
output and snapshot/publication semantics, alongside owner/helper/process CPU,
footprint and bounded maintenance debt. The scalar dependent timings below are
diagnostics and trade-offs, not mandatory rejection gates against a throughput
win. A synchronous independent-key loop is reciprocal-throughput evidence, but
does not establish software-exposed overlap. CPU time includes stalls; less
CPU-ns/op alone does not prove fewer instructions or bytes. Keep one physical
peer policy through each mixed trace rather than splicing standalone minima.

## Complete Engine headroom

For each competent, matched complete-request comparison, let P be the faster
of Calico and LMDB on the same host, workload, output, snapshot/transaction
lifetime and durability policy. When only one has a valid equivalent operation,
use that comparator and identify the missing one.

The design objective is complete Engine time at most `0.50 P`, with an initial
directory-plus-binding allocation of at most `0.25 P`. That leaves at least
half the Engine envelope for segment access, summaries, execution, remaining
publication work and output. Charge the SUM of directory visits and changes
required by the request. A five-layer change does not get five copies of the
complete-request budget.

These fractions are planning allocations, not new module contracts. A proposed
reallocation must show a complete cost sum that still preserves the 2x goal and
substantial remaining headroom; a miss does not automatically justify raising it.
Use a separate P for each metric: elapsed reciprocal throughput, dependent
serial time, CPU time and request p99 are different observations.

For request tails, aim for complete Engine p99 at most `0.67 P99`; directory
work must leave the same substantial fractional margin. This is a target for
actual request distributions, not a claim derived from a median loop mean.
Durable I/O and resource waits require separately matched wall-time results;
they are not removed by a CPU target.

The directory-record mechanism comparison also aims for at least 3x lower cost
than native LMDB for its equivalent operations. A mechanism win does not relax
the complete Engine budget. If a tiny existing-transaction LMDB write makes
that ratio unattainable, retain the loss: batching/publication and the complete
Engine comparison must still be reported honestly.

## First operations and matched boundaries

The first directory-record controls are concrete and separate from a row engine:

| Control | Engine-side work and native peer |
| --- | --- |
| M1 exact directory-record get | One index, L4, J0, initially 8,192 segment entries; unique big-endian u32 physical-prefix keys and 64-byte descriptor values. Exact hit/miss consumes all eight words into the same digest/output. LMDB uses ordinary `mdb_get` in the matching read transaction; no custom Store adapter. |
| M2 directory-record replacement | Replace one inline locator word, stable routing bounds/representation/count. Input is preconstructed directory metadata; execute actual private directory edit/capture/prepare/install and request-local cleanup. LMDB replaces that same 64-byte value in one write transaction per Engine publication. This opaque metadata fixture does not establish real segment-locator association or producer correctness. |
| M3 genuine projected consumer | Actual selected material and native/Ikea binding; request one u64 field and consume that value, including visible delta correction. Read only required descriptor fields, not an artificial 64-byte materialization. Peer returns the same field/output through a competent native layout. Include fixed geometry/pointers with changed committed extent. This is composed directory/material access, still not a complete row-update engine. |

Alongside opaque M2, retain the FIRST actual locator-replacement treatment:
validate the before binding, construct/capture finished material using real
native/Ikea writes and Loom backing, derive its actual Part BYTE locator, then
edit the directory and publish all required material/directory Owners in ONE
packet. Time from before material construction through install/local cleanup;
expose directory-only attribution separately. Old/fresh readers dereference
the corresponding material through the directory and check useful bytes.
Numeric fixture locators or prefinished material outside this interval cannot
replace or underprice this treatment. LMDB matches its semantic metadata/data
change in one transaction and may pack them into one value when appropriate;
it is not forced to mirror Engine's Object count. This is a chosen moving-Part
producer, not a requirement for ordinary stable-locator data writes.

M1/M2 use no duplicate physical keys. Their logical-bound fixtures include
runs of eight equal keys, shifted across storage-page boundaries, plus misses
and gaps. Logical SEEK/range controls return all matching segment prefixes in
physical order; a point request that expects one result may not silently select
one duplicate. Comparator cursors must return the same multiplicity/output.
For these SEPARATE logical-seek cases, load the actual routing bounds too.
LMDB compares a native ordered `(routing_max_K,physical_prefix)` index, either
covering `(routing_min_K,Dirent64)` or a bounds index plus physical-key primary.
Price the chosen layout's real lookups, updates and footprint; a physical-prefix
`mdb_get` alone is not the logical-seek comparator. Closed/open edges preserve
all equal maxima as required, with no invented unique logical fence.
M2 repeats with batches 1/16/64: one complete atomic LMDB write transaction per
Engine packet, identical before/after visibility. Read-cohort anchors below are
NOT batch-write anchors, and scalar write timings are not divided by batch size.

Each first comparison has uninterrupted current-cut and declared retained-read
variants where applicable. Engine Borrowed is a complete uninterrupted call;
retained Engine Scope and LMDB read transactions deliberately preserve their
selected cuts. For publication checks, open old readers BEFORE the write, defer
some first lookups until AFTER install/commit, then open fresh readers. Verify
old/fresh states and final cleanup in both arms. A borrowed call cannot become
a retained cursor or wait by changing its label.

The first F32/independent comparison includes an early dispersed sentinel:
uniform random probes over a directory sized beyond the measured LLC, with
resident upper levels, randomized leaf/payload placement and aged history/
allocator inventory. Record actual footprint/residency controls; if coldness
cannot be established, say so. Cross F32/F128 with co-located/independent
ownership at C8, then C16 copy/source tradeoffs. Do not postpone this until a
warm-only architecture has grown around its result.

The later COMPLETE Engine request starts before request admission/current-snapshot
selection and ends after directory navigation, exact data/delta/summary work,
actual material publication where required, requested output and local cleanup.
Begin with a projected u64 point/range and one-field update, then full-record
output. For duplicate logical keys return all matching `(physical6 B,field)`
results under the same declared order; include physical-key identity in the
peer's composite-key or native duplicate representation. Compare competent
LMDB composite-key and, where appropriate, DUPSORT/cursor layouts rather than
forcing extra peer lookups by adapter design. Atomic update batches have the
same visibility boundary and output in each engine.

Calico's complete-request entry is `calico::runtime::prepare` followed by its
prepared `execute`/row-buffer or visitor path as used in
[rig_ycsb.cpp](../../../../calico/workbench/benchmarks/ycsb/src/rig_ycsb.cpp).
Adapt the declared projection/duplicates/batch semantics through that real
entry; if it cannot express a matched atomic batch or retained-read case, mark
that cell unavailable. QHash's native prefix-to-offset path is a separate
access-method reference, not Calico end-to-end. None of M1/M2/M3 is relabeled a
complete Engine-versus-Calico/LMDB win.

## First collectors and denominators

- Dependent serial loop: next request depends on the consumed result; report
  elapsed ns/completed logical operation for the full declared boundary. This
  is neither CPU time nor sampled request p99.
- Independent loop/Ring: report completed requests divided by elapsed time at
  stated active concurrency, with bootstrap, admission, refill, output and drain.
  Report reciprocal throughput separately from request latency.
- Atomic batch: report elapsed ns/BATCH and ns/completed item, packet size,
  changed directory/material Objects and transaction size. Failed/retried items
  do not inflate the successful denominator.
- Sampled request latency: choose input indices independently of key/outcome;
  sample from first attempt/admission to terminal result, including waits/retries.
  Include a no-sampling control, sample count/rate, p50/p95/p99 and maximum;
  compare the same sampling rule on peers. Never derive tails from loop means.
- Refusal/retry: count offered, committed, refused, aborted and stale attempts.
  Wall/CPU totals include failed work; report terminal refusal latency separately
  from successful-request latency and bounded backlog, not as fast successful work.
- CPU/stock: collect owner-thread CPU, each helper's CPU and process total,
  including native Pool replenishment, catalog/descriptor PMR, maintenance and
  final settlement. Charge benchmark intervals separately from setup/warmup;
  show warm stock versus depleted/aged stock and whole-history replenishment.
  Do not move helper CPU or unbounded stock consumption outside the comparison.

Use existing [comparison](../../benchmarks/comparisons.md),
[capture](../../tools/README.md#captured-experiment-runs) and
[replay](../../benchmarks/README.md#replay-and-summarize) helpers as useful.
These are the first workload-owned collectors, not a proposed generic framework.
A directory oracle checks offered metadata transitions only. It cannot prove
that a producer correctly omitted an event, chose a representative event mix,
or maintained base/delta/validity. Those need an independent semantic producer
oracle in a material/complete-request experiment; M3 exercises binding, not the
entire producer protocol.

## Existing reference points

| Evidence | Observation | What it can constrain |
| --- | --- | --- |
| [Calico YCSB A–F](../../../../calico/workbench/benchmarks/ycsb/README.md) | On c8g.4xlarge, C: Calico 1.475M/s, LMDB 1.129M/s; D: Calico 0.897M/s, LMDB 1.189M/s. Same 1,000-byte records, one client, barriers disabled. | Reciprocal complete-loop means are about 678/886 ns for C and 1,115/841 ns for D. They are not individual latency samples or isolated read/insert costs. |
| [Calico QHash](../../../../calico/qhash/REPORT.md) | Zen5 clustered eight-byte lookup 123 cycles; 64-byte-key insert/update/delete 318/263/254 cycles. Distinct eight-byte lookup loses badly. | Prior access-method cost and its clustering sensitivity. No SixDB MVCC, segment body or full request is included. The report has cycle/ns inconsistencies; use recorded cycles, not a frequency conversion as new timing. |
| [Orbital canonical selected reads](../orbital-mvcc/performance/FEEDBACK.md#canonical-selected-read-specialization) | Zen5 larger batch point read 77.18 ns; equally improved mapped LMDB 76.89 ns. | Foundational encoded-body work was already in tens of ns. This is not ordinary row-key LMDB or full directory navigation. |
| [Coherent read fusion](../orbital-mvcc/performance/FEEDBACK.md#coherent-read-fusion) | Tiny coherent scalar selected-Object read 21.46 ns. | Selection and useful codec work can be short. Preidentified warm Objects and three-row bodies do not establish a directory budget. |
| [Current Hashtable](../../benchmarks/hashtable/ACCUMULATION.md) | Zen5 K30/b10/V64 independent hit 3.782 ns; full-value existing RMW 4.069 ns; existing-strategy upsert 5.072 ns. | Real Ikea private-owner work. These are reciprocal throughput, not serial latency or retained clone/publication costs. |
| [Current Boundlist](../boundlist/evidence/graduation/scorecard.md) | Zen5 AVX-512 serial 256-u32 point consumer 16.58 ns; prepared 4,096-u32 consumer 21.60 ns. | Searches plus dependent child consumption. Preparation, key distribution and node shape matter; do not add a binding per point. |
| [Actual allocation](../allocation-live-tail/README.md#evidence) | Zen5 prepared finite allocate/release 56.12 ns; prepared Window 32.25 ns. | Favourable recycled inventory. No capture, mapping/first-touch, retained history or p99 bound is included. |
| [Actual compiled catalog](../orbital-mvcc/performance/FEEDBACK.md#compiled-object-catalog-boundary) | Tiny retained scalar update 1,205.23 ns on the recorded Zen5 source. | A complete earlier caller, including work we must attribute. Its overhead is neither a new target nor proof that it is unavoidable. |

For illustration ONLY, the Calico C mean gives an Engine goal of 339 ns and a
169.5 ns total directory allowance. D gives 420.5/210.3 ns against the faster
LMDB mean, but D mixes inserts and reads; it supplies no scalar insert ceiling.
Do not transplant those ARM numbers to Zen5 or infer update costs by subtracting
different workload cells. Same-host native controls must replace these anchors.

## Current matched anchors and headroom

The ARM full64B material consumer now has a stronger matched Calico anchor than
the historical row-key LMDB numbers below. The ready point comparison has one
snapshot per64 queries and consumes all eight words; it does not include an
arbitrary future Engine request or prove retained-read parity with Calico.
Use its faster peer to constrain this SAME operation:

| Segment population | Calico best tested ns/item | Native LMDB get/cursor ns/item | Complete Engine <=0.50P | Directory/binding <=0.25P |
| ---: | ---: | ---: | ---: | ---: |
|8,192|85.978|147.965 /150.764|42.989|21.495|
|262,144|143.905|391.095 /374.649|71.952|35.976|

The frozen graph7 full material consumer measured4.741/29.338ns/item. After the
allocation/header/metadata changes, grain1 G16 staged measured4.393/21.687;
G64 measured4.384/25.000 and G2564.499/27.260. The new runs have paired native
BE4 controls, not a fresh Calico arm. Historical Calico arithmetic would leave
50.265ns/item at262K after the G16 full consumer. This is conditional arithmetic,
not measured remaining Engine work. These values neither select a universal
allocation/leaf grain nor establish the cause of the staged throughput change.
See [the measurements and source boundaries](FINDINGS.md).

The finite64-row producer has four different matched LMDB trace anchors. Calico
mixed/SUM128/atomicB64 equivalence is still unavailable, so do not use a different
Calico workload to tighten or relax these cells:

| Atomic request/trace | Faster native LMDB ns/request | Complete Engine <=0.50P | Directory/binding <=0.25P | Best tested finite producer ns/request |
| --- | ---: | ---: | ---: | ---: |
|B1 hot2, no query|3015.01|1507.505|753.753|243.387 plain|
|B1 hot64, query/fold/refusal|3830.30|1915.150|957.575|536.444 indexed|
|B64 hot2, repeated keys|3120.37|1560.185|780.093|310.354 plain|
|B64 hot64, query/fold/refusal|7617.87|3808.935|1904.468|2033.020 packed eager|

The right column already includes producer work; it is not directory-only time.
Each is a whole fixed-policy trace, not a license to combine policy minima.
One request can include queries, folds and a retry; do not divide these B64
figures into64 scalar-update latency samples. No directory relocation budget
fit follows from ordinary writes that change zero directory facts.

The new unmasked scan has actual same-host eligible peers. LMDB is faster than
the tested prepared Calico scan; do not reuse the point-read P for it. G16's
first complete material consumer gives this conditional arithmetic:

| Segments/range | Paired native P ns/row | Complete <=0.50P | Directory/binding <=0.25P | Current full material consumer |
| --- | ---: | ---: | ---: | ---: |
|8K quarter|8.174|4.087|2.044|2.176|
|8K whole|8.307|4.154|2.077|1.983|
|262K quarter|8.331|4.166|2.083|2.342|
|262K whole|8.839|4.420|2.210|4.824|

The last cell misses even the whole-request allocation; G64/G256 whole-layer
consumers do too. Directory-only attribution is still separate from this useful
material consumer. The initial masked full-cursor peer needs sparse/zero-policy
qualification before it supplies an acceptance anchor. See [Findings](FINDINGS.md).

The paired dense-rank refinement measures 4.023/6.225 ns per full-material row
at G16/G64 against 8.873/9.098 for native LMDB. Below half that paired peer time,
the conditional remainder is +0.413/-1.677 ns per row. This is already a useful
material consumer, not a directory-only measurement. The roughly 5% ratio of
pooled medians includes variability: paired median gains are 3.29%/4.24%, and
one G64 process ties. The four-arm lookahead control must beat this strongest
direct procedure; its bank stores/loads and setup count. Even a faster kernel
does not establish complete Engine fit. The grouped DUPFIXED native peer has
since passed behavior qualification, but remains unmeasured and could tighten P.

The bounded 64-pointer lookahead trial did not improve the strongest direct
path: owner-CPU paired medians lose at both G16 and G64 with and without hints.
Do not include a speculative lookahead gain in the budget. Keep the negative
control and its source-capture timing disclosure in
[Findings](FINDINGS.md); it supplies no new acceptance P.

The subsequent competent sparse control measures one ready bit per64 candidates
with actual native run seeks and diagnostic tracking removed from its timed
specialization. Whole-query Engine/native-best times are0.392/2.255us (8K
quarter),1.609/10.550us (8K whole),21.563/157.728us (262K quarter) and
133.879/944.335us (262K whole). These component cells have substantial conditional
room. All-zero masks instead measure the common bitset/lifecycle work:
3.220/3.622us at262K whole, with no entries visited or consumed. That misses2x;
do not use an entry-throughput ratio or the historical full-walk zero result.
Neither cell closes the complete Engine budget. See [Findings](FINDINGS.md).

For membership/balance, hash growth, extra-list packing and schema operations,
matched P is not measured yet. The absolute ceilings below
are engineering targets; measure the competent peer at the same boundary before
claiming complete headroom. A blank peer cell is not an automatic win.

## Historical native navigation anchor

The [retained native row-key LMDB cells](../../benchmarks/orbital/evidence/selected-allocation-20261007/comparison/cells.csv)
give these initial ceilings, rounded down. A faster MATCHED Calico result
tightens them. These are reciprocal median throughput, not individual latency
or CPU cost. The old allocation candidate omitted MVCC and is not being revived.

| Population / operation / snapshot | LMDB mean ns | Complete Engine goal ns | Directory + binding allocation ns |
| --- | ---: | ---: | ---: |
| 8K point, fresh transaction/request | 184.40 | 92 | 46 |
| 8K point, read cohort <=64 | 146.61 | 73 | 36 |
| 262K point, fresh transaction/request | 270.53 | 135 | 67 |
| 262K point, read cohort <=64 | 239.44 | 119 | 59 |
| 4.2M point, fresh transaction/request | 740.26 | 370 | 185 |
| 4.2M point, read cohort <=64 | 711.63 | 355 | 177 |
| 8K partial update, transaction/request | 1,526.22 | 763 | 381 |
| 262K partial update, transaction/request | 2,107.48 | 1,053 | 526 |
| 4.2M partial update, transaction/request | 3,286.12 | 1,643 | 821 |
| 8K random insertion, transaction/request | 1,740.10 | 870 | 435 |
| 8K deletion, transaction/request | 1,556.43 | 778 | 389 |

Source: capture `595e07fa…`, c8a.xlarge EPYC 9R45, Clang 21.1.8, LMDB
0.9.31; three-trial medians. [Flags and request lifecycle](../../benchmarks/orbital/measurement.md)
include ordinary COW/locking with flushes disabled. Read cohorts close before
writes. A fresh Scope must be charged against the fresh-transaction row, not
the cheaper cohort row. Neither this anchor nor the proposed directory proves
durability parity. Record population and DIRECTORY segment population are
separate dimensions; do not assume one segment per record or one segment per
65,536 records without the actual physical-key population.

## Resident operation ceilings

Baseline: prepared index/schema, K<=8, resident working set and a two-level
ordered directory. State the consumer: full 64-byte directory enumeration for
M1, or actual requested fields/material for projected work. State Borrowed or
fresh/retained Scope explicitly; their lifetime boundaries are not interchangeable. Times are per completed operation. The complete
budget includes Scope enrollment when used, at its declared request/batch boundary.

| Operation | Proposed ceiling |
| --- | ---: |
| Ordinary-table exact prefix hit/miss | 10 ns |
| Ikea hash exact prefix hit/miss | 20 ns |
| Sorted u16 suffix-mask/rank group hit/miss | 15 ns |
| Ordered seek to first relevant segment | 35 ns |
| Additional resident ordered-node visit beyond two levels | 10 ns |
| Range startup, including both endpoint policies | 60 ns |
| Same-page cursor continuation, full dirent consumed | 3 ns/segment |
| Cursor crossing into the next selected page | 20 ns + returned dirents |
| Resident range scan | <=60ns startup +3ns/visited dirent +3ns/selected full64B material +20ns per actual directory-page crossing, PLUS actual mask-word processing when supplied; also obey matched directory/complete-scan fractions |
| Already-selected inline Part projection | 3 ns/Part |
| Extra Part projection through a selected StreamPack block | 15 ns/Part |
| Bind an additional required block/version for projection | 15 ns |
| Ordinary data update, directory facts unchanged after initial segment lookup | zero directory mutation/capture/publication; no directory call when producer already proves this |
| Qualify an offered possible directory change and omit it | 10 ns/input; no route or full extra-list scan when supplied proof suffices |
| Change one dirent, stable bounds/representation/Part count, complete scalar publication | 250 ns |
| Representation change, unchanged extra-list reservation | 350 ns |
| Extra-list growth or Part-count change, <=8 extra locators, without packing | 400 ns |
| Bounds change staying inside a routing leaf | 350 ns |
| Each ancestor whose routing extrema actually change | 150 ns |
| Segment insert/delete without a storage split/merge | 400 / 350 ns |
| First/last member of a hash group, including its ordered presence entry | 435 ns |
| Leaf split, merge or redistribution, including publication | 1.5 us |
| F32 co-located leaf structural-repair candidate | 375 ns including publication/charged retirement; capture all output Parts |
| Each cascading internal split/merge level | 375 ns F32;400 ns F128 |
| Structural work amortized over adversarial threshold histories | 75 ns/membership change |
| Offered five-directory-replacement pressure case, one coupled publication, no balancing/packing | 350 ns TOTAL |
| Clustered batch of64 distinct locator edits, shared copied chunks | 35 ns/changed dirent |
| Dispersed batch of64 distinct locator edits | 75 ns/changed dirent |
| Local extra-block packing, <=16 live lists / <=128 extra locators | 1 us + required dirent repairs |
| Append/extend schema definitions, direct-name candidate | 300 ns for one; batch goal +50 ns/additional definition, plus actual descriptor bytes; D actual changed records, header only for actual index-context change |
| Bind an already-admitted representation definition | 5 ns |
| Cold historical-definition cache fill | 100 ns + actual descriptor parsing |
| Refusal before mutation | 50 ns beyond the reached admission work |
| Abort private work / stale-basis retry | Actual abandoned work and cleanup charged; <=100 ns fixed orchestration, plus owned resources |
| Bounded old-version maintenance | <=10% of directory CPU over the complete history; backlog/memory must remain bounded |
| Bulk construction | <=25 ns/segment private construction; <=100 ns/segment including capture/publication, plus actual key/locator bytes |
| Explicit descent to another layer | one exact-prefix goal at that layer, plus its real Part/geometry work |
| Small auxiliary metadata get/put/erase | same matched read/edit ceiling as the chosen real representation, plus actual key/value bytes; no unbounded plan/telemetry computation hidden here |

Apply the absolute goals to the corresponding engineering operations. Apply
the relative allocation to the SUM of actual directory/binding/publication work
in the MATCHED request/workload and the same statistic:

```
directory_metric(actual_complete_request_distribution) <= 0.25 * matched_P_metric
complete_Engine_metric                               <= 0.50 * matched_P_metric
```

For standalone comparable scalar diagnostics, the effective goal is the tighter
of its absolute goal and relative allocation. Throughput remains the primary
selection criterion under the later steer; retain and explain scalar misses.
A structural-event latency must
instead be compared with the corresponding structural event or actual complete
request tail, not with an unrelated loop mean. Charge its frequency and all
cleanup in workload means as well. An isolated 1.5 us repair goal is not permission
for every request, or the measured p99, to spend 1.5 us in the directory.

The zero-change row excludes the initial lookup and the producer's data/version
work; those remain in the complete Engine request. Unchanged directory facts
must not cause an unnecessary directory lookup, clone or publication merely
because a record or summary changed. A summary min/max change is not itself a
directory-range change.

The scan formula is a component target, not permission to omit mask input work
or claim a flat60ns all-zero query. Count actual words read, masked-off candidates,
seeks and lifecycle cost. An all-zero query has no returned-entry throughput;
retain its whole-query result as a control, including the existing2x miss.

The five-replacement stress row includes ALL offered directory changes, not
five times 350 ns. IF a producer supplies five segment replacements AND each
moves a referenced Part into a different directory chunk, the packet has ten
changes. That is one conditional composition, not a five-layer-write minimum.
Stable locators, deferred summary deltas and unchanged enclosing ranges can
produce zero directory changes. The proposed 350 ns stress goal is unresolved;
a 700/900 ns result would fail the small-write allocation if this event were
actually required on each request. Rare compactions/ports instead need both
their own latency distribution and their measured amortized contribution.

Full-enumeration reads consume the full dirent; projected reads consume their
actual descriptor fields and requested material. A small group is one directory selection;
a large ordered path is H routing selections, plus a payload-chunk selection
only for the independent-chunk recipe.
Initial schema/material qualification and actual Scope enrollment belong in the interval; amortize
only the declared cohort. Runtime segment-geometry and extra-block selections
are additional to the already-selected projection row.

Long keys add measured exact comparison/byte-access work; equal abbreviations
ALWAYS refine against full bytes. Rich extra lists add actual changed locator
bytes and block visits. Set separate K=16/32/64/256 and E=1/8/64/256 cells;
compare them with the same keys/output in LMDB. No flat 35 ns promise covers
arbitrarily long shared prefixes or an unbounded returned range.

For large random memory, first aim for <=150 ns known-prefix serial lookup and
<=250 ns ordered lookup with resident upper routing and one cold leaf, and
<=30/45 ns reciprocal throughput for independent Ring32 lookups. These are
ambitious hypotheses. Fully cold catalogs, long histories, page faults and
remote I/O have separate observations. Ring throughput does not establish
serial latency; cold-catalog dependency waves can change the throughput trade-off.
The [memory evidence](../memory-characterisation/FINDINGS.md) includes a roughly
140 ns Zen5 cold-line control and 260–280 ns cross-LLC peer cases. A single cold
dependency can consume much of the envelope. A 250 ns directory does not
automatically fit the 177/185 ns serial-read diagnostic allocation: show the
actual remaining Engine work and the primary throughput/CPU result before
proposing a reallocation. Retain cold serial latency as a separate trade-off.

## Charge the actual composition

For one mutation batch, retain the following ledger rather than estimating from
row count:

```
D = select_old_directory
  + route_and_edit + copied_bytes / observed_copy_rate
  + actual_backing_acquisition + immutable_owner_construction
  + capture(all admitted Parts of each changed directory Object)
  + coupled_prepare_and_install + required_maintenance + failed_attempts
```

Report coupled `prepare/install` once with the complete packet size, including
supplied segment Objects. Conservatively include that entire time in the
directory comparison as well as showing it separately; never claim an isolated
directory publication cost by subtracting unlike trials. A complete Engine
result counts it once.

Stock capture's metadata work scales with all admitted Parts. Orbital
`a5c0a0bb` sizes larger native/mixed uniqueness scratch by advisory fresh suppliers
and preserves final post-callback checking.16 retained+1 fresh now avoids the
former136B request;17 fresh still requires scratch. Source/ABI/pin/publication
costs remain real; this is not a measured Engine speedup. Current prepare checks duplicate Object IDs pairwise for packets <=64 and uses
full-ID uniqueness scratch above64, then performs catalog/reservation work.
Model packet sizes1/2/5/10/64/65/128/256 and charge scratch/refusal at that crossover. Do not invent Engine publication to bypass it.

A shared physical slab can reduce backing acquisitions for a batch. It must
have a counted owner holding the actual Loom Allocation, with all destination
slices disjoint from sealed bodies/padding. Fresh native Allocation transfer
cannot be repeated across captures. Count slab control/representation metadata,
shared references and whole-slab retained capacity. Scalar edits do not get an
imaginary batch amortization.

## Per-operation composition ledger

`H` is the actual ordered routing depth; `A` the number of ancestors whose
routing metadata changes; `P(x)` all admitted Parts of captured Object x;
`B(x)` its real initialized/copied bytes. These counts exclude no work: actual Borrowed/Scope boundary and one prepared-schema selection are charged once at the
declared request/cohort boundary. Supplied producer changes are counted
separately, then included in the single complete publication interval.

| Operation | Directory selections after prepared schema | Directory writes/captures/publication |
| --- | --- | --- |
| Stable-fact data edit after locating segment | none added | zero; actual producer data/delta/validity work remains in Engine |
| Exact top-table or compact-hash lookup | one selected chunk/group | none; owner/proof and requested output are consumed; full 64 B only for enumeration |
| Natural singleton/small region lookup | one root | none |
| Large ordered seek/exact-prefix lookup | H routing nodes; +one payload chunk only for the independent-chunk recipe | none; absent natural region/successor can additionally visit its presence tree |
| Range/continuation | startup path, then each newly entered node/chunk once | none; count every false candidate admitted by enclosing fences |
| Inline projection | none if authoritative material AND geometry are already qualified; else actual material/shape selection | none; committed extent/validity can change with identical pointers/geometry; real resource binding is additional |
| Extra projection | one selected StreamPack block/list, reused for its ordinals; paged vector costs actual depth | none; no repeated admission/length lookup per ordinal |
| Ordered C8 locator replacement | route to one payload chunk | copy512 B, capture one one-Part chunk; no routing/ancestor edit |
| F32/C16 compact candidate locator replacement | H routing nodes, no separate payload Object | copy1,024 B, capture one THREE-Part leaf, retain two sources; no ancestor edit |
| F128/C32 compact candidate locator replacement | its actual H routing nodes | copy2,048 B, capture one FIVE-Part leaf, retain four sources; no ancestor edit; compare saved routing depth |
| Embedded small/table locator replacement | one selected group/chunk | actual whole small payload/format; retained sources and all P(x), not merely changed fields |
| Hash locator replacement | one group | payload<=1,024 B; retain bounds/map; capture actual group Parts and retain the real mapping owner |
| Representation change | route + needed segment/definition admission | actual changed dirent/proof, optional extra list; no map/bounds repair unless those facts change |
| Bounds change | route to leaf; compare actual ancestor extrema | leaf metadata/tails plus A changed routing Objects; copy their actual formats, capture all P(x) |
| Insert/delete without balance | route plus real presence path if needed | membership/bounds/mapping + affected payload; empty chunks/Objects retire; changed name counter is included |
| Split/merge/redistribute | final affected run and required neighbours/parents | materialize final row streams once; copy rows that cannot retain whole chunks; capture every output, changed parent and counter, and publish real tombstones |
| Extra-only pointer replacement | selected local block | changed StreamPack Parts/Control; no dirent write when block/rank stays equal; include actual producer change |
| Extra rewrite, fixed-rank candidate | one block, sixteen original-suffix lists | actual final StreamPack construction once; fixed block/rank means no holder discovery or rank repair; count all real bytes |
| Schema append/extension, direct-name candidate | selected index header + actual definition record when not already qualified in this read | D actual changed64B record Owners plus retained descriptor material; no roster, header changes only for actual index-context change |
| Migration/port | source shape, target definition, touched directory path | actual changed locations/representation/extra associations, plus finished target and any schema changes in one coherent publication |
| Same-value/qualified no-op | no extra route if complete supplied evidence suffices | no capture/publication; comparing an unqualified K/E-sized value costs its actual bytes |
| Refusal/stale/abort | whatever was actually reached | abandoned drafts, physical uses and synchronous cleanup; no forced full drain or fictitious undo replay |
| Batch/bulk | each affected node/chunk once where placement permits | sum DISTINCT final changed Objects/Parts, one declared publication; repeated edits do not repeatedly copy a Part |
| Maintenance | actual history/physical-retirement visit | bounded foreground share plus whole-history CPU and retained capacity; no unpaid backlog |

Each copied Part needs a new disjoint writable destination; whether that means
one consuming Allocation or a slice of an actually retained slab is an explicit
physical treatment. Each capture also constructs a real Control/proof and
descriptor; each publication admits every finished Owner, performs the actual
catalog/history work and installs one cut. These are not nanosecond constants
that can be filled in from an unrelated kernel benchmark.

The choice between exact occupied bounds and stable enclosing fences also
needs an event-frequency ledger: directory edits per producer update, extra
candidate segments per query, compaction/port frequency and amortized bytes.
An enclosing policy only wins if its complete read/write mixture wins. Report
the no-directory ordinary path, bursts and cleanup independently as well as the
mixture; averaging must not conceal unacceptable burst tails.

## Conditional one- and five-chunk replacement pressure

For this independent-chunk source-level pressure cell, each existing directory chunk has one
Part, and each changed chunk accompanies one finished segment Owner whose
locator changed. All changes use one packet. C8 is the 512 B candidate; C16 is
the 1 KiB copy/metadata control. This counts work, not unmeasured nanoseconds,
and does not describe an ordinary data write with stable directory facts.

| Work | One chunk + segment | Five chunks + segments |
| --- | ---: | ---: |
| Directory bytes copied, C8 | 512 | 2,560 |
| Directory bytes copied, C16 control | 1,024 | 5,120 |
| Directory captures / admitted Parts | 1 / 1 | 5 / 5 |
| Total publication changes | 2 | 10 |
| Current preparation ID searches, existing tips | about 6 | about 30 |
| Proposed preparation ID resolutions | 2 | 10 |
| Current pairwise duplicate comparisons | 1 | 45 |
| Mutation workspace, 16 B per change | 32 B | 160 B |
| Current temporary history-handle array, plus allocation | 8 B | 40 B |
| Preserved history nodes, 16 B per old tip | 32 B | 160 B |
| Physical-directory pointer slots, 8 B per old tip | 16 B | 80 B |

The proposed preparation reuses workspace and eliminates the temporary handle
array; it does not eliminate old-version storage, admission or duplicate checks.
Entry, bucket, registry, history and debt capacity must actually be available.
Birth, deletion, tombstones and retained history can require more capacity.

The F32 co-located variant has the SAME one/five directory Object counts for
ordinary locator replacements, but captures 5/25 admitted Parts and retains 4/20
old sources. Its native descriptor is112B per leaf under current24B Control,
versus48B for a one-Part chunk. It copies the same 512/2,560 B, saves the extra
payload catalog selection, and changes structural packet size substantially.
That trade must be tested in complete operations, not resolved by looking at
capture fanout alone.

NEW compound leaves also compare grouped native capture: F32/C8 P5 requests
80B rather than112B with the current32B header. A later partial update can
use independent native sources, retaining four actual Part uses and possibly
the entire original grouped Allocation. F32/C16 is P3 (80B independent/64B
grouped); F128/C8 is P17 (304/176B). These are descriptor formulas, not total
memory or timing. Alignment/padding, physical granularity, real pins and source
work remain in the ledger. Cross placement/fanout at the same C before choosing.

Directory capture additionally owns one physical use and one interpretation
proof retain per Object. Its requested native descriptor bytes are
`sizeof(Representation<Control>) + 16` per one-Part Object; shared ownership uses
24 B instead of 16 B. The proposed ranged-native layout remains unqualified.
Pooling must preserve the same resource identity and each descriptor's real
lifetime. Already-finished segment Owners need no second capture, but their
admission, history, installation and eventual retirement still count.

Consuming native capture acquires one fresh Allocation per chunk. A batch slab
can amortize that acquisition while retaining actual per-cut ownership costs,
rotation and coarser retained capacity. The measured 56.12 ns ready finite
allocation/release cycle is neither allocation-only time nor a lower bound;
five such cycles are a warning about scale, not a valid component sum.

The roughly 381 ns small-write allocation therefore has a conditional route
through small captures, reusable descriptor/workspace stock and one-pass
Orbital admission. Current source and retained timings do not establish a fit.
Selection, private drafting, 2.5/5 KiB copying/editing, five captures, ten-change
prepare/install and amortized cleanup must ALL fit. Warm payload does not imply
warm catalogue entries, history or descriptor storage. Cold and pressure cases
cannot inherit this ready-case hypothesis.

## Complete graph write counts before choosing custody

For dense262,144 prefixes, logical16/F32 has512 low routing nodes,16 middle
nodes and one root. A locator edit changes the group payload even when bounds
stay equal. Its raw-child address changes, so the graph must rebuild its path.
This differs from the stable-name array treatment, which changes no ancestor.
These counts assume commands were coalesced BEFORE materialization:

| Offered changed segments | Changed group Parts | Changed routing nodes | Directory captures | Published changes, including Segments |
| --- | ---: | ---: | ---: | ---: |
|One|1|3|4|2|
|Five in distinct low/middle branches|5|11|16|6|
|64 consecutive prefixes, aligned|4|3|7|65|
|64 dispersed prefixes, worst distinct branches at this population|64|81|145|65|

The aligned B64 graph copies5,824 directory bytes plus4,096 Segment bytes.
The dispersed bound copies112,192 directory bytes plus4,096 Segment bytes;
its81 proofs establish up to2,576 child associations, including2,432 unchanged
children. Capturing the64 supplied Segments adds another64 captures in the
complete producer. They remain independently allocated. A packet with65
changes crosses current Orbital's small-packet uniqueness path. No scalar replay
or omitted construction is hidden behind the single root publication.

At350ns for the five-replacement stress,16 directory captures alone would need
an average below21.9ns, before routing, backing, proof retention, supplied
material and publication. Current measurements do not support that composition.
Keep it as the read-optimized control, not a claimed fit for that write target.
The next write comparison uses bounded stable-name payload arrays and full-ID
batch deduplication, and prices the extra read selections against graph path
construction. Ordinary stable-fact producer edits still have zero directory
captures in either design. This is a reason to change custody or batching, not
raise the target or substitute a different locator meaning.

A per-operation paper ledger records required calls/bytes and the maximum total
remaining budget. Assigning guessed nanoseconds to those calls cannot establish
a favorable sum. llvm-mca bounds the instruction loop; it does not price catalog,
allocation, history, cache/TLB misses or proof destruction. Measure the smallest
complete real-module operation before extending a candidate that depends on
those costs. A failure stays visible in the same table as successful reads.

## Paper feasibility and remaining uncertainty

The current paper candidates are F32/C16 and F128/C32 with the compact endpoint
layout in [Design](DESIGN.md#compact-candidate-byte-and-ownership-ledger). The
older separate-chunk/F32-C8/graph cells above are labeled comparisons, not costs
that can be silently combined with this candidate.

For one F32 locator replacement with unchanged bounds, membership and extra
list, the full directory work is:

1. Use the caller's original Scope/Core; qualify schema/before binding and the
   finished after material. Route the actual physical prefix once. Charge Scope
   enrollment at its declared request/cohort boundary and actual definition
   selections when not already qualified.
2. Compare the complete relevant descriptor/interpretation association. If equal,
   return an empty directory proposal; no allocation/capture is needed.
3. Acquire one disjoint 1,024 B native destination, copy that payload Part once and
   patch 64 B at its existing slot. Metadata and the other payload Part stay sealed.
4. Form three actual sources: fresh payload plus two retained native sources.
   Cached immutable geometry avoids a new Layout/Geometry vector per edit.
   Capture one three-Part directory Owner:32+3*16=80B descriptor, before PMR
   rounding. Retained sources acquire physical uses; they are not free borrows
   after capture. No ancestors, counter, bounds or extra block change.
5. Return one directory change. The caller joins its actual replacement Segment
   Owner, giving two changes in this witness, and performs one real prepare and
   install. The small-packet duplicate check performs one full-ID comparison;
   real catalog/reservation/history work remains.
6. Clear caller-held Owners, release scratch, and pay local/whole-history
   maintenance and retirement. The selected old Scope still protects late reads.

The following is an allocation of the 250 ns engineering ceiling, not a timing
prediction. Each line is a separate unresolved sub-budget. The tighter matched
relative allocation can reduce the whole ceiling further; this table never
overrides it.

| Complete directory replacement phase | Target allocation |
| --- | ---: |
| Scope/schema/before qualification and one route | 35 ns |
| Derive/compare actual after association | 20 ns |
| Actual backing acquisition | 60 ns |
| 1 KiB copy and final stores | 20 ns |
| Source retention and three-Part capture | 40 ns |
| Whole two-change prepare/install | 60 ns |
| Local cleanup and amortized maintenance | 15 ns |
| Total | 250 ns |

The same trace for F128/C32 copies 2 KiB and captures five Parts (112B descriptor,
four old sources); it must repay that extra work by lower read depth or better
structural amortization. It does not receive a larger ordinary-update ceiling.
An L1 whole-Part replacement, hash clone, seventh-pointer change or bounds edit
takes its own actual path; the trace above cannot price it.

Five unrelated locator replacements do not inherit five 250 ns allowances. The
350ns coupled pressure target needs actual batch reuse/backing/capture savings.
Five scalar copies/captures/installations cannot meet that target by construction.
Likewise, measured graph reads do not demonstrate stable-name read throughput,
and the old stable-name loss remains relevant until a complete matched witness
shows what changed.

The paper revisions remove identifiable unnecessary work:

| Rejected cost | Current candidate | Residual cost that remains |
| --- | --- | --- |
| Clone a layer/roster for one entry | Bounded leaf/hash group | actual bounded clone, capture and publication |
| Physical child pointer repair on every payload move | Stable child names | each read's real catalog selections |
| Decode/build all node rows for locator-only edits | Patch the selected payload Part directly | copied Part plus ALL admitted capture sources |
| F32/C8's five capture Parts | F32/C16's three Parts | twice the changed bytes; actual old backing retention |
| Full keys plus exact short abbreviations | Abbreviations plus only missing tails | full-tail refinement for K>8; format conversion/qualification |
| Scalar replay of structural batch intermediates | Final sorted streams and bottom-up normalization | all final outputs/neighbours/parents/counter/tombstones |
| Dynamic extra ranks and holder repair | Sixteen original-suffix StreamPack ranks | local final-image rewrite, potentially large bytes |
| Append-time schema roster/header churn | Direct named caller-supplied definition IDs | actual record Owners, scoped admission and descriptor lifetime |

The occupancy arithmetic was checked over every legal scalar sibling count for
F32 and F128. Minimum underflow potential release is 6/22 and split release 7/23;
the conservative global denominators are 5/21. Therefore the former 400 ns F32
repair goal did not justify 75 ns amortized work. 375 ns is the corrected F32 goal.
F128 at 1.5 us gives 71.4 ns before any work omitted from its boundary. A complete
capture/publication/retirement result must earn those numbers; a potential
argument alone establishes no CPU bound.

The measured full 64 B point consumer has substantial conditional room in some
cells. Dense whole-layer scan has almost none: G16's strongest retained direct
case leaves 0.413 ns/item below half its paired native peer, and the unmeasured
competent DUPFIXED peer may tighten it. A multi-row Segment can amortize directory
work over its actual records; the one-row stress case remains in the results.
It cannot be reclassified as acceptance or removed to manufacture headroom.

Latest whole-table Hashtable evidence in
[the memory-matched study](../hashtable-boost-adoption/MEMORY.md) strengthens the
reason to test complete caller execution: future-key prefetch can substantially
improve eligible independent cold work, while some hot mixed cases still favor
peers. That is not a composed directory result, a scalar point constant or a
reason to insert a Ring where dependencies/visibility do not permit overlap.

I cannot yet conclude that every mutable operation fits. The unclosed costs are
stable-name selection throughput, actual three/five-Part capture plus coupled
publication, five-replacement pressure, whole structural repair/retirement, and
large-list/schema/cold-history behavior. Current APIs can express the algorithms;
expressibility does not prove their cost. The present decision is to retain this
explicit paper candidate and its misses, not resume a broad implementation or
claim that another prototype is already certain to pass.
