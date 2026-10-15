# Generic containers through Loom

The first resumed integration from [the recalibration](RECALIBRATION.md) connects
sets, opaque maps and multimaps to the same direct/manual/actual Loom comparison.
The previous specialized word implementation remains a paired control for sets
and values up to eight bytes. This is a spike interface, not a production choice.

`bind_read()` borrows an immutable owner and interpretation for the whole operation.
A probe emits the primary bucket, then visits the existing fused bucket reader.
A secondary bucket is requested only when descendant accounting requires it. Key
overflow adds directory/payload dependencies. External multimap values likewise
resolve their directory, then consume the complete payload before following another
link. No yield separates fields in an already fetched bucket. Sets/maps carry a
24-byte probe; multimaps carry 48 bytes. Directory resolution and key-overflow
decoding use a tail-called cold callback, keeping temporary records off the ordinary
ready path's stack. Native callers still need their own disassembly qualification.

## Complete consumers and independent checks

Both all-hit and 50%-miss shuffled cycles run. C1 measures a serial consumer;
C32 partitions one whole cycle into 32 independent chains. Every timed graph
checks its complete value digest, hit/value counts and terminal requests.

- Sets use the actual membership bit to select a successor from an external
  two-column graph. This measures a membership consumer, not bare lookup latency.
- One-, two- and three-byte values combine their full digest with an external
  successor, because a narrow value cannot encode a large arbitrary next rank.
- Four-byte and wider values carry the next hit rank in the XOR of all returned
  bytes. The hit path needs no external successor load; misses still do.

Every query consumes all returned values. Mixed multimap fanout uses a deterministic
hash of rank: nominally 75% singleton, 15% pairs, 6% four, 3% sixteen, 1% sixty-four.
The actual finite realization is shared by contenders. Expected values are generated
during setup/checking, avoiding one fixture vector allocation per key. The temporary
shuffle is released before measurement; all-hit wide-value graphs also release the
unused successor array. Live graph, jobs and Ring bytes are recorded separately.

The baseline is ordinary `flat_hash_set`/`flat_hash_map`; multimap values use
`InlinedVector<array<byte,W>,1>` (its actual capacity may exceed the template minimum).
Its public prefetch is a separate arm. Candidate arms are direct, manual staged
with/without hints, and actual Loom Ring with/without hints. Fixed L1 hints use
Loom and the discovered worker geometry. Caller heat/adaptive policy selection is
still open; this panel does not install the Ring on every hot read.

Independent per-key checks compare complete bytes and multiplicity. Each emitted
window must fit one independently inventoried live extent. Forced small geometries
assert populated primary, secondary and key-overflow tiers. The same interface
covers linked bags, individual Orbital buffers and pooled contiguous vectors.
Sixteen local checks pass, including all eight key widths and values of
1/2/3/4/8/16/32/57/64 bytes. This is boundary coverage, not an exhaustive Cartesian
product or native performance qualification. The scoped immutable histories do
not by themselves stand in for retained-reader MVCC tests.

`staged_publication.cpp` now checks the actual Orbital ObjectStore boundary. It
keeps an old Scope and queued Loom borrowers alive while a private replacement
deletes/reuses every key, refuses one capture, then publishes successfully. Reads
resume against the old payload and interpretation; an initially unbound retained
Scope also discovers the old values. Sets, maps and all three bag owners pass,
with all key tiers populated and balanced catalog/material retirement. This is
scoped Ring execution across publication, not Task suspension/cancellation. It
uses full material cloning, not fine-grained COW or a byte-damage journal consumer.

## Comparison and evidence

[Paired local evidence](evidence/staged-paired-local/summary.json) includes all methods,
raw timings, source/binary hashes and ready/Abseil code-generation checks. These are
OrbStack ARM development diagnostics, with pinned Clang and automatic vectorization
disabled, run sequentially on CPU 0. They establish neither Neoverse performance
nor named cache residence. The earlier `evidence/staged-local` panel used a different
graph, redundant C32 traversals and an oversized set layout; do not combine it with
these timings or use it to select formats.

The new compact hot-set control uses 512 primary/32 secondary 64-byte buckets.
Hot maps retain 1,024/64 128-byte units; multimaps use 192-byte units and a 16-byte
inline budget. These are matched controls, not final presets. The native runner
also captures larger footprints, with separate process/RSS/NUMA/owner receipts:

| Scale | Set keys / primary buckets | Map and multimap keys / primary buckets |
| --- | --- | --- |
| hot | 6,144 / 512 | 6,144 / 1,024 |
| large | 14,400,000 / 1,048,576 | 3,600,000 / 524,288 |
| dram | 115,200,000 / 8,388,608 | 57,600,000 / 8,388,608 |

Scale names are input controls, not proof of residence. Sets need more keys to
exceed cache because their representation is smaller. The largest Granite case
is designed to exceed its 480 MiB LLC even for the compressed set. Requested,
backing and metadata bytes remain separate; Abseil has logical allocation
accounting, not a fabricated backing/RSS estimate. Process RSS includes the paired
contenders, fixture and runtime and cannot be assigned wholly to one table.

`staged-native.sh` builds and checks before timing, preserves binaries/disassembly,
qualifies ordinary ready callbacks and paired baseline inlining, then measures all
owners with rotating order. `staged-report.py` rejects missing arms or repetitions
and keeps C1, C32 and bytes separate. The live native handles are in [STATUS.md](STATUS.md).

The x86 audit permits and explicitly reports one RBP save/restore in a ready
callback: `preserve_none` still preserves that register. It occurs in the hinted
word-map reader and generic multimap readers in the captured hot binaries. No
other ready-path stack traffic or ordinary calls are admitted. Those callbacks
are not frameless; their ABI cost is included in timing. ARM callbacks in the
completed V2 panel have no calls or stack traffic. This is a static boundary
check, not proof that the generated instruction sequence is optimal.

## First native result: Neoverse V2

Three repetitions on c8g.xlarge, NEON, pinned Clang 21.1.8. The host reports
2 MiB L2 and 36 MiB LLC. The larger owners each exceed LLC; full shuffled traces
touch every key. This is a sustained above-cache workload, not a claim that every
demand load reaches DRAM. Process peak RSS is 503.6 MiB for the paired set case,
290.3 MiB for maps and 453.2 MiB for multimaps. Those totals include all contenders
and the graph. Page/NUMA receipts and owner extents remain in the recoverable run.

The table uses generic direct reads for serial cost; C32 columns show each
explicitly staged method. Abseil is the faster median of direct/prefetch at that
concurrency. Every entry is ns per completed request, including the consumer.
These are separate choices; the table does not imply selecting Ring for hot data.

| Scale / kind | Queries | Serial ns, ours / Abseil | C32 manual+hints | C32 Loom+hints | C32 Abseil |
| --- | --- | ---: | ---: | ---: | ---: |
| hot / set | hits | 4.61 / 3.96 | 5.25 | 7.05 | 4.18 |
| hot / set | 50% misses | 10.88 / 10.86 | 11.93 | 15.28 | 10.80 |
| hot / map | hits | 15.45 / 11.82 | 6.95 | 8.37 | 4.28 |
| hot / map | 50% misses | 14.70 / 13.13 | 9.35 | 11.81 | 12.16 |
| hot / multimap | hits | 23.10 / 16.83 | 14.71 | 18.67 | 10.49 |
| hot / multimap | 50% misses | 19.51 / 16.09 | 16.38 | 20.65 | 14.98 |
| large / set | hits | 129.74 / 126.98 | 33.25 | 46.55 | 34.37 |
| large / set | 50% misses | 141.08 / 152.25 | 33.82 | 43.51 | 41.66 |
| large / map | hits | 141.73 / 139.20 | 15.79 | 14.86 | 27.93 |
| large / map | 50% misses | 172.86 / 148.51 | 28.89 | 36.47 | 37.91 |
| large / multimap | hits | 192.02 / 168.46 | 30.81 | 32.65 | 48.82 |
| large / multimap | 50% misses | 197.05 / 165.66 | 41.54 | 47.54 | 50.84 |

Logical bytes, generic / ordinary Abseil, for the same owners:

| Scale | Set | Map | Multimap |
| --- | ---: | ---: | ---: |
| hot | 35,784 / 73,712 | 139,560 / 139,200 | 389,984 / 341,248 |
| large | 71,303,464 / 150,994,928 | 71,303,464 / 71,303,104 | 178,637,294 / 177,549,184 |

Backing is separate: the large generic set/map each owns 71,303,168 accessible
bytes; the multimap owns 199,564,800 in the hit graph and 200,031,744 in the mixed
graph. Logical contents are equal between those two bag histories; Orbital reuse
changes backing. Abseil backing is unmeasured.

The generic path preserves the word-map cold advantage: its all-hit C32 Ring
cost is 14.86 ns versus the paired word reader's 15.29 ns. At C1 the generic
hinted Ring takes 139.25 ns versus Abseil 139.20. Mixed multimap throughput improves
too, with serial and hot-read losses still visible. Sets save substantial logical
memory but do not benefit from Ring here. The best execution choice depends on
consumer, temperature and concurrency; this supports a shared storage/reader
boundary, not universal Ring dispatch. These measurements do not compare mutations.

Sources: [all timings and memory](evidence/staged-neon/summary.csv),
[hardware](evidence/staged-neon/hardware.json), and the accompanying recovery
reference. The baseline, direct/manual controls, word reader and losing methods
are all retained. Do not combine this new graph with the older Ring scorecard.

## Zen 5 counterpart

Three repetitions on c8a.xlarge, AVX2. The same graph and layouts reproduce
the generic all-hit cold advantage. The 50%-miss graphs have meaningful losses;
they are the subject of the matched caller-input experiment below. Abseil is
again its faster direct/prefetch median. Serial uses generic direct; C32 keeps
manual and Ring separate. All entries are ns/request.

| Scale / kind | Queries | Serial ns, ours / Abseil | C32 manual+hints | C32 Loom+hints | C32 Abseil |
| --- | --- | ---: | ---: | ---: | ---: |
| hot / set | hits | 3.48 / 3.06 | 3.86 | 4.49 | 2.77 |
| hot / set | 50% misses | 5.56 / 4.82 | 6.98 | 8.55 | 4.27 |
| hot / map | hits | 9.95 / 7.76 | 4.30 | 4.71 | 2.53 |
| hot / map | 50% misses | 7.29 / 6.42 | 4.39 | 4.99 | 4.01 |
| hot / multimap | hits | 13.30 / 8.59 | 7.56 | 8.10 | 5.24 |
| hot / multimap | 50% misses | 9.79 / 7.38 | 10.95 | 11.65 | 6.65 |
| large / set | hits | 141.04 / 147.10 | 43.69 | 45.41 | 33.40 |
| large / set | 50% misses | 165.01 / 160.24 | 44.43 | 47.68 | 34.11 |
| large / map | hits | 110.51 / 111.10 | 6.95 | 7.42 | 27.51 |
| large / map | 50% misses | 165.55 / 137.46 | 35.91 | 38.80 | 33.23 |
| large / multimap | hits | 196.03 / 169.59 | 17.09 | 17.74 | 48.82 |
| large / multimap | 50% misses | 205.34 / 171.83 | 49.25 | 52.58 | 47.24 |

Candidate logical bytes match the V2 table above. Zen Abseil's owner metadata
adds eight bytes to each corresponding V2 total. Backing, graph, job and Ring
bytes remain separate in [all Zen rows](evidence/staged-zen5/summary.csv); the
recovery bundle contains actual mapping/NUMA and hardware receipts. The large
owners exceed the reported LLC; no PMU service-level claim is made. Hot direct
C32 hits are 3.22/3.64/7.30 ns for set/map/multimap versus Abseil 2.77/2.53/5.24 ns:
staging every hot query would be the wrong inference from the cold wins.

The follow-up `staged-caller-native.sh` tests a caller-side omission in this
first panel. The successor-graph cell address is known when each query begins,
but was not prefetched alongside table storage. The new default-off control
issues that hint for both candidate and Abseil hinted methods. It introduces no
new Ring visit and never touches the released all-hit wide-value graph. Local
complete-graph checks and ready/baseline audits pass; AArch64 emits the expected
indexed `prfm` before the next bucket request. Native comparison uses matched
sources and keeps all C32 mixed methods, rather than borrowing timings from an
older binary. New raw rows
identify `graph_hints`; graph storage now also counts the input object's small
fixed metadata, rather than only its boundary array.

The matched V2 follow-up completes three repetitions of every C32 mixed arm.
Known caller-input hints improve throughput across all three kinds:

| Kind | Generic manual, table-only / caller+table | Generic Ring, table-only / caller+table | Abseil prefetch, table-only / caller+table | Best Abseil with caller hints |
| --- | ---: | ---: | ---: | ---: |
| set | 33.86 / 18.88 | 43.68 / 19.60 | 46.96 / 42.84 | 42.84 |
| map | 28.91 / 23.87 | 36.46 / 26.71 | 44.22 / 38.56 | 38.56 |
| multimap | 41.26 / 35.56 | 47.88 / 38.85 | 54.86 / 49.13 | 49.13 |

All timings are ns/request. Paired logical storage, request, job and Ring-visit
inventories match exactly. Unhinted controls and repetition ranges are retained
in [the complete caller panel](evidence/staged-caller-neon/summary.csv). The set
gain is especially large because every set query consumes an external successor;
wide-value hits carry it in their own value, so only misses need that input. The
hint is still issued before membership is known, including queries that turn out
to hit. This is measured complete-operation performance, not an attributed count
of saved misses. It adds no storage/format feature. It shows why known caller
inputs should compose with container hints before yielding. C1 and hot dispatch
were not requalified by this targeted follow-up.

The [matched Zen 5 follow-up](evidence/staged-caller-zen5/summary.csv) confirms
the opportunity, with three repetitions and the same full-graph checks:

| Kind | Generic manual, table-only / caller+table | Generic Ring, table-only / caller+table | Abseil prefetch, table-only / caller+table | Best Abseil with caller hints |
| --- | ---: | ---: | ---: | ---: |
| set | 45.27 / 9.63 | 48.38 / 10.80 | 47.15 / 34.72 | 34.72 |
| map | 36.63 / 9.52 | 39.48 / 10.53 | 45.79 / 34.73 | 34.52 |
| multimap | 47.34 / 17.79 | 51.44 / 18.45 | 55.52 / 45.89 | 45.89 |

These are C32 reciprocal-throughput ns/request for 50%-miss graphs, not serial
latency. The map's best baseline is its direct arm. Generic Ring repetition
ranges with caller hints are 10.73–10.81, 10.52–10.53 and 18.36–18.61 ns.
Unhinted generic direct controls remain much slower at 42.65/49.59/76.96 ns;
the improvement is specific to preparing known inputs early. Complete paired
logical/graph/job/request/visit inventories agree. Native ready callbacks have
no ordinary calls or stack traffic beyond the explicitly reported RBP save in
the multimap and one word-reader control. This is evidence for composing useful
prefetches at an existing boundary, not for adding stages or abandoning the
direct hot path. Manual staging still has a smaller scheduling cost here.

Logical bytes (generic / ordinary Abseil) in this same cohort are
71,303,464 / 150,994,936 for sets; 71,303,464 / 71,303,112 for maps; and
178,637,294 / 177,549,192 for multimaps. Generic accessible backing is
71,303,168 / 71,303,168 / 199,564,800 B respectively. Baseline backing remains
unmeasured; co-resident process RSS does not prove a per-container RSS saving.

## Granite: completed scope and deadline boundary

AVX-512 job `20261015T000522Z-8df6d309` reached its execution deadline after
completing hot/large sets, maps and multimaps and the 115.2M-key set. Its DRAM
map CSV is partial (67 of 96 rows); DRAM multimap timing did not start. Those
are not completed comparisons. The verified archive and raw partial output are
retained in [Granite evidence](evidence/staged-granite/artifact.json).

The following medians use two complete repetitions per arm. Serial is the C1
direct consumer; throughput is C32, in ns/request. The baseline throughput is
the faster median of ordinary direct/prefetch. These original-source runs lack
the later matched caller-successor hint, which must be qualified separately.

| Scale / kind | Queries | Serial ours / Abseil | C32 direct | C32 manual+hints | C32 Loom+hints | C32 Abseil |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| hot / set | hits | 4.68 / 4.52 | 4.39 | 4.33 | 5.48 | 3.32 |
| hot / set | mixed | 12.80 / 10.86 | 12.28 | 12.92 | 15.39 | 10.65 |
| hot / map | hits | 14.43 / 11.42 | 4.78 | 5.02 | 5.37 | 3.84 |
| hot / map | mixed | 14.15 / 13.72 | 9.45 | 8.67 | 9.41 | 10.46 |
| hot / multimap | hits | 19.34 / 12.86 | 8.86 | 14.41 | 15.87 | 7.18 |
| hot / multimap | mixed | 17.89 / 15.22 | 14.15 | 16.46 | 17.33 | 13.25 |
| large / set | hits | 124.12 / 137.27 | 36.73 | 45.59 | 52.30 | 32.50 |
| large / set | mixed | 175.47 / 185.63 | 44.31 | 46.39 | 48.68 | 42.00 |
| large / map | hits | 130.36 / 101.80 | 30.99 | 9.20 | 9.42 | 26.58 |
| large / map | mixed | 207.44 / 205.24 | 50.34 | 35.33 | 40.16 | 39.57 |
| large / multimap | hits | 167.23 / 145.43 | 56.55 | 22.29 | 24.68 | 43.30 |
| large / multimap | mixed | 184.47 / 161.39 | 50.24 | 36.35 | 36.92 | 45.40 |
| dram / set | hits | 193.02 / 193.33 | 45.36 | 51.61 | 56.74 | 48.89 |
| dram / set | mixed | 206.54 / 218.79 | 50.39 | 51.11 | 56.73 | 60.74 |

The host reports 2 MiB L2 and 480 MiB shared LLC. The compressed DRAM set itself
uses 570,425,640 logical bytes versus Abseil's 1,207,959,544; its external
successor graph adds 1,843,200,132 B. Peak process RSS is 4,098,460 KiB, including
both owners, the word control, graphs and runtime. Large-map and multimap owner
sizes are 71.3/178.6 MB, below this host's LLC, so their large-cohort wins do not
establish DRAM-map performance. Full-trace demand is above cache for the DRAM
set; there is no claim that every load misses LLC. Hinted Ring is slower than
direct there despite favorable direct throughput and memory.

The multimap follow-up `20261015T020952Z-c486965a` completes both graphs,
C1/C32, all methods and two repetitions from the exact original source; only
the runner gains a kind-selection parameter. It uses 57.6M distinct keys and
mixed fanouts, consuming every returned value. All entries remain ns/request:

| Queries | Serial direct ours / Abseil | C32 direct | C32 manual+hints | C32 Loom+hints | C32 best Abseil |
| --- | ---: | ---: | ---: | ---: | ---: |
| hits | 306.33 / 244.59 | 96.88 | 41.21 | 42.13 | 58.94 |
| mixed | 284.99 / 266.47 | 86.75 | 65.68 | 69.18 | 67.31 |

Generic logical storage is 2,854,933,212 B versus Abseil's 2,841,335,752 B,
each well above this host's 480 MiB LLC. Generic accessible backing is
3,189,956,224 B for hits and 3,191,185,024 B for mixed queries; baseline backing
is unmeasured. Peak process RSS is 6,907,072 KiB, charging both contenders and
their caller/runtime storage rather than attributing that total to one container.
The all-hit cold throughput advantage survives the larger footprint; the mixed
advantage is thin for manual staging and absent for Ring. Direct serial reads
remain slower. These runs still lack the later caller-successor hints, so their
possible mixed-query benefit on Granite is unmeasured. [Complete raw/summary
evidence and recovery](evidence/staged-dram-mm-granite/artifact.json) retains
the losing methods and physical inventory too.

Map follow-up `20261015T020952Z-ba546ac4` also completes every arm and both
repetitions at 57.6M keys. Direct serial hits cost 237.44 ns versus Abseil's
227.10 ns; direct serial mixed reads cost 257.26 versus 277.43 ns. C32 hinted
Ring costs **22.36 versus 45.83 ns** for hits and **50.80 versus 63.53 ns**
for mixed queries (the faster Abseil direct/prefetch median in each case).
The generic and specialized word Ring hit controls are 22.36 and 23.01 ns,
so the large throughput advantage survives the generic owner at this footprint.
Generic logical bytes are 1,140,850,984 versus Abseil's 1,140,850,632, with
1,140,850,688 B accessible backing for the generic owner. Peak process RSS is
4,246,932 KiB across the generic, word, Abseil and caller/runtime cohort.
[Complete map evidence](evidence/staged-dram-map-granite/artifact.json) retains
direct, manual, Ring and prefetch controls, serial and reciprocal rates, and
the physical inventory. This completes the originally missing Granite DRAM
read comparison for all three container kinds; it does not qualify cold mutation
or the later caller-input hint on Granite.

The next work is completing native qualification, heat selection and broader
mutation/retirement histories. Small-domain cold
workloads need many independently owned tables rather than impossible per-table
cardinalities. Coupled geometry and multimap lifetime studies remain separate
inputs to the final interface and format recommendation.
