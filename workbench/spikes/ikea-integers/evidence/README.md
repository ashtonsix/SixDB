# Integer evidence map

[Back to the spike](../README.md). The 48 compact exports below retain the
selected repetitions/checks and exact source identity. Each linked
`provenance.json` names an adjacent `artifact.json` for the full source,
binaries, disassembly and logs. Those bundles, rather than the live checkout,
reproduce a historical implementation. Three additional recovery-only references
are listed separately. No timing campaigns are pooled.

## Current wire and live implementation choices

| Capture family | Zen 5 | Granite Rapids | Neoverse V2 | Interpretation |
| --- | --- | --- | --- | --- |
| Continuous Scan5/7, fragment arithmetic | [5](continuous-zen5-k5/provenance.json) / [7](continuous-zen5-k7/provenance.json) | [5](continuous-granite-rapids-k5/provenance.json) / [7](continuous-granite-rapids-k7/provenance.json) | [5](continuous-neoverse-v2-k5/provenance.json) / [7](continuous-neoverse-v2-k7/provenance.json) | [Current reader and bulk findings](../measurements.md#two-readers-over-the-continuous-wire) |
| Same wire, constant-offset readers | [5](dispatch-zen5-k5/provenance.json) / [7](dispatch-zen5-k7/provenance.json) | [5](dispatch-granite-rapids-k5/provenance.json) / [7](dispatch-granite-rapids-k7/provenance.json) | [5](dispatch-neoverse-v2-k5/provenance.json) / [7](dispatch-neoverse-v2-k7/provenance.json) | Explicit execution alternative; unchanged stored bytes |
| Scan6 fragment-count reader | [run](scan6-zen5/provenance.json) | [run](scan6-granite-rapids/provenance.json) | [run](scan6-neoverse-v2/provenance.json) | [Small-read win, mixed capacity result](../measurements.md#six-bit-reader-refinement) |
| Optional register-mask encoder | [run](register-masks-zen5/provenance.json) | [run](register-masks-granite-rapids/provenance.json) | — | [Partial Zen improvement](../measurements.md#continuous-wire-bulk-and-register-masks); off by default |
| 12-bit composition and paired stripes | [run](composition-zen5/provenance.json) | [run](composition-granite-rapids/provenance.json) | [run](composition-neoverse-v2/provenance.json) | [Inline/CPS](../composition/README.md#checks-and-measurement), [paired-stripe execution](../measurements.md#paired-stripe-execution); this earlier source still uses permuted narrow Scan5/7 |
| Width 56, native plane control | [run](../wide56/evidence/native-planes-zen5/provenance.json) | [run](../wide56/evidence/native-planes-granite-rapids/provenance.json) | [run](../wide56/evidence/native-planes-neoverse-v2/provenance.json) | [Final wider-body comparison](../wide56/README.md#hardware-findings-2026-09-10) |
| Width 56, optional 32-value encode region | [run](../wide56/evidence/encode32-zen5/provenance.json) | [run](../wide56/evidence/encode32-granite-rapids/provenance.json) | — | [Same-wire larger encoding region](../wide56/README.md#a-larger-encoding-region-over-unchanged-packets); off by default |

“Current” identifies a still-relevant wire or implementation choice. It does
not mean all captures used today's entire source tree. Consult each receipt,
the [instruction audit](../access-audit.md), and the validation captures below.

## Validation and geometry

| Scope | Captures | Meaning |
| --- | --- | --- |
| Final narrow, fragment reader | [Native ARM sanitizer](final-native-fragments/provenance.json), [AVX2/QEMU](final-avx2-fragments/provenance.json) | Exact values, wire and access checks; no hardware speed claim |
| Final narrow, constant-offset reader | [Native ARM sanitizer](final-native-offsets/provenance.json), [AVX2/QEMU](final-avx2-offsets/provenance.json) | Same checks for the other reader |
| Width 56 | [Native ARM sanitizer](../wide56/evidence/native-sanitize/provenance.json), [AVX2/QEMU](../wide56/evidence/avx2-qemu/provenance.json) | Kernel and four-arm comparator checks; both AVX-512 encoder variants are additionally checked in their hardware captures |
| Complete locality audit | [Continuous-wire audit](../locality/continuous-evidence/provenance.json) | 862 layout/width cases, including wider geometry; [39 selected cases](../locality/continuous-evidence/cases.csv), no timing |

## Historical and discarded alternatives

| Capture family | Evidence | Why retain it |
| --- | --- | --- |
| Original seven-width bulk campaign | [Zen](bulk-zen5/provenance.json), [GNR](bulk-granite-rapids/provenance.json), [V2](bulk-neoverse-v2/provenance.json) | [Permuted Scan5/7 bulk findings](../measurements.md#resident-encodedecode-permuted-57-bit-repair); includes LocalPack and other Scan widths. Not current continuous-wire parity. |
| Permuted Scan5/7 capacity | [Zen](permuted-capacity-zen5/provenance.json), [GNR](permuted-capacity-granite-rapids/provenance.json), [V2](permuted-capacity-neoverse-v2/provenance.json) | Legal locality still lost large independent-read throughput; [motivation for the continuous repair](../measurements.md#capacity-locality-legality-did-not-settle-the-wire-choice) |
| Removed GFNI decoder control | Zen [5](gfni-decode-zen5-k5/provenance.json) / [7](gfni-decode-zen5-k7/provenance.json); GNR [5](gfni-decode-granite-rapids-k5/provenance.json) / [7](gfni-decode-granite-rapids-k7/provenance.json) | [Negative result](../measurements.md#discarded-gfni-decoder); its source and historical `--scan-affine` option survive in the bundles |
| Earlier narrow validation | [Native ARM sanitizer](check-native-sanitize/provenance.json), [AVX2/QEMU](check-avx2-qemu/provenance.json) | Checks of the permuted-wire revision, superseded by the final checks above |
| Original locality audit | [Earlier 37-case selection](../locality/evidence/provenance.json) | Before the continuous-wire extension; current proof is the 39-case selection above |
| Width 56, scalar-loop plane get16 control | [Zen](../wide56/evidence/auto-planes-zen5/provenance.json), [GNR](../wide56/evidence/auto-planes-granite-rapids/provenance.json), [V2](../wide56/evidence/auto-planes-neoverse-v2/provenance.json) | [Control repair and unresolved Zen prior-sum drift](../wide56/README.md#why-the-first-control-was-tightened); not the final speedup claim |

The initial capacity run receipts/raw results also have recovery-only pointers:
[Zen](initial-capacity-zen5.json), [GNR](initial-capacity-granite-rapids.json),
[V2](initial-capacity-neoverse-v2.json). These are full file bundles without a
compact timing export; they are not three additional final timing samples.

## Verify, report and recover

From Linux (prefix `orb -m ubuntu` in this macOS workspace):

```sh
python3 workbench/tools/artifacts.py verify workbench/spikes/ikea-integers
python3 workbench/spikes/ikea-integers/report.py workbench/spikes/ikea-integers/evidence/continuous-zen5-k7
python3 workbench/spikes/ikea-integers/wide56/report.py workbench/spikes/ikea-integers/wide56/evidence/native-planes-zen5
python3 workbench/tools/artifacts.py fetch workbench/spikes/ikea-integers/evidence/continuous-zen5-k7 build/recovered/ikea-integers-scan7
```

Verification checks local evidence and bundle references without network access.
Reports read the retained repetitions offline; they are not new experiments.
The narrow report prints CSV; the width-56 report regenerates `summary.csv` and
`summary.md` in the supplied directory.
Only `fetch` downloads the full bundle and shared inputs. Run the captured
source with its recorded toolchain/flags to reproduce a historical result;
current runner options need not exist in older revisions or vice versa.
[Retention and recovery](../../../tools/artifacts.md) explains the shared tools.
