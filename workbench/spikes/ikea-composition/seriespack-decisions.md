# Initial SeriesPack inclusion decisions

Recorded at the initial implementation handoff, 2026-09-10. The table and
opportunities below preserve the evidence available then, including work that
has since been implemented. They are historical rationale, not current status. The [implementation measurements](../../benchmarks/seriespack/measurements.md)
track its controls, target profiles and performance investigations; semantic
coverage does not establish competitive performance.
Reconsider them when a real consumer supplies stronger evidence; there is no
requirement to preserve every experimental switch in the production module.

| Mechanism | Initial decision | Reason and evidence limit |
| --- | --- | --- |
| Local8 body/tail packets; narrow LocalPack and current ScanPack wire | **Include** | Complete baseline geometry and direct primitive equivalence; measured narrow kernels and width-56 body provide implementation examples |
| Separate 8/16-bit heads and explicit tile stride | **Include** | Needed for filtering and embedding without making placement/meaning implicit; full operation costs still include heads and gaps |
| Body64/Scan4 at `w=12` | **Include** | Exercised in native composition; only one of the two equivalent-width parent placements is selected here |
| Body64/Scan4/body64 at `w=20` | **Include, borderline** | Reuses the same Scan4 mechanism and a small chunk map; proved geometry, but no wider native timing. Adds a useful second whole-byte body width without a new tail algorithm |
| Interleaved Scan2/6/7 at `w=10/14/15` | **Include in ARM recipe, borderline** | Positive locality witnesses and strong narrow ARM stripe economics justify a limited attempt. Complete body/join performance is unmeasured; large periods and mapping code are costs |
| Arithmetic and constant-offset Scan5/7 point/group readers | **Include both** | Material small-versus-large access tradeoff on unchanged bytes; approximately doubled reader text is justified here. Bind explicitly, no cache classifier |
| Balanced packing and ARM shift-insert reconstruction from the bit map | **Include** | Shared algebra and substantial narrow ARM improvements; do not recreate a generic ISA abstraction around the bodies |
| Compact AoS byte expansion, including irregular 3/5/6/7-byte bodies | **Include** | Needed for full-width coverage and locality; width 56 is measured, other native widths still require implementation/checks |
| Optional SVE2 bit-permutation helpers | **Evaluate as small target-local specializations** | Complete NEON remains required. Retained V2 LocalPack paths use BGRP/BEXT; those timings cannot establish NEON-only speed or isolate the helpers' contribution. Compare enabled/disabled paths on unchanged bytes before choosing |
| 32-value width-56 AVX-512 encoder region | **Include for dense complete regions** | Measured 6–9% encode improvement with cycles corroboration; captured endpoint adds 167 text bytes and 192 table bytes. Same wire and shared encoder region, not another public preset. Requires BW+VBMI; packet kernels handle boundaries and gaps |
| Register-mask Scan5/7 encoder control | **Reassess by width and target** | Captured Scan7 Zen encode improves from 2.19 to 2.06 ns/256 across separate runs, with a 48-byte code increase; Scan5 does not benefit there. Carry a focused comparison into implementation, not a blanket mode or a claim that Zen parity is solved |
| Paired 64-byte AVX-512 stripe execution | **Reassess encoder and compatible native consumers separately** | Measured Scan4 Zen encode gains about 9%, while the materializing decoder loses about 18%. Keep 32-byte storage stripes; a consumer accepting the paired lane map may avoid that decoder's rearrangement. No blanket paired default |
| Historical permuted Scan5/7 wire; GFNI Scan decoder experiment | **Exclude** | Additional wire/implementation variants without a compelling overall case. This does not exclude GFNI use for LocalPack transposes |
| Arbitrary chunk permutations, optimal padded supertiles, global body planes | **Exclude from presets** | Extra format/placement space without enough benefit for this scope; padding and separated ordinary body planes also change locality/storage contracts |
| General graph matcher, universal native carrier, per-node CPS switches | **Exclude** | Probe scaffolding is not the production authoring model; shared functions and chosen regions are sufficient to start |

The [narrow measurements](probes/ikea-integers/measurements.md)
support these distinctions. Current continuous Scan5/7 has strong ARM results,
near-parity GNR comparisons and remaining Zen gaps; historical permuted-wire
parity must not be used to claim those gaps are gone. The optional
[width-56 encoder](probes/ikea-integers/wide56/README.md#a-larger-encoding-region-over-unchanged-packets)
and [locality placements](probes/ikea-integers/locality/README.md#repairs-for-intact-32-byte-interleaving)
retain the evidence for the original borderline decisions. Those decisions
preceded the production implementation's hardware measurements.

## Performance questions at the initial handoff

Judge an implementation at its actual substitution scope: operation, width/body
family, target features/tuning, placement and region. A small same-wire body
specialization does not have the cost of another public geometry or the full
product of consumer combinations. Conversely, a short source expression can
instantiate substantial code. Record generated text and constant-table bytes,
incremental compilation cost, access widths and runtime under matched work;
neither an instruction count nor a fixed percentage cutoff decides marginality.
The width-56 region's measured size delta is evidence for that endpoint, not a
size guarantee for the eventual arbitrary-length implementation.

Full-width work should extend the shared byte expansion/packing mechanisms for
body sizes 1..8 and compose tails/heads around them. Native lane widths follow
the consumer's required range; widen to u64 only for that contract. Useful next
optimizations include exact-store regions over several local packets, reuse of
loaded stripes across their logical groups, and native read/compare/consume
without packing and re-expanding a predicate. The old
[12-bit composition](probes/ikea-integers/composition/README.md#real-native-handoff-and-its-cost)
paid that predicate conversion even in its inline arm. Compatible mapped
consumers also make paired stripes worth revisiting. These are opportunities
to test within all-width implementation, not established performance gains.

Separate trusted codec timing from checked validation and binding, while also
measuring their complete outer operations. Compare minimum sufficient scalar
output, u64 materialization and immediate consumers as distinct work. Cover
small/partial arrays, dense bulk, strided embedding, selected work and both
dependent and independent large random reads. Include heads, tile slack and
all actual accesses in their proper totals. The old fixed-256 endpoints cannot
establish arbitrary-boundary performance; large allocations alone cannot prove
cold cache residence. Compare with prior art under matched permissions and
also a tight native control, so prior API overhead does not set the ceiling.
Known Zen Scan5/7 losses remain work to resolve, not an accepted cost budget.

