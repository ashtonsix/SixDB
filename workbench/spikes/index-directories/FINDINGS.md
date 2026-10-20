# Index directory findings

Measured prototype, 20 October 2026. This is an implementation experiment for
Ashton's supplied index. No Engine interface, physical format or placement
policy has been promoted. The [plan](README.md) and [prior art](PRIOR-ART.md)
give the conceptual context and mechanisms under investigation.
The directory owns segment dirents and Part locators. Record/aggregate models
in these drivers generate input versions and provide correctness oracles.
Earlier tables labelled `rows` count fixture records; their complete edit
timings include producer and segment-body work. They must not be read as
isolated directory latency or as the proposed Engine end-to-end driver. The
[maintenance panel](MAINTENANCE.md) reports actual segment populations and
isolates directory/overflow maintenance from those producer costs.

## The first fixture update results fail the performance goal

Hundreds of microseconds for a small value update are far behind the historical
Calico and LMDB scale. They expose prototype work to remove, rather than a
reasonable cost of this directory concept. Historical native LMDB point-update
p50s were 1.52, 2.08 and 3.20 microseconds at 8K, 256K and 4M records in the
[retained allocation-consumer comparison](../../benchmarks/orbital/evidence/selected-allocation-20261007/comparison/cells.csv).
Calico's [retained YCSB campaign](../../../../calico/workbench/benchmarks/ycsb/README.md)
reported 880K operations/s for the 50/50 read/update workload, about 1.14
microseconds amortized per mixed operation. That is not a point-update latency.
The hardware, operations, summary maintenance and measurement statistics differ;
these are scale checks, not matched speedup claims.

The first native Zen 5 composed fixture took 190 microseconds/update for full
copy and 227 for selective Parts on 4,096 records, J=0. Merely sharing unchanged
Parts lost despite reducing bytes. The [first native evidence](evidence/first-zen5/)
keeps that losing result and the source/build context. Its actual dependent
65,536-element numeric lookup also lost to `std::lower_bound`: ordinary
Boundlist 56.9 ns versus std 33.8 ns; opt-in numeric binding was 34.2 ns.
Ikea reviewed the integration and found no caller error. The storage control
consumes the candidate payload; logical gaps are routing candidates, not a full
record-absence verdict. These numbers do not establish complete Engine cost.

## What the prototype actually does

Ten research drivers use one implementation where practical:

| Driver | Boundary |
| --- | --- |
| `baseline` | Actual Ikea hash/Boundlist and ordinary-array controls through pointed payload consumption. |
| `composed` | All five physical-prefix layers, actual segment/directory/schema Objects, Allocation-backed Parts, lookup through summary payload, and coherent publication. |
| `placement` | Ordered labels and duplicate-run gaps through all resulting segment, summary and directory changes. |
| `pressure` | Sparse directory growth, coarse/paged/branched layouts, summary and table copy units, overflow, representation history and update phase attribution. |
| `staged` | Serial, manual lookahead and actual Loom Ring around deferred summary consumption, including active/overflow retained readers. |
| `keys` | Generic full-byte Boundlist versus ordinary search, including long common prefixes, binary zeros, duplicates and virtual large directories. |
| `locators` | Raw addresses versus stable memory-reference cells, through actual Part consumption and value publication, with retained readers and escaping Parts. |
| `schema` | Definition lookup across a historical tail; selected segment migration with codec, chunk and auxiliary-Part changes; native-source conversion versus model reconstruction. |
| `failure` | Reversible author edits through construction, refusal, abort and retry; normal/retry costs and shared versus fresh Pool state. |
| `maintenance` | Directory-only descriptor/page/overflow costs through supplied segment rewrites, birth/death and Part-recipe changes; stable split/merge versus complete repartition. |

Build with `-DSIXDB_SPIKES=index-directories`. On Linux, for a captured checked
run with CPU affinity and median/min/max summaries:

```sh
python3 workbench/spikes/index-directories/run.py --stage all \
  --operations 16384 --repetitions 5 --writes 32 --march znver5 --tune zen5
```

From the macOS workspace prefix Linux commands with `orb -m ubuntu`. Each
executable also has a `--check` mode. `run.py` captures the exact source,
compiler/configuration, hardware, commands, raw CSVs and executable in ignored
experiment storage. Both vectorizers remain enabled in all matched arms.

Queries supply the layer. Directory pages and their optional internal branches
are storage within that same layer. They introduce no order across index layers
and do not change the prefix identifying a segment. Exact inclusive occupied
maxima preserve repeated bounds and first-duplicate routing; partial summary
overlap still requires evaluation/descent by a future query operator.

The 64-byte prototype dirent contains six 64-bit locators, representation ID
(32 bits), flags (16), overflow rank (16), overflow block (32), and a revision
cross-check (32). This revision is not Orbital's snapshot cut. Actual MVCC,
Control and geometry metadata live outside those bytes and must be charged.
Each segment has one representation; a schema can admit many definitions with
different allowed layers. Two real summary encodings test interpretation, with
schema histories extending from three to 100,000 definitions.

The executable uses one nullable bounded integer field. Fixture ordinals carry
full logical ordering, with stored key widths 8/64/256. Only the separate byte
search control is generic over arbitrary key bytes. This does not establish a
generic-key Engine codec, SQL aggregate arithmetic or semantics for every
optional structure.

Extra segment Part addresses use actual StreamPack blocks of at most 256 ranks,
with a wider per-layer block number. Directory population has no 256-rank or
65,536-segment cap. The byte-key control exercises a virtual 2^32 global count
with local partitions, without trying to allocate that population. A real full
65,536-position segment tests the count and final local position separately.

## Update attribution and experiments

The original selective path still rebuilt broad images. Subsequent source cuts
removed that work for value-only updates, retained representation identity on
those updates, cached author-side descriptors and rebuilt only affected
directory pages. Readers continue to use published Objects, not the editor's
cache. Author caches and mutable model operands are additional memory, not a
replacement directory or a production identity rule.

Local aarch64 VM attribution on 4,096 sparse records put an earlier flat update
at 576 microseconds, including 383 in directory work and 170 in segment-image
construction. Installation was around 0.1 microseconds. Paged directories and
damage-aware image construction removed much of that work. These successive
source cuts are exploratory diagnostics; they are not single-variable speedup
experiments against the first native executable.

The latest local layout comparison retains flat, 16/64-position directory
pages, occupied-rank summary chunks of 64/256 entries, and a 32-way directory
branch control with compact/chunked first-layer tables. On 4,096 sparse records,
copying fell from about 422 KB to 11 KB per value replacement. The small-unit
candidate made no native Part request over 3 KiB on this trace; Orbital currently
routes larger requests through Windows. That did not establish a latency win:
more Parts add directory scan/retain, alias checking and overflow work. In the
local three-trial diagnostic the 256-entry summary chunks were about 28
microseconds/update while the all-small-unit candidate was about 46, with wide
variation. The [native layout sweep](evidence/layout-zen5/measurements/pressure-summary.csv)
confirmed the tradeoff: 20.0 microseconds for 256-entry chunks versus 26.9 for
the all-small-unit candidate. Preserve this losing case.

An Orbital caller review then identified redundant scratch allocations, clearing
before complete overwrite and dummy allocations for empty Parts. The helper now
reuses synchronous writer scratch, reconstructs borrowed source certificates
from each live basis, supplies fully initialized bodies without first clearing
them, and borrows immutable geometry when unchanged. The cleaned-up native
Zen 5 run measured 22.9 microseconds for the 256-entry variant. It establishes
no elapsed-time improvement over the prior 20.0-microsecond result. Bare
`store.capture` is now timed separately: all Object captures combined took
about 2.9 microseconds/update, compared with the segment helper envelope's 6.0
and directory phase's 8.2. The helper envelope includes allocation, comparison,
copying, temporary construction and cleanup. It is not an Orbital call time.
Final Image-to-native copies omit staging copies, equality reads and allocator
work; `copied_bytes` is not memory traffic.

The [final native evidence](evidence/README.md) uses the same captured code on
Zen 5, Granite Rapids and Neoverse V2, with each target's own ISA/tuning flags.
Five independent trials, 4,096 sparse records, J=0:

| Layout | Zen 5 update µs | Granite Rapids update µs | Neoverse V2 update µs |
| --- | ---: | ---: | ---: |
| Flat directory, monolithic summaries | 113.0 | 153.4 | 123.0 |
| 16-position pages, monolithic summaries | 42.8 | 55.6 | 58.1 |
| 64-position pages, monolithic summaries | 47.5 | 63.2 | 61.6 |
| 64-position pages, 256-entry summary chunks | 22.9 | 26.5 | 31.3 |
| 64-position pages, 64-entry summary chunks | 23.8 | 26.7 | 31.4 |
| 32-way branches, 256-entry summary chunks, compact/chunked tables | 24.4 | 27.1 | 32.8 |
| 32-way branches, 64-entry summary chunks, compact/chunked tables | 31.2 | 33.4 | 42.6 |

These are complete fixture update averages, not directory-only time. The flat
read control remains useful: on Zen 5 its layer-4 lookup through payload was
228 ns, versus 277 ns for pages/256-entry chunks and 354 ns for the corresponding
branch control. On Neoverse V2 the same three values were 332/432/503 ns.
More locality in mutation introduces read indirection; no universal layout is
selected from this small working set.

Measurements include the mutable model, Part capture and complete publication,
with an old reader retained across each edit burst. Pressure uses 16 writes,
composed 64, and placement a configurable burst. Pressure's writes target the
first sixteen rows; composed writes use a stride through the population, and
placement supplies the skewed structural histories. These are closed-loop averages
per request, then medians over independent trial arms; they are not request
p50/p99 samples or long-run service rates. Counters count requested/copied/shared
bytes and logical requests, not observed cache traffic or resident RAM.

## Placement consequences

Simple shifts, bounded hierarchical redistribution and repeated midpoint
padding preserve full order and the fixed J-prefix capacity. Unordered
duplicate ties allow use of gaps across the equal run and movement of a boundary
duplicate to free an intervening position. Small-domain exhaustion checks do
not borrow capacity from another shared prefix.

One local 32-insert interior trace at 4,096 records moved 72,568 labels under
shifting versus 4,096 under redistribution; complete update averages were 2.89
ms versus 580 microseconds. Both are far too slow. Midpoint append padding was
faster immediately than the packed shift control (60 versus 277 microseconds)
but produced 85 segments instead of five. More segments do not alone prove a
loss: subsequent reads, maintained summaries and future insertions must decide
the tradeoff. No fill threshold or amortized bound is selected by this fixture.
The mutable vector/map editor is a correctness and change generator, not an
optimized payload owner. Rank-changing insertions still rebuild segment images;
value-only copy units do not solve structural insertion economics.

## Integration and remaining evidence

One Orbital Store/Core publishes coupled segment, directory, schema and
overflow changes. A stationary Scope protects late-discovered Parts and
interpretation. Checks retain readers through edits, disappearance/recreation,
overflow remaps and schema extension. An independent oracle derives all five
layers from original physical records, checks counts/extrema/nulls/sums and
logical order, and permits arbitrary order within duplicate runs. Dense L1
population exercises compact table ranks and row-copy chunks; parent directory
locators are checked as well as leaf dirents.

Loom checks actual Ring completion/discard, active and overflow slots, bounded
spin and schema publication while frames retain old reads. Performance uses
one fixed cache hint in comparable arms. There is no resource Task wait or
remote I/O in this fixture, and no claim that a Ring always wins.

Initial preparation/refusal/abort checks exercised the Store boundary only.
The [failure experiment](FAILURES.md) now adds fallible editor paths and an
allocation-free undo log for author state, including first population, edits,
removal, schema extension and migration. It tests actual metadata/journal
allocation failures, accepted-packet abort, actual stale-basis refusal and
successful retries with retained readers. The process-local address format
remains a prototype: a changed-address
semantic rebuild tests relocation but does not reopen portable bytes. Durable
installation/recovery and mapped/remote locator resolution remain Orbital
integration dependencies. A held selected Part currently retains the geometry
proof; its minimal escape metadata is another measurable caller choice.

The earlier [aggregate-maintenance conclusions](../aggregate-maintenance/CONCLUSIONS.md)
also apply to author work. Direct old/new field contributions now leave count
and logical membership alone. Together with skipping identical descriptor-cache
saves, this reduces value-replacement inverse records from about 2.8–2.9 KB to
494–508 bytes in the 4,096-row cases. The matched generic/direct controls retain
the same five published segment versions and native copied bytes. Total update
cost remains in tens of microseconds, with broad structural edits still much
slower. [The owned report](FAILURES.md#measured-result) keeps normal/retry timing,
memory exclusions and overlapping timing ranges. Exact extrema still rely on
full frequency maps; buffering has not been qualified with Orbital snapshots.
These are fixture observations. Aggregate-policy implementation remains outside
the directory spike; directory-native timing is reported separately below.

The failure comparison also finds allocation-state pressure beyond logical
fixture size. Fresh Index instances sharing one Pool show two capture-cost
regimes despite identical native bytes and Object counts. The shared-Pool panel
is retained; the follow-up uses a fresh Worker/Pool/Arena per fixture outside
timing. Earlier panels' allocation histories remain part of their context.
This sensitivity needs aged/churned backend histories in a future end-to-end
comparison, alongside matched-state component controls.

The [schema comparison](SCHEMA.md) adds explicit selected-segment migration
between codecs, summary chunks and optional-Part counts. It checks unchanged
unrelated segments and retained historical interpretations. On Zen 5 a compact
codec migration falls from 18.28 to 5.66 µs by transcoding selected native
summaries and borrowing unchanged columns; native copied bytes are identical.
At 100,000 definitions, protected hot-ID lookup is 3.62 ns, but bulk full-copy
schema extension costs 875 µs and stores 2.4 MB of native definition bytes.
The owned report retains all targets and losing controls. These are segment
conversion costs, not record-update or end-to-end peer measurements.

At the packed-layout source cut, all six then-existing drivers passed native
Zen 5, Granite Rapids and Neoverse V2, and local ASan/UBSan. The staged
uniform-read case at 32,768 records
illustrates why to retain platform differences: the Ring was slightly slower
than serial on Zen 5 and Neoverse V2, but about 8% faster on Granite Rapids.
It is a small in-memory hint/control experiment, not evidence of I/O overlap.
The complete Part scan is measurable but does not dominate the best complete
update; that capture evidence does not justify an Orbital API change. A production writer
still needs bounded payload edits, efficient aggregate operands and directory
maintenance, plus the failure/recovery dependencies above. A prototype that
publishes correct results in tens or hundreds of microseconds has not met the
ordinary-update ambition in the Engine comparison proposal.

## Implementation direction

Use exact occupied maxima with repeated bounds as the first correct boundary
encoding. This handles duplicate runs without inventing uniqueness for logical
keys. Preserve the generic byte-key comparison requirement; the ordinal fixture
does not choose an abbreviation scheme for Engine.

Continue with bounded directory edits and bounded payload edits as separate
problems. Pages plus 256-entry summary chunks are the strongest measured update
control here, but still fail the absolute target. Copying an entire per-layer
directory on an interior edit is a rejected implementation strategy. Small
native requests alone are not a sufficient selection rule: the all-small-unit
branch layout loses on all three machines. Keep compact top tables and chunked
table rows available for dense/sparse-prefix work; this sweep does not isolate
their benefit from branch depth and summary grain.

The next payload work needs a representation that can edit positions, bounds,
keys and fields without reconstructing an occupied-rank segment image on each
insertion. Evaluate that together with gap placement: merely reducing relabels
does not reduce packed payload movement. Keep the simple shift and midpoint
controls until sustained churn shows the read/write/space tradeoff. The current
fixture provides a repeatable failing baseline and correctness oracle, not a
production-ready payload owner or a selected rebalance policy.

Do not optimize a reported envelope as though it were a foundation call.
Continue measuring allocation, image/staging work, complete capture and bare
capture separately. The absolute comparison with LMDB remains a future complete
Engine request comparison; none of these fixtures establishes parity.

## Many shared prefixes expose an Object-resolution defect

The original J=2 population had sixteen tenants. A new pressure fixture assigns
one populated two-byte prefix per record: physical `i<<32`, logical `i<<48`.
This still follows the supplied model and full logical order. At 4,096 prefixes,
the [native baseline](evidence/prefix-zen5/measurements/pressure-summary.csv)
took 8.18 microseconds for a layer-2 request with a whole hash and 10.69 for
paged hashes; complete strided value updates took 554/496 microseconds. The
pressure test was essential: the earlier small-prefix result hid this behavior.

Source attribution found that Orbital's TipDirectory hash multiplied the low
ObjectId word, XORed the high word, then masked low table bits. Natural layer-4
segment IDs here have low word `(5<<56)|(i<<16)`, so their varying bits disappear
at those table sizes. They form long linear-probe chains; layer tags also
vanish under the mask. A subsequent frozen local attribution panel measured
prebound actual Ikea hash plus pointed summary consumption at 11.2 ns, while a
single protected Object bind took 3.12 microseconds. Full requests were 11.9
microseconds on that VM. These controls isolate resolution from the kernel;
their residency and request boundaries differ and they are not subtracted to
claim a precise residual cost.

Orbital LEAD repaired this in commit `4b4751a5`, with a deterministic regression
using natural structured IDs. Segment identities remain unchanged. The
[before](evidence/resolution-before-zen5/measurements/pressure-summary.csv) and
[after](evidence/resolution-after-zen5/measurements/pressure-summary.csv) native
captures differ only in `orbital/include/orbital/detail/object/directory.h`:

| Boundary, 4,096 prefixes | Before | After |
| --- | ---: | ---: |
| Whole-hash full layer-2 request | 7,917 ns | 172.5 ns |
| Protected layer-4 Object bind | 1,991 ns | 55.0 ns |
| Prebound Ikea hash plus pointed summary | 30.5 ns | 29.6 ns |
| Whole-hash complete value update | 522 µs | 306 µs |
| Paged-hash complete value update | 466 µs | 248 µs |

That isolates the resolver failure without changing Engine's identities. It
also prevents a false conclusion about Ikea's kernel: ordinary Object binding
was swallowing its cost. The remaining update time is still unacceptable.

## Occupied page packing and hash geometry

Fixed low-bit physical-prefix pages fail another realistic shape: with physical
prefixes `i<<16`, each small prefix page contains only one dirent, and the outer
directory remains huge. The next control packs at most 64 occupied dirents per
page and names that page by its first physical prefix. These pages store the
directory; index segments, their prefixes and all five windows are unchanged.
Logical seeks use the same repeated exact maxima. Known physical descent finds
the page through its ordered first-prefix roster.

Value updates rebuild only affected pages. A structural edit deliberately
repartitions the occupied roster and retires unused page Objects. This exposes
the benefit of page packing but does not implement stable split/merge or claim
bounded structural work. The independent oracle checks duplicate runs across
page cuts, removal/recreation and retained snapshots through repartition.

Ikea reviewed the hash shapes. A `table<32,8,64,24>` has 4,096 primary rows and
272 KiB of primary backing. Using it for each 64-entry page over-reserves about
17 MiB across 64 pages. That is allocated backing, not evidence of an equally
large active read set. Typed controls keep encoding fixed while comparing whole
b=8/b=9 and page b=2/b=3, with the oversized page b=8 owner as a negative control.
Actual spills on this seed/population are 28/0 for whole b=8/b=9 and 47/0 across
the small b=2/b=3 pages. Doubling whole capacity did not improve this fixture.

On Zen 5, typed one-owner reconstruction took 77.5 µs for whole b=8, 92.9 for
whole b=9, 56.3 for an oversized page owner, and 0.99/0.84 for small b=2/b=3 page
owners. These are owner-construction controls with retained replacements and
changed-row consumption, not complete published updates. Their prebound lookup
times do not predict the complete Scope/schema/directory request.

The [packed-page sweep](evidence/packed-zen5/measurements/pressure-summary.csv)
measures the full caller on the same five-trial, many-prefix trace:

| Directory/hash layout | Zen 5 update µs | Granite Rapids update µs | Neoverse V2 update µs |
| --- | ---: | ---: | ---: |
| Whole b=8 hash, flat lower directories | 319 | 732 | 471 |
| Fixed-prefix pages, b=8 page hashes | 259 | 578 | 365 |
| Occupied-entry pages, b=8 page hashes | 76.1 | 179 | 108 |
| Occupied-entry pages, b=2 page hashes | 66.9 | 137 | 97.7 |

For the last row, native payload copying is about 46 KB/update versus 808 KB
for the whole-directory control; changed hash backing is about 5.5 KB versus
281 KB. Directory maintenance still took 42 µs of the 67-µs Zen 5 update. This
is a useful implementation direction and a repeatable failing baseline, not
ordinary-update parity. Read indirection also remains: the corresponding whole
and compact-paged layer-2 requests were 167 and 221 ns on Zen 5.

A further root-row chunk control copies 40 KB rather than 46 KB/update and
reduces large native requests from eight to five. It loses on all three
platforms: the [root sweep](evidence/root-zen5/measurements/pressure-summary.csv)
measured 171/202/186 µs, versus 60.8/106/77.3 for unchunked roots in that same
sweep. Most of the extra time is inside the directory helper, outside bare
Object capture. The mechanism is not isolated; neither smaller copies nor
the finite/Window request threshold is sufficient to choose this layout. Keep
the losing case. Checks additionally retain a 4,097-prefix root across edits
in three row chunks and a shrink that changes its Part count.

The directory needs bounded populated-page maintenance, with native allocation,
staging and helper costs separately attributed. The maintained-page experiment
below supplies a leaf control. A flat outer roster or scanning an ever-growing
Representation Part directory is not a general solution for the possible 2^32
segments. Segment-payload implementation is separate Engine work. No arbitrary
48-bit population has been materialized, no production format is selected, and
the local-publication fixture still has the failure/recovery limits above.

The [memory-locator experiment](LOCATORS.md) separates changes in logical
routing/representation from changes in raw addresses. It compares direct byte
addresses with immutable reference cells resolved under the already selected
Orbital segment. Value-only publication can then leave the directory, hash and
overflow Objects unchanged. The experiment retains segment identity at layer
plus prefix, charges the cell metadata and checks disappearance, recreation,
actual Object-version advancement and escaping Part proofs. Calico and Bw-tree
supply prior art for this implementation question; their publication semantics
do not carry over. Mapped/resource locators and portable reopen remain open.
On the final Zen 5 many-prefix panel, compact cells reduce updates from 68.18
to 25.23 µs, with reads rising from 540 to 567 ns and 1.08 MB of locator storage.
Intel and ARM show the same direction. Full-cell and compact controls, both
summary grains, phase attribution and retained metadata are in the owned report.
This removes a measured directory-repair cost; the remaining publisher still
falls well short of the historical peer scale above.

## Maintained directory pages

The [maintenance experiment](MAINTENANCE.md) keeps page fences stable through
segment birth/death and repairs StreamPack ranks locally. Both controls receive
the same supplied segment-version histories. Populations are segments per
layer; primary time and bytes cover directory/overflow work. Fixture record
editing, segment-body generation and coupled publication are separately named.

At 4,096 layer-4 segments on Zen 5, directory mutation for five-segment rewrites
falls from 552 to 25.2 microseconds at J=0 and from 2,961 to 72.7 at J=2. Native
copied directory bytes are unchanged in that case; traversals, hash-owner
rebuilds and captures fall instead. A single-segment representation/Part-count
change improves more modestly, from 24.1 to 15.4 microseconds at J=0. The owned
report retains all three targets, both populations, five event types and normal
as well as broad-repartition cases. It also reports increased page count, the
remaining flat outer roster and unpriced ownership costs.

All three native targets pass split/merge/redistribution, retained-reader and
failure/retry histories. All ten local research checks pass ASan/UBSan.
This removes a demonstrated broad-repair failure. Directory-only costs are
still tens to hundreds of microseconds; neither correctness nor improvement
over the earlier control establishes an acceptable Engine directory.

The [Engine-versus-LMDB driver](ENGINE-VS-LMDB.md) remains a proposal. These
component measurements describe local fixture execution; direct peer
measurements remain outstanding.
