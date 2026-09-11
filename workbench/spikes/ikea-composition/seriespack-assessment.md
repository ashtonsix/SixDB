# Predecessor SeriesPack delivery assessment

This is the September 11 assessment of the retired implementation. Its
[source and baseline evidence](seriespack-predecessor/README.md) remain recoverable;
[Ikea](../../../ikea/README.md) describes the replacement.

The first unsigned SeriesPack implementation is a usable composition
component with complete physical and public-operation coverage across the
promised feature profiles. Its cost is not uniformly superior: short ordinary
materialization, some headed construction, ARM narrow geometry and several
compound consumers still have material losses. The campaign separates those
limits from useful retained corrections and from uninstalled experiments.
The seven-profile reconciliation, independent composition review and final
ordinary-store follow-up are complete. This closes the initial delivery
assessment with the significant limitations below; it is not a claim of
performance dominance or a candidate-promotion decision.

## What was delivered

The component supplies unsigned widths 1–64 and 206 legal descriptions,
versioned actual formats, empty/partial/large lengths, independent payload/head
placements, checked construction/reads/selected updates, reusable trusted
readers/encoders and issued-write byte coverage. Ordinary bound decode accepts
arbitrary ranges. Views borrow byte owners; bound endpoints borrow their views'
resources. Native expressions also borrow their named source-view objects,
whose lifetime must cover execution.

Native tile/dense/grouped fragments and consumers support authored functions,
with original coordinates, independent leaf placements and result-domain
operations. Caller-authored drivers own traversal, clipping and retained state.
There is no implemented turnkey general native-range driver, erased/CPS ABI,
Engine schema discovery, signed/float transform layer or Loom integration.
These were separate integration questions, not missing unsigned packed-array
semantics. The [independent scope review](native-regions/evidence/delivery-composition-20260911/scope-review.md)
records that boundary and the public examples. Its local inspection predates
the completed sweep; the pending-hardware paragraph is superseded by the
current seven-profile findings below.

The unchanged current baseline includes the retained Local4 AVX2 transpose,
Local2 NEON coalescing, and scalar trusted decoder/encoder argument corrections.
The [complete current reconciliation](seriespack-predecessor/findings/baseline-20260911.md) now covers
all seven profiles on Zen/GNR/V2:21305 cases and 63915 repetitions, all 206 bulk
descriptions and 76 unheaded access descriptions per profile. Source, compiled
objects, library members, binaries, actual feature applicability and required
checks are audited. All three examples and the additional scalar-x86 aggregate
pass. The [compact baseline evidence](seriespack-predecessor/evidence/delivery-baseline-20260911/summary.md)
links recoverable measured programs, keeps decision witnesses and identifies
the complete repetition table in the verified full analysis bundle.

## What the measurements support

Bulk kernels are generally competitive with the available same-wire controls.
Current Zen Local6/u8 decode is 14.2% slower than its predecessor, substantially
closer than the stale 2.438× observation. V2 Local2/u8 reaches parity after its
retained correction, while Striped4 remains 31.8% slower for encode and 21.4%
for decode. Carrier, physical representation, target family and compiled ISA
ceiling matter; a geometry comparison is not a same-wire kernel comparison.

Ordinary short reads remain a significant weakness. Current Zen full-profile
Striped5 get16 is 5.804 versus 2.758 ns for its same-wire predecessor. Pure AVX2
Striped5 is 3.57× slower. GNR retains roughly 7 ns in several AVX512 simple
get16 calls. V2 Striped7 is 7.181 versus 4.464 ns. The current Zen large-source
pilot keeps point/dependent access competitive but observes get16 around
30–32 ns versus 16–19 ns for the predecessor. Exact query/state and useful
logical-source footprint audits prevent the old short-cycle ambiguity; they
do not establish per-query hardware misses.

H16 construction also has real residual costs. On Zen, Local17/23-H16 u64
encode remains about 1.9× a different Calico plane representation, and the faster measured
Striped17/23-H16 family remains about 2.2×. The joint-head candidate's dense
wins do not erase its gapped/small losses. Repeated selected mutation is
currently scalar read/modify/write, with exact issued-write coverage; there
is no native batched selected writer or all-width mutation-throughput claim.

The [all-profile composition review](native-regions/evidence/delivery-composition-20260911/review.md)
provides a different result from simply assuming fusion repays these costs:

- Basic tile-authored compare-and-sum wins 48 of 144 cases against reused-scratch
  materialization and loses 96. A physical tile is frequently too small a useful
  execution grain on x86.
- Selecting each case's fastest registered native grain/carrier alternative
  yields 110 wins and 34 remaining losses. This is measured alternative selection,
  not automatic dispatch or a proof that the registered set is optimal.
- Full-profile Zen Local12 still loses 1.791× with its best registered AVX512
  alternative, and Local31/full-mask 1.305×. V2's authored form wins 15 of 16;
  headed Local60/full-mask remains a small loss even with its carrier control.
- Authored/direct ratios span .671–1.010. These cases support the authored
  composition boundary without a material wrapper penalty; they do not show
  every body is instruction-identical or competitive with materialization.

The [BEC metadata substitution](../bec-packed-metadata/findings.md) is a concrete useful
consumer integration:175 of 180 whole-consumer cases improve over metadata
materialization, with median ratio .9669, while 45 metadata-refill cases improve
at median .457. Against the specialized existing whole consumer, the median
is essentially parity with small losses retained. This is useful reuse and
avoided materialization in that consumer, not a universal primitive offset.

The design implication is to keep physical geometry, execution grain, working
lanes and result finalization independently expressible. The
[retained-plan notebook](../../notebook/ideas.md#plans-that-keep-improving)
also distinguishes semantic equivalence from cost evidence tied to an actual
caller and executable context. Neither conclusion calls for a width whitelist
or a new mandatory optimization framework.

## Candidate and maintenance judgment

| Investigation | Result and disposition |
| --- | --- |
| Local4 AVX2 / Local2 NEON kernels | Retained earlier; current baseline measures their actual linked results |
| Trusted decoder/encoder scalar arguments | Retained earlier; ordinary operation semantics preserved, but trusted callback signatures changed and custom callbacks must be rebuilt |
| [Canonical point callbacks](call-boundaries/point-binding.md) | Same-algorithm reuse preserves strategy metadata; generic ARM-O2 library text falls 32840 bytes; uninstalled, no runtime claim |
| [Checked get](call-boundaries/checked-get.md) | All 18 checked witnesses improve across three hosts; tiny text reduction; uninstalled, with V2 unchanged-control/layout residual explicit |
| [Ordinary expression materializer](../seriespack-range-execution/integration.md) | Rejected: broad short/suffix losses remain after separation; native text grows 135480–156027 bytes |
| [Clipped striped boundary loop](../seriespack-range-execution/striped-fragments.md) | Rejected:51 of 72 affected cases lose, despite a few aligned wins; adds code/data and another helper |
| [Joint H16 projection](../seriespack-head-projection/findings.md) | Rejected as a common policy: strong dense wins, gapped/small losses, +33715 native text bytes |
| [Ordinary short-region stores on GNR](../seriespack-range-execution/ordinary-stores.md) | Promising small candidate: all 15 affected get16 cases improve 7.6–54%, +1102 text bytes; short scalar-tail regressions and unmeasured all-target costs remain explicit; held in Workbench |

Production library text is approximately 1.93 MB for V2 NEON and 2.58–4.45 MB
across the x86 host/profile builds; actual rodata and benchmark/prior sections
are [separately recorded](seriespack-predecessor/evidence/delivery-baseline-20260911/code-costs.csv).
These are meaningful maintenance costs. The build receipts record actual
build-and-check and benchmark scopes, not a claimed compile-speed delta.
Small source helpers can still instantiate broad code; compact dispatchers
alone did not make the rejected expression integration smaller.

The final store probe confirms a small ordinary implementation seam without
importing the general driver: Local1 get16 falls 7.379 → 3.689 ns and all
15 affected resident cases improve. Short3/last-lane calls still regress
12–18% despite skipping the changed store branches, and unchanged controls
also move. Those are relevant whole-call costs alongside a substantial gain
and small code addition, not an automatic rejection gate. The promising
candidate remains in Workbench under this campaign’s scope; a common
all-target policy is not established.

The checked-get V2 follow-up reinforces that code movement matters: identical
bound instructions slowed after a relink, and restoring placement recovered
most but not all of that loss. It supports a small direct checked operation,
with explicit measured limits; it does not establish a padding policy or a
particular hardware mechanism.

## Overall conclusion

The evidence establishes implemented semantics, useful composition mechanisms,
current whole-call costs and concrete limitations. It does not establish
across-the-board performance dominance or authorize promoting candidates.
The initial unsigned component and this bounded assessment are complete.
The useful next adoption choices are the small point/callback simplifications
and ordinary-store candidate, weighed against their actual caller/target costs.
The larger rejected drivers do not earn their maintenance cost from this evidence.
General Engine/Loom integration and a native batched mutation implementation
remain separate work. No additional benchmark matrix is needed to state this
conclusion, and no production change or commit is made by this reconciliation.
