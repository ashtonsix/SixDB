# Output alignment in the expression materializer

The controlled address sweep does not explain the two earlier bimodal
AVX512/u32 results. Their slower mode does not recur at any tested natural
output offset. It does expose a separate u64 aligned-range slowdown away from
32-byte output boundaries. Both observations remain specific to this caller,
consumer, machine and physical candidate; neither adds an alignment admission.

## What was held fixed

Zen job `20260910T224437Z-a5ca5798`, capture
`63790360d963d562868ac8ac7a7c674c282af827c29ddbf066b880f52d75c82d`,
reuses the exact whole-window and combined callback objects from the
[native-edge investigation](edges.md). Both original
programs first relink byte-identically. One newly compiled runtime caller
object is then linked against each callback pair. The original uninstrumented
combined program is a separate control, measured first and last with reversed
provider order.

The new caller selects the output offset before timing. It verifies every
query at that exact pointer, including all canaries around the requested
output span. Extra backing storage supplies no writable slack. The timed loop
retains one indirect callback and the original first/last scalar consumer.
It records actual output addresses modulo 64 and 4096, plus payload/head
addresses modulo 4096. Its stack layout and spilled checksum differ from the
original caller, so the two callers are separate experiments.

The scope is Striped12/H0 and Striped28/H16, u32/u64, all four original
runtime regimes. AVX512 tests every natural output offset modulo 64; AVX2
tests offsets 0 and 32 as controls. The full x86 profile permits VBMI/VL in
both labelled families. This is not pure AVX2 performance evidence.

Binary order is before/after/after/before, with provider order reversed after
two blocks. Each case has three sequential 30 ms repetitions on CPU0. The
[independent audit and retained samples](evidence/materialize-alignment-20260910/20260910T224437Z-a5ca5798/provenance.json)
cover 6,192 measured samples and 1,032 separate one-iteration preflight samples,
all runtime counters/checksums, original link inputs and exact output addresses.
Both frozen candidates' native expression and sink guards also pass again.

Three earlier runner attempts ended at relinking (`222720Z-7103a76a`), control
execution (`223751Z-a450163b`), and inventory validation (`224123Z-65ca4ada`),
all on 2026-09-10. None produced paired comparisons. Their failures are retained;
the completed capture fixes the path type, executable mode and target filter,
and actually executes the separate preflights before timing.

## Observations

Among 224 comparisons against ordinary in the same revised program, 198 have
separated wins, none have separated losses, and 26 overlap. There are 222 lower
medians and 208 directions agreeing at both provider-order edges. These counts
do not close the earlier unresolved blocks or the broader campaign.

For AVX512/u32 origin17/count17, Striped12 candidate medians across all 16
offsets are 7.210–7.310 ns/query, and Striped28 medians are 7.854–7.899. All
32 comparisons improve at both ordering edges; 31 have separated samples and
Striped12/output60 overlaps. The two revised-program blocks place output at
page offsets `640 + requested_offset` and `2432 + requested_offset`. The
earlier 11.7/13.2 ns modes do not recur. The original uninstrumented control
also has 68 separated wins here, including those two cases. This non-recurrence
does not establish the cause of the previous slow blocks.

The u64 aligned origin16/count16 case has a different address-sensitive result:

| Case | Output offsets modulo 64 | Ordinary medians | Combined medians |
| --- | --- | ---: | ---: |
| AVX512 Striped12/u64 | 0, 32 | 3.435–3.538 | 2.849–2.853 |
| AVX512 Striped12/u64 | 8, 16, 24, 40, 48, 56 | 6.167–6.270 | 5.778–6.014 |
| AVX512 Striped28/u64 | 0, 32 | 3.663–3.695 | 3.275–3.335 |
| AVX512 Striped28/u64 | 8, 16, 24, 40, 48, 56 | 6.119–6.268 | 5.894–6.273 |

Numbers are CPU ns/query, with ranges across offset medians rather than sample
extrema. All 16 cases, individual extrema and ordering edges are retained in
the primary table. Only two have separated wins against ordinary. These legal
offsets need useful implementation work; the fast offsets cannot become a
new caller requirement. The [earlier short-store experiment](stores.md)
did not establish a general u64 benefit in its uncontrolled output context.

Against the frozen whole-window callbacks in the same new caller, 113 cases
have separated wins, none have separated losses, and 111 overlap. The unchanged
ordinary provider across the two linked programs has 99 separated losses and
one win among 224 cases. That substantial context variation remains visible;
it prevents treating the paired-revision recovery count as a complete causal
account of the changed stores.

The retained [actual-object store paths](evidence/materialize-alignment-20260910/20260910T224437Z-a5ca5798/callback-store-audit.json)
show output0–15 written by two 32-byte instructions, followed by mask1
`vpcompressd` at output16 for the 17-row u32 case. The producer's final native
region starts at original row33. The
[linked callers](evidence/materialize-alignment-20260910/20260910T224437Z-a5ca5798/timed-callers.json)
retain both endpoint loads. This motivates testing an exact unsigned scalar
store for a single active result while keeping the producer/read policy fixed.
It does not establish a universal compress-store tax or forwarding latency.

## One active result through an exact scalar store

Job `20260910T225235Z-f0841a28`, capture
`0b9485e8c4cdbe21b82ebec3267cef0add7d20f2721a995daf2c7a1b1cdb2afe`,
changes only the AVX512 partial sink when exactly one row is active. It selects
that native lane, converts through unsigned u32, and writes one u32/u64 object.
The unsigned intermediate prevents sign extension into u64. The native
producer, actual source windows and dependencies, traversal, full stores and
other partial counts remain fixed.

The original runtime/selector objects are reused and the combined baseline
relinks byte-identically. All native expression and sink guards pass, including
the complete unsigned32 domain, poisoned unclaimed lanes and exact output
boundaries. The [independent audit](evidence/materialize-region-20260910/20260910T225235Z-f0841a28/provenance.json)
retains all 2,700 original-caller, bulk and resident samples. Actual AVX2
instructions/relocations are unchanged. The
[actual AVX512 store paths](evidence/materialize-region-20260910/20260910T225235Z-f0841a28/single-store-codegen.json)
have an exact scalar store for one active row and retain compress stores for
larger partial selections. There are no inner calls or vector stack staging;
static callback instruction counts increase by 14–16, not measured executed
instruction counts per query.

| AVX512 origin17/count17 | Frozen combined | Single-value store |
| --- | ---: | ---: |
| Striped12/u32 | 7.461 | 4.842 |
| Striped12/u64 | 7.600 | 4.942 |
| Striped28/u32 | 8.154 | 5.394 |
| Striped28/u64 | 8.346 | 5.565 |

Numbers are median CPU ns/query; all four improvements have separated samples.
The six Local mixed cases improve by roughly 17–21%, while shifted Local17
cases mostly overlap their frozen counterparts. The latter traverse different
partial counts and do not imply the same single-row store cost.

Against ordinary in the revised program, all 68 cases have separated wins and
agree at both provider-order edges. Against the frozen combined callbacks,
61 medians improve, with 26 separated wins, no separated losses and 42 overlaps;
46 directions agree at both edges. The ordinary/control comparison retains
12 separated losses and 23 wins among 157 cases. Ordinary Striped12/u32 shifts
by about 20–22% in both AVX2 and AVX512 on fixed shifted ranges, alongside
four bulk/resident losses. Every control remains in the evidence.

The useful result is a smaller physical sink operation under the same positional
and expression-domain contract. It does not require a semantic interface change,
explain all process variation, or close legal-alignment and broader integration
work. The u64 full-store experiment remains a separate comparison from the
combined baseline.

## Splitting Count16/u64 full stores

Job `20260910T225913Z-c9917478`, capture
`e195317bf8c5a56f1d278850d559e20e8c8f84ed50932adc6f4d665a1e71755f`,
compares the combined baseline with explicit 32-byte stores for native ZMM
Count16/u64 output. Count8 and every partial store remain unchanged; the
single-value candidate above is deliberately separate. All matched native
guards pass. The original caller retains its
[2,700-sample comparison and controls](evidence/materialize-region-20260910/20260910T225913Z-c9917478/provenance.json).
The same callback object pairs then run through one shared controlled-offset
caller, with [5,376 audited samples](evidence/materialize-alignment-20260910/20260910T225913Z-c9917478/provenance.json)
and 896 separate preflight samples. Source identities, link substitutions,
actual addresses and all query counters/checksums are checked independently.

For origin16/count16, every output offset outside a 32-byte boundary improves
with separated samples in both striped widths. Striped12 falls from
5.620–5.664 to 2.952–3.141 ns/query across those six offsets; Striped28 falls
from 6.058–6.341 to 3.301–3.724. Shifted origin17/count16 and mixed ranges also
have 12 separated wins apiece at those offsets. The 36 wins are useful evidence
for this output context, not a general u64 default.

Twelve changed AVX512/u64 cases have separated losses against the frozen
combined candidate. Striped12 aligned16 at output32 goes from 2.763 to 3.053
ns/query; shifted16 at outputs0/32 goes from about 2.82 to 3.16. Mixed/output0
also loses. Every Striped12 origin17/count17 offset loses, from roughly
7.25–7.37 to 7.89–8.37. The complete aligned paired-revision table has 36
separated wins, 63 losses and 125 overlaps: its other 51 losses are 48 u32
cases and three AVX2 controls. All remain open.

Against ordinary in the revised aligned program, 193 cases have separated wins,
none have separated losses and 31 overlap. The unchanged ordinary provider
across binaries retains 38 separated losses and one win. In the original
caller, the revised candidate has 64 separated wins against ordinary and four
overlaps; against frozen combined it has ten separated wins, no separated
losses and 58 overlaps. The original ordinary/control table also retains two
separated losses and 17 wins.

The initial ordinary-reader increment therefore keeps the supported single-value
change and existing u32 full-store choice. This u64 splitting policy remains
an address-specific candidate. The legal-offset performance gap is still owned
work; it cannot be converted into an alignment requirement on callers.
