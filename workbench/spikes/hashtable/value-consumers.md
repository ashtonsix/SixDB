# Wide-value consumers and packing transfer cost

A lookup may consume an entire opaque value or just one field. These operations
put different demands on a container, even when its stored bytes are identical.
The earlier hierarchy packet driver reduced every byte with a weighted scalar
checksum. That is a valid full-value workload, but does not establish the cost
of fetching one field from a wide record.

`hierarchy_batch.cpp`, built with `HT_PACKET_CONSUMERS=1`, compares the same
complete histories with two consumers: a weighted full-byte reduction, and an
unaligned eight-byte load from the end of every value. Both count hits and all
returned values, including multimap duplicates. Abseil exposes a synchronous
borrow of its stored bytes. No value escapes the callback. The selected-word
reference reconstructs bytes with shifts, independently of the timed memcpy;
whole-value contents and per-lane multiplicity are also checked outside timing.

The six cases are K16/W16 map; K40/W16 map and multimap; K40/W64 map and multimap;
and K64/W64 multimap. Every case includes scalar, four-key and eight-key callers,
composed fields, direct bucket fields, ordinary Abseil and exact-key Abseil.
Direct changes fingerprint, suffix, tag and payload transfers in primary and
secondary buckets. It shares routing, headers, key-overflow links and bags with
the composed table. It isolates transfers, not an independent container design.

The byte layout, hash family, primary/secondary counts A6/B2, 128-byte units,
16-byte inline budget and StreamPack bag policy remain fixed. W16 multimaps
initially hold one inline value and spill on update; W64 values start external.
These controls do not choose geometry, growth or inline policy. Large histories
deliberately become crowded, making overflow an observable part of the operation.

The runner records separate fresh processes in shuffled order and then reverse
order. It checks the full schedule, ELF hashes, every phase and operation count,
and equality of logical inventories across consumer, packet-width and
composed/direct pairs. Construction, insertion, duplicate/sparse mutation,
erase/reuse and release remain measured; phase-summed lifecycle excludes reads.
Read mode is selected once per complete trace, not dispatched for each key.

## Actual generated callers

The final source capture is `hashtable-selected-consumer-v3`, digest
`173da826a640c4e21253248200f432c9e2b3f23c4887a0e4258b33673ac81d63`.
This is pinned Clang 21.1.8, generic AArch64/NEON in OrbStack, with both automatic
vectorizers disabled. It is not native V2/V3 or x86 qualification.

All 144 final full/tail read roots are call-free. All 36 composed/direct pairs
have identical instructions, registers and immediates after normalizing only
root-relative branch targets. Thus these exact byte-plane packing transfers add
no instructions over the direct-field control in the audited complete callers.
The finding does not cover non-identity packing or independently routed kernels.

The selected K40/W64 map paths converge on one explicit eight-byte payload load,
including key overflow. Multimap bag traversal loads only the final word with a
64-byte stride. Scalar and packet multimap index interpretation retain an
eight-byte head chunk and use its low 32 bits. Neither path copies or reduces
the remaining value bytes. Admitted selector windows can
overlap other payload bytes incidentally; this is not an eight-byte total-memory-
traffic claim. No StreamPack traversal or user-callback call remains.

The first audit found full-value checksum wrappers outlined in the benchmark.
After those two forwarding bodies were marked inline, a full-value scalar
multimap callback could still remain outlined in `promoted_table::get_bytes`.
Its record-to-value forwarding body now also preserves the caller's inlining.
The final audit confirms all three repairs. This is a composition-boundary
finding above the packing primitives. V1 timings retain those outlines; V2 was
an untimed diagnostic build. Only the final V3 panel qualifies the corrected
comparison. Its full consumer also borrows bytes instead of the old array-copy
adapter, so earlier driver receipts are not a projection-only comparison.
The final forwarding annotation also changes some tail register/frame choices
and the scalar bag-head load width. V1-to-V3 is not an isolated call-cost test.

Six Release checks exercise both composed and direct fields across scalar/4/8
callers, all three tiers, duplicate writes, sparse active masks, refusal-prefix
retry, erase/reuse and cloning. Selected W64-map and W16-multimap checks also pass
ASan/UBSan, as do 48 complete full/tail trials across all four contenders.

## Local measurements

The final broad panel contains 77,760 phase rows from 864 fresh processes,
with nine repetitions in each of two reversed-order passes. A scalar replay
uses the same six exact ELFs, 31 repetitions per process, 192 processes and
59,520 phase rows. It repeats n64/n192; the broad panel also includes n768.
All reports and independent content checks pass. These are repeated warm
throughput measurements, not serial latency or evidence of a named cache tier.

The following initial successful scalar reads are from the longer replay.
Nanoseconds include the requested consumer. Ratios divide candidate time by
the named baseline; below one is faster. Tier populations are exact key counts.

| Case | Keys | Primary / secondary / overflow | Full ns/query | Tail ns/query | Tail / Abseil | Tail composed / direct |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| K16, W16 map | 192 | 191 / 1 / 0 | 8.48 | 2.97 | 1.36 | 1.006 |
| K40, W16 map | 192 | 182 / 10 / 0 | 8.73 | 3.17 | 1.58 | 1.000 |
| K40, W16 multimap | 192 | 182 / 10 / 0 | 9.15 | 3.51 | 1.16 | 0.992 |
| K40, W64 map | 64 | 43 / 4 / 17 | 30.88 | 3.62 | 1.96 | 0.995 |
| K40, W64 map | 192 | 63 / 4 / 125 | 47.37 | 16.58 | 8.39 | 0.969 |
| K40, W64 multimap | 192 | 182 / 10 / 0 | 44.35 | 6.51 | 2.18 | 0.999 |
| K64, W64 multimap | 192 | 167 / 16 / 9 | 45.29 | 7.07 | 2.38 | 0.999 |

The consumer changes operation cost substantially on both candidate and Abseil.
It does not remove the candidate's lookup deficit. K40/W16 multimap initial
inline hits are relatively close, while the updated history costs 1.95 times
ordinary Abseil for selected reads after spilling. K40/W64 multimap selected
reads already lose with no key-overflow entries, exposing the shared bag/owner
path rather than an incidental forced key-overflow case.

W64 maps fit only one slot in the held 128-byte bucket. At n768, 700 of 768 keys
are in overflow; that comparison diagnoses an overfilled fixed geometry, not
a proposed table size or growth policy. Narrower cases also overflow at n768.
Those rows remain in the full report instead of being discarded or generalized
into a claim about a properly grown table.

Direct/composed differences do not establish transfer speedups when the actual
paired instruction streams are identical. In the scalar replay, hit/aged-hit
ratios span 0.944–1.014. The low endpoint is the crowded W64-map aged read, with
separate passes 0.937 and 1.004; its apparent gain is not stable. Short mutation
phases also vary despite unchanged consumer-independent write work. The report
retains both passes, all contenders, phases and negative results.

The [retained comparison](evidence/value-consumer-local/context.json) includes
both panels' full/tail read summaries, validation and the
[final caller audit](evidence/value-consumer-local/codegen-v3/review-index.json).
Its verified archive preserves raw complete histories, exact ELFs, source,
report regeneration and the earlier outlining diagnostics. Superseded V1 timing
panels are omitted; their caller defects are preserved as code-generation evidence.

## Disposition

Retain both consumers. A full-byte reduction can obscure container lookup costs;
a selected-field consumer is also only one workload. Their timing difference is
not an isolated lookup cost, and changing the requested work is not a container
speedup. The current scoped callback already permits payload selection without
a new public projection interface in these layouts.

Remaining lookup and bag-traversal gaps must be addressed in the shared table
organization and ownership path. Independently routed, non-identity packing
comparisons remain separate evidence. Serial latency, target-native residence,
growth policy and retained-reader costs remain part of the wider spike.

One concrete design restriction now needs revisiting: `inline_bag` caps its
experimental budget at 56 bytes to leave a tag within `record_table` and `links`'
64-byte internal record limit. Consequently it cannot even test one inline
64-byte value. That limit is not an evidenced choice for wide multimaps or a
StreamPack slot-size limit. A one-inline-wide control must be measured together
with its larger bucket footprint, changed fitting capacity, spill, overflow and
whole lifetime before choosing the external-bag policy.
