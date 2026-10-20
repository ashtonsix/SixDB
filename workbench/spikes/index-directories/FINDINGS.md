# Directory and producer checkpoint, 2026-10-20

The current-root graph and bounded producer now have useful throughput signal.
They do not establish complete Engine acceptance. The immediate priority is
removing measured allocation/metadata waste while retaining these outputs,
snapshots, atomic publications and throughput. Ordered mutation, balancing,
extras/schema and general segment/record codecs remain implementation work.

## Actual composition and the read result

One actual Orbital Scope selects the root; its physical interpretation proof
owns the immutable directory children. Reads follow qualified byte pointers
without a catalog selection per child. The SAME live Scope protects separately
versioned material. No previous-root ownership chain, Engine MVCC, undo queue or
authoritative shadow map is involved. Writes to the graph pay real path copies,
child-owner retention, captures and publication; read throughput does not price
those writes. Source bytes and that trade-off are in [Design](DESIGN.md).

Pinned Clang21.1.8, ARM/OrbStack, guest CPU8. Each read consumes all eight actual
material words into XOR, using the same ready flat-u32 trace. Scope/root/Part
selection and close occur inside each64-request cohort; native LMDB renew/reset
and cursor renewal are included. Calico uses real prepared runtime calls on
unchanging data, without a retained-old-cut API. Inputs, full-byte/locator
oracles, per-input batch association and all timed signatures were independently
audited. These are reciprocal-throughput observations, not request latency.

| Dense segment count | Scalar graph ns/item | Staged32 graph | Native LMDB u32 get / cursor | Best tested Calico full8 visitor |
| ---: | ---: | ---: | ---: | ---: |
|8,192|4.750|4.741|147.965 /150.764|85.978|
|262,144|38.043|29.338|391.095 /374.649|143.905|

The staged graph adds about1.30x throughput over scalar at262K;8K ties. That
control combines staging, fewer call boundaries and amortized checks. The much
larger recovery over the earlier repeated-catalog caller also changes physical
topology and useful-consumer/input closure; it is not an isolated MLP gain.
An earlier262K stable-name dependent projected read lost784.9ns to native540.5ns;
its moving-Part update was7.91us against6.82us. Preserve those negative probes,
without presenting unlike operations as a clean before/after timing pair.

Against the best tested full8 Calico control, the graph gives18.1x/4.91x useful
throughput. The2x complete-request planning envelopes are42.989/71.952ns/item;
subtracting the present graph consumer leaves38.249/42.615ns/item. This is
conditional budget arithmetic, not measured time for missing Engine work or
permission to add costs from unrelated trials. The initial25% directory/binding
allocations are21.495/35.976ns. Actual directory attribution must still separate
the material consumer, and the complete composed request must be measured.
No p99, cold/history, durable, range, write or native Zen5 win follows here.

## Original memory defect and the measured correction

Primary warmed backend-only UNTRIMMED RSS/PSS, MiB, with external live input and
trace buffers released. Released allocator capacity remains counted:

| Dense segment count | Engine | Calico recordwise | LMDB native-u32 |
| ---: | ---: | ---: | ---: |
|8,192|9.215 /7.031|8.793 /7.424|6.867 /5.357|
|262,144|176.613 /174.421|24.758 /23.386|46.080 /44.575|

Engine's262K lifetime high-water mark was211.83MiB; its steady RSS is about7.1x
Calico's. Roughly4MiB of freed trace storage remains allocator residue. LMDB's
load-and-serve environment retains about20MiB of live load-era heap plus20MiB
resident DB file; close/reopen minimum footprint was not measured. Do not call
that46MiB its irreducible footprint or compare file size with Engine RSS.

The source/counter audit reconciles132.32MiB of native mapped payload:
16MiB actual material,17.29MiB directory/groups/graph,51.03MiB class rounding,
48MiB mapped-page underfill. Alignment64 forces the old palette choice; the
interleaved16-material/one-directory allocation cadence underfills class pages.
The32-lane512-byte stack bank is negligible. Native reserved VA4.90GB is sparse
reservation, not resident RAM; page-table cost remains separately reported.

Requested live PMR metadata is27.22MiB INSIDE43.00MiB pool backing, not additional
to it:14.90MiB captured headers/address/ownership slots,12.13MiB catalog/hash
structures and0.19MiB graph proofs. Another15.78MiB is pool rounding/chunks/spare
capacity. Pool mapping, backing, live requests and RSS are different ledgers.
The one-record-per-segment fixture preserves actual segment cardinality;
reductions must not silently merge segment Objects or remove their association.

Orbital's eligible-palette/page-placement fix (`57a80647`) reduces the SAME
caller's262K native mapping from132.324 to36.324MiB and RSS to79.906MiB, with
64-byte alignment and all independent Segment Allocations preserved. Removing
the resource pointer from each Control (32->24B) and placing it in shared Format
(88->96B), then bounding the metadata pool's chunk size, reduces live requested
metadata to25.091MiB and its backing to25.425MiB. Removing the duplicate64B group
header avoids a native size-class cliff. These are different source changes;
the12.63MiB PMR backing reduction is not a12.63MiB RSS reduction.

The next frozen grain1 source is
`acad3cc8534a3c004aa3f2d139f56ff37342c6ea20464f3a690e0a8535b2c046`.
The physical directory grain comparison keeps logical16/F32, one Part per group,
independent Segment Allocations and the useful full64B consumer fixed:

| Physical dirents/allocation | Initial directory Allocations | Fresh262K RSS MiB | Aged/released/trimmed RSS MiB | Aged mapped MiB | Staged point ns/item |
| ---: | ---: | ---: | ---: | ---: | ---: |
|16|16,384|69.738|77.184|38.863|21.687|
|64|4,096|70.996|78.410|38.863|25.000|
|256|1,024|69.871|89.270|50.863|27.260|

Fresh native mapping is32.324MiB in all three:16MiB Segment bytes,16MiB directory
bytes and0.324MiB routing. PMR request/backing is25.091/25.425MiB in all three.
Aging performs15,360 actual moving-Part updates: one in15 of every16 logical
groups, retaining an old Scope whose first discovery follows publication and
drain. After closing it and actual trimming, unchanged slices still pin the
G256 native backings, producing the exact12MiB excess in this witness. No native
Part slicing implies independent reclamation of the surrounding Allocation.

The read screen has12 isolated processes,192 rows and8 samples/arm/cell, with
opposite grain orders. At262K scalar G16/G64/G256 is36.510/31.686/32.644ns/item;
staged G16 leads in both outer orders. Paired BE4 LMDB get/cursor controls range
from338.2..373.0/346.5..372.1ns/item across grains. No fresh Calico arm was run.
The [bounded counterfactual](evidence/memory-grains-20261020/cause/handoff.json)
uses the exact SAME262,144 Segment byte addresses in three actual directory
graphs/stores, with opposite child-construction orders and the unchanged staged
reader. All pointers, full64 material bytes,56 metadata bytes and actual selected
Part associations pass. SharedD16/D256 measure24.911/22.771ns in one order and
25.386/22.428 in the other: the ranking reverses. A larger directory allocation
is not intrinsically slower in this reader. This diagnostic keeps three graphs
and their control stocks live, so it does not uniquely identify Segment placement
as the previous cause or select a production layout. Setup/placement/state
interactions affect the ordering; the cache/TLB mechanism is UNKNOWN. Guest PMU
events are unavailable, and virtual line/page censuses are not hardware counters.

The subsequent address census and controlled spacing experiment establish a
narrower cause. In the original G64/G256 construction, almost every adjacent
256-Segment material group is separated by exactly 640 pages. With G256 fixed,
four serial ABBA children compare 640 with 641 pages while retaining exactly
the same absolute directory, routing, Core, root, trace and selected-realization
addresses. Every material Allocation remains independent and 64 bytes; mapped
bytes, capacity and reservation bytes are equal. The Scope64 consumer measures
25.289 versus 23.327 ns/item, and the stationary consumer 24.029 versus 22.221,
with 12 samples per spacing and caller. Both child orders favour 641 pages.
Material address spacing therefore affects this reader by about 8% in this
control. It does not explain the complete historical grain gap or distinguish
cache sets, TLBs, banks, physical-page history or page walks. No allocator policy
is selected. The complete census, exact-address audits, runnable binary/shim and
48 timing rows are in the
[Architecture causal evidence](evidence/memory-grains-20261020/address-census-artifact.json).

These are physical coallocation results, not a universal optimum for16-entry
logical leaves or Parts. Logical64/256 whole-Part alternatives, scalar/batch
write cost and the optional actual-Hold custody treatment remain to be priced.
The current memory/source/raw receipts are recoverable through
[the Architecture evidence reference](evidence/memory-grains-20261020/artifact.json).
The existing implementation's four checks pass in grain1; later live reader,
L1-table and scan-reference edits are separate sources and carry no timing claim.

## Full-cycle producer and the stronger eager control

The finite DRIVER has64 actual rows, eight u64 fields, all five summary layers,
SUM128 and field3 assignments. Plain append writes24-byte absolute afterimages;
indexed append also copies a160-byte footer; packed eager clones one576-byte
correction/SUM image. Ordinary updates change zero directory facts. Partial/full
folds publish actual moved-Part locators with material. Failed suffixes rotate
from the selected committed prefix, never overwrite sealed bytes.

Frozen sources buffered1/2/3 retain the iterations. Eliminating unnecessary
144-owner setup/destruction, full-row scans and repeated advice admission moved
hotB1 plain from412.6ns to247.0ns in the first screen. The follow-up pairs plain
against frozen2 again:243.39 versus243.32ns, a tie. This does not attribute the
earlier gain to one instruction. Vector copying reduced indexed append overhead;
the complete source revision, not an isolated copy benchmark, is measured.

Final foreground plus declared maintenance, medians in ns/ATOMIC REQUEST:

| Trace | Plain | Indexed | Packed eager | Native get+put | Native cursor CURRENT |
| --- | ---: | ---: | ---: | ---: | ---: |
|B1, hot2, no query|243.4|253.9|318.9|3070.8|3015.0|
|B1, hot64, query/fold/refusal pressure|851.3|536.4|561.2|3990.2|3830.3|
|B64, hot2, repeated-key coalescing|310.4|380.6|385.5|3263.3|3120.4|
|B64, hot64, query/fold/refusal pressure|3888.4|2183.3|2033.0|11521.8|7617.9|

In the last row, whole-history ns/command are62.37/35.80/33.23 versus180.89/121.17.
Packed eager beats indexed append there. Its predecessor used separate small
row/SUM Objects and cost25.11us/request; replacing dozens of captures with one
actual image reduced that to2.03us without weakening atomicity. That older loss
does not establish an eager lower bound. Plain remains best in the hot cases.
Choose from whole fixed-policy traces; do not splice each policy's fastest piece.

The indexedB1-pressure wall median536.4ns differs from owner/process about506.5ns;
all samples, including506..557ns wall spread, are retained. It is not clean
evidence of the same percentage instruction reduction. Other medians have CPU
close to wall; CPU includes stalls. No sampled request tails were collected.

There are262,144 supplied commands per run, either one or64 per publication;
native batches may coalesce repeated rows too. Each arm occupies every position
once across independent processes,5repetitions(6with the paired baseline).
Across the two screens,222processes/444phase rows passed full useful-output and
final64-row oracles; source/binary hashes remain fixed. Full-history clocks begin
before backend setup and include fixture/catalog destruction and actual Worker
shutdown. They also include final oracle/reporting overhead, explicitly labeled.
External command/oracle preparation is outside both arms' full-history clocks.

Pressure cases have8 full-row reads/request; SUM every64B1 requests or16B64;
partial+full fold every4096B1 or256B64; failed prepare+retry every8191B1 or511B64.
Native abort/retry is charged but has a different physical failure cause.
LMDB uses actual64B values, BE4 keys and CPU-only NOSYNC/NOMETASYNC. It is not
forced to reproduce Engine compaction. SUM uses a renewed ordinary cursor walk.
The stronger numeric-key write alternative remains separately qualified,
unmeasured here. Matching Calico mixed updates/SUM128 or atomicB64 is still open;
the earlier8K scalar Calico assignment658ns is not substituted for a matched64-row
trace. These are meaningful producer controls, not full Engine acceptance.

## Actual ordered and masked range scan

The scan uses one saved-frame seek and ordered traversal of the real graph,
under one Scope for the complete scan. It consumes selected64B material into
`sum_mod64(xor8(row))`. Ready external masks use ORIGINAL physical-prefix
coordinates and seeded permuted bits at0/1/8/32/64 bits per64; predicate generation
is outside this control. Rank uses original occupancy. A sample repeats complete
scans over at least1M candidate prefixes, including Scope/root/traversal/close
or native LMDB transaction/cursor renewal, SET_RANGE/NEXT and reset EACH time.

Frozen scan-driver1 is
`e96f14ad818c72d88e81d60c79d18d62ae69789756e8ceba2ab0811094525511`.
Twelve isolated processes and opposite population/grain orders give576 samples,
four per arm/cell. Counts and checksums agree across peers/grains/processes;
the driver hash is unchanged. The native peer uses actual u32 INTEGERKEY64B
values. Logical16/F32 and Part grain are fixed across allocation grains.

Unmasked medians, ns/visited segment with all64B consumed:

| Segments/range | Engine G16 | G64 | G256 | Paired native LMDB G16 | Best tested Calico prepared scan |
| --- | ---: | ---: | ---: | ---: | ---: |
|8K quarter|2.176|2.184|2.118|8.174|32.752|
|8K whole|1.983|2.110|2.001|8.307|32.054|
|262K quarter|2.342|3.316|2.778|8.331|31.547|
|262K whole|4.824|7.030|5.440|8.839|31.696|

LMDB is the strongest tested eligible scan peer. The262K whole G16 result is
1.83x faster; it misses the provisional2x complete-Engine objective before the
remaining Engine work. G64/G256 whole-layer results miss too. The smaller
working-set result cannot establish headroom for this case. No cache/TLB cause
is established by that change. Retain the miss while testing one bounded
selected-material lookahead control against the SAME graph/Segment bytes.

The masked native baseline walks the complete cursor range and tests the mask
before consuming64B. It is not proven best for sparse/empty masks. The0% case
uses a full-span zero bitset. Do not use its large ratio as masked acceptance:
price an inside-clock zero-bitset guard in both arms and an actual native
mask-run seek control. Report complete query time and actual visited/selected
counts; ns/selected is undefined at zero. Consumer bytes are not physical traffic
or proof that a returned native value's page was untouched.

Calico uses real prepared eight-field numeric visitors and complete execute/
status lifetimes, with at least1M rows/sample. Recordwise ZERO beats Fieldwise
exact_min in all four eligible unmasked cells; both policies remain in the raw
evidence. Its public external-mask preprojection is unavailable; a postdecoded
filter is not substituted. Retained-old-cut parity is not inferred.

The five native checks pass, including5040 native/reference graph scan cases,
40005 selected items, independent ordered association/counter oracles, exact-end
guards, compressed roots, sparse/max prefixes and never-read old Scopes first
used after successful publication+drain+trim. Native ARM and AVX2 under existing
QEMU each pass10083 ABI-guard calls. AVX-512 is assembled/linked/static only.
The finite producer checks corrected masked ranges through plain/indexed/eager
policies, faults, folds and old cuts. Those are not raw-base timing claims.

The first refinement uses a separate ARM entry: full original membership means
rank equals suffix, removing eight rank instructions while keeping a generic
sparse fallback. The actual unchanged fixture, material and complete Scope
caller compare baseline/dense/native in all six arm permutations, with a checked
self-warm complete scan before each sample. Four processes give72 rows and12
samples/arm/cell. Before timing, actual large-fixture counters agree too.

|262K whole-layer control, ns/row|G16|G64|
|---|---:|---:|
|Baseline, same new caller|4.241|6.557|
|Dense original-rank shortcut|4.023|6.225|
|Paired native cursor|8.873|9.098|

The ratio of pooled medians improves by about5%; paired median gains are
3.29%/4.24%, with dense winning9/12 pairs at each grain. One G64 process
essentially ties. Do not attribute the larger difference from
the first screen to these eight instructions: warmup/order, executable and
placement histories also changed. G16's conditional half-peer margin is0.413ns
per row; G64 still exceeds it by1.677ns. Neither establishes complete Engine fit.
The one bounded next control stages selected material pointers, with hint and
no-hint variants; its additional bank traffic and instructions must earn their
cost. A possible grouped DUPFIXED bulk LMDB policy is source-reviewed only;
its behavior/performance are not substituted into these measured comparisons.

That bounded lookahead control is now complete and loses. Its 64-pointer bank
adds an 8-byte store and load per selected segment and a 1,040-byte stack frame;
hint/no-hint entries differ only in their two PRFM sites.
Both bank variants retain generic original-mask rank work; comparison with
dense therefore includes reinstated rank instructions as well as staging.
Hint versus no-hint is the closer matched intervention. Four rotations of
`{0,1,3,2}` balance all positions and all twelve directed adjacent-arm pairs,
with a complete checked self-warm scan before every sample. Four processes
produce64 rows, eight per arm/grain cell, on the same fixture and Segment bytes:

|262K whole-layer control, ns/segment|G16|G64|
|---|---:|---:|
|Strongest direct dense entry|4.498|6.775|
|Pointer bank, no hint|4.673|8.344|
|Pointer bank, hint|5.623|7.975|
|Paired native cursor|9.160|10.004|

Paired median owner-CPU overheads versus direct are +2.73%/+18.84% for no-hint
and +24.38%/+13.06% for hint. Wall outliers do not reverse the negative result;
all samples remain retained. A peer disclosed a brief source capture after its
idle acknowledgment; its completion timestamp is20.8 seconds before Engine's
pre-run process snapshot and runner start, so the reported activity did not
overlap these children. This screen still supplies no acceptance or precision
percentage claim. There is no further tile-width or hint
sweep. Keep the strongest direct entry and address the unresolved layout,
stronger native peer and complete mutation costs.

The integrated source passes six primary checks. The expanded scan check has
9,600 ranges/masks/snapshot cases and105,540 ordered selected items against the
independent full64-byte oracle, exercising all four ARM entries, strict11/16
membership, tile boundaries63/64/65/127/128/129 and first old reads after
publication+drain+trim. Loom's separate tile rollover and ABI checks remain
additional evidence, not a replacement for these actual composition checks.

## Verification and remaining scope

Six current native checks pass: directory bytes/slot versus rank, physical
graph/late history/export/faults, the earlier P/Q/P composition, and current
plain/indexed/packed-eager all-field/all-layer producers, and ordered/masked
graph ranges, and actual native mask-run cursors against independent per-bit
and ordered full64-byte oracles. Tests cover repeated
atomic batches, no-op omission, partial/full folds, actual capture/prepare/global
stale-basis refusal, prefix rotation, SUM overflow, late first old reads after
drain, exact-end0/8..512-byte copies and physical shutdown. Broader concurrency,
general NULL/aggregate laws, arbitrary segment codecs and durable publication
are not implied. The current model reports only literal backend sequences;
copy/append cycle estimates exclude cache/TLB/branch/callee/allocator work.

The rest of the spike remains owed: memory solution and matched regression,
ordered mutation/balancing/extras/schema, varied realistic segment/row counts,
placement/duplicate pressure and complete Engine comparisons. Read-only speed
and a finite producer do not close those requirements.
