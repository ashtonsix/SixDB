# Scalar arguments at the trusted decode boundary

The integrated callback now passes output pointer and element width directly.
It removes the unused output-capacity field from the already trusted boundary;
the ordinary `decode(rows, output_values)` facade and checked capacity/type/
overlap validation remain intact. The exposed callback type changes, requiring
matching callbacks and callers to be rebuilt together. Encoder signatures and
physical wire contracts are unchanged.

Fresh paired builds improve every ordinary get16 case on Zen and GNR. The V2
effect is smaller and includes regressions. This is a useful implementation
change with remaining performance work, not evidence that the range campaign
is complete or that one calling convention is best for every native region.

## Paired evidence

[Zen](evidence/scalar-callback-20260910/zen/provenance.json),
[GNR](evidence/scalar-callback-20260910/gnr/provenance.json) and
[V2](evidence/scalar-callback-20260910/v2/provenance.json) retain every ordered
block and repetition, counters, compiler/host contexts and recovery bundles.
Their adjacent audit, summary and control tables preserve before/after extrema
and both ordering edges. The common captured source digest is
`822f7773841dd5186863b68ccce38ed11c2060199ccaf2703a3cdc3a982f9d4d`.

Before and after differ in exactly three files: operations.h, native.cpp and
the captured physical.cpp (now operations.cpp). All selected callers, callbacks,
controls and checkers are freshly built. Each pair runs before/after/after/before,
three sequential 30 ms repetitions per block, pinned CPU0. All-width H0 get16
uses 4 KiB requested encoded extent, 64 KiB of indices, original origins aligned
to 16 and sixteen u64 outputs per query. Selected bulk controls cover widths
1/6/8/16/17/23/56/64 at legal heads and carriers, with 8,192 values per call.
These are footprint descriptions, not cold-cache measurements.

Both variants pass operations, public physical oracle, exact guarded ranges and
admitted-region checks. The [integration record](evidence/scalar-callback-20260910/integration.json)
also verifies the exact candidate against the renamed live files and retains
local scalar/NEON checks. The live tree additionally contains independently
validated Local2/Local4 changes; this pair isolates the callback source change.

| Ordinary native get16 | Cases improving | Median after/before range |
| --- | ---: | ---: |
| Zen, full-feature x86 profile | 152/152 | .238–.774 |
| GNR, full-feature x86 profile | 152/152 | .340–.860 |
| V2, NEON | 68/76 | .912–1.018 |

Both x86 profiles include AVX2 and AVX-512 endpoint families with the profile's
extra features available. Every x86 improvement separates its six before/after
samples and agrees at both ordering edges. Family names do not enforce an
instruction ceiling. The V2 range includes real losses; it is not an acceptance
band or a claim that changes below a chosen percentage are irrelevant.

| Target / shape | Before | After |
| --- | ---: | ---: |
| Zen AVX-512 Local1 | 8.704 | 2.538 |
| Zen AVX-512 Local7 | 9.608 | 2.751 |
| Zen AVX-512 Local56 | 10.465 | 2.915 |
| GNR AVX2 Local1 | 10.293 | 3.676 |
| GNR AVX-512 Local1 | 13.424 | 7.292 |
| GNR AVX-512 Local56 | 13.440 | 7.105 |
| V2 Local1 | 4.891 | 4.805 |
| V2 Local56 | 4.813 | 4.449 |

Numbers are pooled median ns/query. The actual x86 query loop removes the
outgoing aggregate copy and changes from 23 to 18 instructions; AArch64 changes
from 21 to 20. Both keep one indirect call, range coordinates, stores and checksum.
This establishes a changed calling sequence and its paired timing effect. It
does not isolate a particular store-forwarding mechanism or make every timing
difference an additive ABI cost.

## Remaining losses and context

Zen's scalarized AVX-512 Striped5 remains 6.049 ns/query against its matched
predecessor 2.305 (2.625×); Striped1 remains 2.254× its predecessor. The admitted
and raw controls retain stronger facts and still expose further opportunities.
The ordinary arbitrary-range obligation remains, including runtime lengths,
misaligned origins, narrower sinks, heads and independent source strides.

V2 Local50 increases 7.812→7.950 ns/query (1.78%), Local42 increases 1.27%, and
Local28 increases 1.17%, with separated samples. Its Local6/u64 bulk decode
increases .35581→.37095 ns/value (4.26%), and Local56/u64 decode increases 4.01%.
These remain unresolved. Selected native bulk cases with separated losses
number 13 on Zen, 5 on GNR and 10 on V2; every affected row remains in the control
tables. Encoding is unchanged in source but also shows context movement.

On Zen, Local6/u8 bulk decode halves in time while the selected complete decoder
retains identical normalized instructions, register operands, internal branches
and every referenced constant byte. The same identity holds for the selected
K23/H16/u32 complete decoders despite their 2.36–3.29% losses. This does not
establish that scalarizing the callback repaired the old grain/unroll problem.
The surrounding caller and linked placement remain part of the experiment.
Unchanged admitted/raw/predecessor controls also move, sometimes by 14–16%.

The next physical work therefore keeps the ordinary runtime-range driver and
an expression-consuming materializer connected, with useful exact regions and
typed output. The implemented authoring core preserves expression bit-domain,
actual child references, original coordinates and native selection; the dynamic
driver has not yet adopted it. Caller probes must use identical runtime queries
and admissions, rather than borrowing the fixed-16 diagnostic's stronger facts.

The subsequent [runtime-range probe](../../seriespack-range-execution/runtime.md) tests those
variable-query obligations. The separate [GNR store comparison](../../seriespack-range-execution/short-stores.md)
retains ZMM reconstruction while removing the short materialization floor in
the affected diagnostic callbacks; ordinary-driver integration remains work.
