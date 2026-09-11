# V2 Scan4/6 grain result

The runtime-count, three-extent comparison supports a bounded Scan4/u8
256-value dense-region candidate for encode and decode. It does not support
applying the same change to Scan6. No production change is selected by this
receipt; the next Scan4 candidate needs actual public before/after validation
and measurement, including smaller-array and exact-remainder behavior.

All 96 cases completed with five sequential 50 ms repetitions, pinned CPU0,
Neoverse V2, Clang 21.1.8 O3, `-march=armv8-a -mtune=neoverse-v2`.
The independent ASan/UBSan checker passed 95,232 wire, projection, exact extent,
offset and guard checks over all four carriers. Every compared arm within a
width/operation/extent used the same input/output addresses; recorded page
offsets agree. The three extents specify footprints, not cache residency.

The [compact case table](evidence/v2-20260910/cases.csv) retains all 480 raw
repetitions in 96 rows, their medians and actual buffer counters. The
[provenance](evidence/v2-20260910/provenance.json) records input identity and
host context; the [artifact reference](evidence/v2-20260910/artifact.json)
recovers the full worker run, executable, assembly and source/build receipts.
This table is generated with the shared `workbench/tools/evidence.py summarize`
helper, with the five counters named in its provenance.

Numbers below are median CPU ns/value. The final column compares the native
256-value region against the default raw native array path; it does not compare
against the bound endpoint.

| Width | Operation | Values | Old bound | Native | Region256 | Predecessor256 | Region change |
|---|---|---:|---:|---:|---:|---:|---:|
| 4 | encode | 256 | 0.02623 | 0.01382 | 0.01189 | 0.01174 | -14.0% |
| 4 | encode | 8,192 | 0.01412 | 0.01393 | 0.01062 | 0.01063 | -23.7% |
| 4 | encode | 65,536 | 0.01566 | 0.01546 | 0.01351 | 0.01350 | -12.6% |
| 4 | decode | 256 | 0.03467 | 0.01959 | 0.01559 | 0.01552 | -20.4% |
| 4 | decode | 8,192 | 0.02033 | 0.01703 | 0.01580 | 0.01569 | -7.2% |
| 4 | decode | 65,536 | 0.02013 | 0.01783 | 0.01704 | 0.01764 | -4.4% |
| 6 | encode | 256 | 0.03624 | 0.02179 | 0.02098 | 0.02045 | -3.7% |
| 6 | encode | 8,192 | 0.02114 | 0.01832 | 0.01944 | 0.01942 | +6.2% |
| 6 | encode | 65,536 | 0.02148 | 0.01870 | 0.01998 | 0.02001 | +6.8% |
| 6 | decode | 256 | 0.04032 | 0.02239 | 0.02235 | 0.02421 | -0.2% |
| 6 | decode | 8,192 | 0.02210 | 0.01915 | 0.01882 | 0.02240 | -1.7% |
| 6 | decode | 65,536 | 0.02258 | 0.02376 | 0.02375 | 0.02273 | -0.1% |

## Scan4: the useful grain is 256

Encode improves by 14.0%, 23.7% and 12.6% across the three extents with
Region256. Region128 helps but recovers less. Region512 gains only another
0.2% at 8,192 and 1.4% at 65,536, while losing 5.1% against Region256 at 256
values. That does not justify the larger region. Region256 and Predecessor256
encode agree within 1.4% everywhere, and within 0.1% at the two larger extents.

Decode improves by 20.4%, 7.3% and 4.4% with Region256. Region128 is slower
than the default native path at both larger extents; Region512 is worse than
Region256 at every extent and slightly worse than native at 65,536. The proposed
256-value preset therefore has an extent-spanning signal for both operations,
rather than a blanket request to increase unrolling. Its physical work remains
four existing 64-value Scan4 tiles: no wire or tile-contract change is needed.

The older fixed-8,192 run found the same encode/decode direction, but its count
was propagated into the noinline arms. This run keeps count runtime-visible and
adds smaller/larger arrays, making the narrower conclusion better supported.
The original cross-fixture 40% Scan4 decode ratio was not a 40% raw leaf loss;
here the default native/Predecessor256 ratios are 1.262, 1.086 and 1.011.

## Scan6: retain the current encoder; decode is unresolved

The old fixed-count encode result does not generalize. Region256 saves 3.8%
at 256 values but costs 6.2% and 6.8% at 8,192 and 65,536. Default raw native
encode is already 5.7% and 6.5% faster than Predecessor256 at those larger
extents. Region128 and Region512 also lose there. This run gives no reason to
replace the current Scan6 bulk encoder with a grouped preset.

Decode Region256 is effectively unchanged at 256 and 65,536, and 1.7% better
at 8,192. Region512 has similarly small gains. Region128 wins 7.9% at 65,536
but loses 6.2% and 10.4% at the smaller extents; selecting it generally would
trade away measured behavior. Default native decode beats Predecessor256 by
7.5% and 14.5% at the first two extents, then loses 4.5% at the largest. That
remaining large-array difference is recorded, not explained away by the smaller
wins. A cache or loop-context explanation would need additional evidence.

The two deliberately identical Scan6 predecessor_tile/predecessor128 functions
agree within 0.21% in every case. The sample variation is usually small; the
largest CV is 4.54% for Scan4 Region128 decode at 256 values, and the next is
2.69% for Scan4 Predecessor256 decode at 8,192. Neither drives the recommended
Scan4 Region256 selection or reverses the Scan6 encoder regressions.

## Bound-interface and provenance limits

The bound arm deliberately links old library `2807a57e`, restored and verified
from retained artifact `038da287`. Raw physical arms and current benchmark/control
objects use coherent header `24ca6129` and captured source `335ab31e`. The old
bound source digest is `edd9ace9`. Full hashes and receipts accompany the run.
The bound arm does not measure the newer coherent range implementation.

At 256 values its fixed interface/placement work is substantial. At 8,192,
Scan6 encode is .021145 through the old bound endpoint versus .018316 through
the current raw entry, despite the raw entry beating its immediate predecessor.
These observations distinguish whole-operation cost from leaf/grain cost; they
do not isolate a single causal overhead. Indeed the old bound Scan6 decoder is
faster than the current raw entry at 65,536, so a uniform additive boundary-cost
model would be misleading. No broader carrier, head, stride or decode policy
follows from these u8 dense-region measurements.
