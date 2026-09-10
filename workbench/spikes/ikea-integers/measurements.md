# Packed integer measurements

[Back to the spike](README.md). This page leads with the current narrow wire
and implementations. The [benchmark contract](bench.md) defines matched work;
[width-56 measurements](wide56/README.md#hardware-findings-2026-09-10) use a separate
u64 harness. Captures are independent runs, never pooled across source revisions.
The [evidence map](evidence/README.md) distinguishes current choices, optional
controls, superseded wires and validation-only exports.

| Read first | Main result |
| --- | --- |
| [Continuous Scan5/7 readers](#two-readers-over-the-continuous-wire) | Fragment arithmetic wins small get16; constant-offset leaves recover large independent-read throughput. No universal winner. |
| [Current bulk costs](#continuous-wire-bulk-and-register-masks) | ARM gains and near-parity GNR; unresolved Zen encode/decode gaps. Register masks are an explicit partial improvement. |
| [Scan6](#six-bit-reader-refinement) | Better small get16; mixed large-read results. |
| [Paired stripes](#paired-stripe-execution) | Wider AVX-512 execution is not automatically faster. |
| [12-bit composition](composition/README.md#checks-and-measurement) | Native reuse works, but blanket CPS's 16–57% cost is rejected. |
| [Width 56](wide56/README.md#hardware-findings-2026-09-10) | Adjacent bodies improve large reads over native planes; native sum and an optional larger encode region exercise different grains. |

Historical near-parity results for the permuted Scan5/7 wire below are **not**
a claim of performance parity for the current continuous wire. Current validation
is linked from the [run guide](bench.md#validation); timing captures identify
their own exact sources.

## Two readers over the continuous wire

The continuous repair keeps every 5/7-bit value in at most two adjacent
32-byte fragments. The first reader selects fragment classes and computes
their addresses, shifts and masks arithmetically. The second dispatches to
constant-offset branch bodies, generated from the same bit map. Both perform
only the required payload loads. The [instruction audit](access-audit.md)
checks their measured bodies, including folded memory operands.

The focused campaigns use widths 5 and 7, five sequential repetitions, the
same 262,144-value bulk workload, and capacity extents of 1,024 and 2^30 values.
The two reader campaigns ran on the same workers, with separately captured
sources and independent repeated prior controls. They are not pooled samples
or a simultaneous paired timing. Each selected/prior pair below is within
one run; units are ns per request, and the width is 7 bits:

| Target | Small-extent get16, arithmetic / prior | Large independent point, constant-offset / prior | Large get16, constant-offset / prior |
| --- | ---: | ---: | ---: |
| Zen 5 | 4.12 / 9.03 | 21.10 / 21.35 | 15.97 / 16.19 |
| Granite Rapids | 5.15 / 10.46 | 20.26 / 22.38 | 20.89 / 21.47 |
| Neoverse V2 | 4.50 / 7.42 | 24.53 / 25.26 | 31.92 / 32.26 |

The constant-offset reader largely removes the large-extent throughput loss.
For width 5, large independent point medians are 20.18/19.94 ns on Zen,
18.52/19.98 ns on Granite Rapids, and 21.57/21.89 ns on V2. Large get16 is also
near or faster than the prior on all three targets. Dependent point reads are
not a universal win: constant-offset Scan7 is about 3% slower than prior on Zen.

It is also a poor default for hot data. Constant-offset Scan7 get16 costs
9.11/9.04 ns versus prior on Zen, 10.41/10.50 ns on Granite Rapids and
9.16/7.42 ns on V2, losing the arithmetic reader's substantial gains. The x86
implementation has an eight-entry jump table; V2 uses a balanced branch tree.
Both replace runtime fragment arithmetic with fixed leaf operations and
roughly double the emitted reader size. This experiment does not isolate branch
prediction, address readiness and instruction footprint as separate causes.

The useful composition result is an explicit **reader choice for unchanged
data**. `ScanReader` makes both implementations available; the runner chooses
the opaque endpoint before timing. There is no automatic cache classifier,
no data migration and no implied single fastest reader. A parent placement's
eligibility does not change with this execution choice.

The [evidence map](evidence/README.md#current-wire-and-live-implementation-choices)
links all six arithmetic and six constant-offset captures, with their checks and
source/assembly bundles.

## Continuous-wire bulk and register masks

The continuous wire also permits the ARM bulk decoder to assemble low
fragments first and insert normalized high fragments with SLI. This is derived
from the bit map; it removes a final mask when the last physical fragment ends
at bit 7. Arithmetic-campaign bulk medians per 256 values are:

| Target | Encode5 / prior | Decode5 / prior | Encode7 / prior | Decode7 / prior |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 | 2.04 / 1.86 | 1.86 / 1.85 | 2.19 / 1.95 | 2.21 / 1.88 |
| Granite Rapids | 4.58 / 4.48 | 5.24 / 5.30 | 5.31 / 5.26 | 5.55 / 5.61 |
| Neoverse V2 | 6.15 / 6.43 | 6.40 / 9.69 | 7.01 / 9.43 | 7.26 / 10.47 |

This improves ARM bulk and preserves Granite Rapids performance, but exposes
material Zen losses: 10–13% in encode and 17% in 7-bit decode, also visible in
cycles. These are unresolved costs of this captured lowering, not an accepted
budget. Fewer emitted instructions did not ensure faster execution; continuous
fields require more distinct x86 mask constants than the permuted wire.

The removed GFNI decoder control is recorded in [historical experiments](#discarded-gfni-decoder).

The register-mask encoder control directly tests the constant-load tradeoff.
It materializes each mask in a scalar register and broadcasts it, removing all
seven mask loads while adding fourteen instructions and 48 code bytes. Payload
loads/stores remain unchanged, and there are no spills. On Zen, encode5 is
2.07/1.87 ns and encode7 is 2.06/1.96 ns versus prior: the 7-bit gap narrows to
5%, while the 5-bit loss remains about 11%. Granite Rapids gives 4.38/4.54 ns
and 5.24/5.22 ns. The control remains explicit as `--scan-register-masks`;
it does not improve every case or remove the remaining Zen decode cost.
The [Zen](evidence/register-masks-zen5/provenance.json) and
[Granite Rapids](evidence/register-masks-granite-rapids/provenance.json) runs
retain the full seven-width bulk control and source/assembly.

## Six-bit reader refinement

The unchanged 6-bit wire needs either one edge fragment or that edge plus the
middle fragment. One direct branch and a small shared scalar/vector algebra
replace its four-way dispatch. At 1,024 values, selected/prior get16 medians
are 5.63/7.22 ns on Zen, 6.93/8.21 ns on Granite Rapids and 5.72/6.72 ns on V2.
The large-extent result is mixed: Granite Rapids independent points are
24.26/22.64 ns, while dependent points improve on all three targets. Bulk and
stored bytes are unchanged. The explicit constant-offset alternative remains
available; no universal reader winner is claimed.

The [Scan6 captures](evidence/README.md#current-wire-and-live-implementation-choices)
retain all 120 capacity repetitions per target and their full checks/assembly.

## Paired-stripe execution

[scan_pairs.h](scan_pairs.h) pairs unchanged 32-byte stripes into a 64-byte
AVX-512 execution grain for widths 1/2/4. This is a kernel choice, not a data
format, and is not selected automatically. In the retained resident comparison,
Zen's 4-bit encoder improves from 3.56 to 3.24 ns per 512 values while its
decoder slows from 3.74 to 4.41 ns. Granite Rapids mostly gains nothing. Extra
lane rearrangement can cost more than wider operations save. The repetitions
are in the composition captures: [Zen 5](evidence/composition-zen5/pairs-timings.csv),
[Granite Rapids](evidence/composition-granite-rapids/pairs-timings.csv), and
[Neoverse V2](evidence/composition-neoverse-v2/pairs-timings.csv).

## Historical experiments

These captures explain why the current choices changed. Their source bundles
remain recoverable; a later code or control change does not retroactively change
their interpretation. The original bulk campaign also covers LocalPack and the
Scan widths whose wires did not change. Consult subsequent captures for changed
reader implementations.

## Resident encode/decode: permuted 5/7-bit repair

The retained bulk campaign uses 262,144 logical values, matched opaque 256-value
calls and five sequential repetitions. Input plus output occupies 288–480 KiB
for packed representations and 512 KiB for plain bytes. The recorded private
L2 sizes are 1 MiB on Zen 5 and 2 MiB on Granite Rapids and Neoverse V2. Each case
gets a full warm pass; no buffer flush or hardware cache-residence assertion is
part of this comparison.

The table gives the range across widths 1–7 of **prior time / selected time**.
A ratio above one favours the selected kernel. It does not average away a
slower width.

| Target and layout | Decode ratio range | Encode ratio range |
| --- | ---: | ---: |
| Zen 5 LocalPack | 0.949–1.007 | 0.977–1.059 |
| Zen 5 ScanPack | 0.985–1.098 | 0.980–1.086 |
| Granite Rapids LocalPack | 0.994–1.003 | 0.955–1.006 |
| Granite Rapids ScanPack | 0.986–1.011 | 0.984–1.008 |
| Neoverse V2 LocalPack | 1.029–1.999 | 0.966–1.242 |
| Neoverse V2 ScanPack | 1.004–1.429 | 0.999–1.298 |

The x86 implementations are near the prior. The largest measured LocalPack
losses are 5.4% for Zen 5 7-bit decode and 4.7% for Granite Rapids 4-bit encode.
ARM LocalPack decode improves by 3–100%; its 1-bit encode remains 3.6% slower.
ScanPack's ARM decode improves by up to 43%, and encode by up to 30%. These
results preserve exact packing and the permuted 5/7-bit locality repair.

The initial ScanPack implementation had material regressions, including a
serial 1-bit packing chain and redundant masks around non-power field merges.
Balanced packing and map-derived destination field order removed those costs.
For example, the V2 3-bit encoder fell from 7.33 to 4.98 ns per 256 values; its
same-run prior is 5.89 ns. Final 5- and 7-bit encode are 6.25 and 7.23 ns versus
6.43 and 9.39 ns for their prior comparands. The different repaired wire is
explicit in those comparisons.

Wall time and cycles do not always move together on these workers. Granite
Rapids 4-bit LocalPack encode is slower in ns while using slightly fewer cycles
per call than the prior in the same run. Zen 5 7-bit decode is slower in both.
CPU pinning does not hold frequency fixed. Small per-width differences need
their cycles, instruction counts and generated bodies considered before a
causal claim; different campaigns are not pooled.

The [Zen 5](evidence/bulk-zen5/provenance.json),
[Granite Rapids](evidence/bulk-granite-rapids/provenance.json) and
[Neoverse V2](evidence/bulk-neoverse-v2/provenance.json) exports retain all 350
timing repetitions per target, checks, compiler and cache context. Their bundle
references recover measured source and complete assembly. The earlier resident
source is recoverable through the corresponding
[composition campaign](composition/README.md#checks-and-measurement); it is
historical evidence, not a second sample of the final implementation.

```sh
python3 workbench/spikes/ikea-integers/report.py \
  workbench/spikes/ikea-integers/evidence/bulk-zen5
```

This verifies the compact export and prints medians, min/max, work counts and
matched prior/plain ratios without downloading the full bundle.

## Capacity: locality legality did not settle the wire choice

The next campaign measured dependent points, eight independent point requests
per loop, and aligned groups of sixteen at equal logical counts of 2^10, 2^20
and 2^30. It uses three sequential repetitions of 1,048,576 requests, with a
fresh trace seed per repetition. Each packed arm has one prefaulted allocation;
trace replay, output validation and the required-byte line census occur after
timing. See [bench.md](bench.md) for the dependency chain and control arms.

Removing the eight-way dispatch from the permuted Scan7 reader improved its
small-extent get16 result. At 1,024 values, selected/prior times were
7.39/9.15 ns on Zen 5, 8.20/10.50 ns on Granite Rapids and 6.74/7.42 ns on V2.
The same reader still lost on large-extent independent reads. At 2^30 values
(896 MiB of 7-bit payload), medians in ns per request were:

| Target | Dependent point, permuted / prior | Independent point, permuted / prior | Aligned get16, permuted / prior |
| --- | ---: | ---: | ---: |
| Zen 5 | 177.90 / 177.70 | 25.33 / 21.47 | 19.65 / 15.67 |
| Granite Rapids | 198.52 / 203.88 | 26.52 / 23.15 | 23.58 / 21.86 |
| Neoverse V2 | 175.74 / 184.00 | 30.14 / 25.16 | 32.30 / 31.07 |

The permuted wire meets the hard two-adjacent-line bound. Both seven-bit repairs
also average 1.75 required bytes per point. Neither fact predicts these timings.
The permuted reader has dynamic edge/middle addressing and up to three
fragments, whereas the prior's branch leaves use constant fragment offsets.
That is an observed code difference, not an established cause of the cold
throughput loss. It motivated measuring the continuous repair, which needs at
most two fragments and admits additional surveyed body placements.

The compact [Zen 5](evidence/permuted-capacity-zen5/provenance.json),
[Granite Rapids](evidence/permuted-capacity-granite-rapids/provenance.json) and
[V2](evidence/permuted-capacity-neoverse-v2/provenance.json) exports retain every
5/7-bit selected/prior ScanPack repetition at the smallest and largest extents.
The full six-arm, seven-width sweep remains in their recoverable bundles.

These are allocation/trace regimes, not certified cache-residence states. All
rows deliberately say `residence=unestablished`. The largest allocations exceed
the recorded cache capacities, but generic cache misses are not LLC or DRAM
transaction counts. In particular, Granite Rapids returned zero for this
generic event throughout the campaign; it is uninformative. The captured page
fault counts have no major faults and at most three minor faults per timed
case. Separate fixed-payload runs used budgets of 512 MiB on Zen/V2 and 1 GiB
on Granite Rapids; those arms have different logical counts, and are not pooled
with the equal-count comparison above.

## Discarded GFNI decoder

A GFNI control replaced first-fragment shift/AND pairs with byte-affine
operations, reducing both decoder bodies by eight instructions. Actual hardware
correctness passed, but Zen decode5/decode7 still cost 2.01/2.24 ns versus
1.87/1.88 ns for the prior; Granite Rapids remained near parity. The distinct
mask operands remained, and fewer instructions again did not improve throughput.
The control was removed from live code. Its selected decode repetitions and
complete source are retained for Zen [5](evidence/gfni-decode-zen5-k5/provenance.json)
and [7](evidence/gfni-decode-zen5-k7/provenance.json), and Granite Rapids
[5](evidence/gfni-decode-granite-rapids-k5/provenance.json) and
[7](evidence/gfni-decode-granite-rapids-k7/provenance.json). Restoring those source
bundles restores the historical runner's `--scan-affine` control too.
