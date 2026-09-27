# What survives the first SeriesPack

The first implementation was replaced on September 11. Its kernel experiments,
range drivers, head projection, callback changes and delivery assessment no
longer form a second development surface. Their source and measurements are
[archived together](../../notebook/retired-spikes.md). This note keeps the reasons
to return to them; current contracts belong to [Ikea](../../../ikea/README.md).

## Composition lessons

Physical tile size, execution grain, working lanes and result representation
are independent choices. In the final seven-profile comparison, tile-authored
compare-and-sum won 48 of 144 cases against reused-scratch materialization and
lost 96. Selecting among measured native grains and carriers improved that to
110 wins and 34 losses. Authored/direct ratios ranged from .671 to 1.010:
a cheap composition wrapper did not make every underlying operation competitive.
Those are results for the retired implementation, not current dispatch advice.

Larger working groups and deferred reduction helped some consumers without
changing storage geometry. More accumulator chains alone did not resolve the
same gaps. A useful future comparison varies grouping and finalization separately,
including independently placed inputs and a strong materialized control.

The old BEC length-child substitution likewise separated child and enclosing
operation. Native six-bit metadata had median native/materialized ratios of
.457 for refill but .9669 for whole count; it was essentially tied with the
specialized whole consumer (.9993). The cursor retained checkpoint predecessors
and native lanes across body pairs while each child kept its own placement and
logical extent. The current [Bec256 composition study](../bec256-composition/README.md)
owns that line of investigation, including its shared prepared windows.

## Candidates are clues, not pending patches

| Retired experiment | Useful result and remaining question |
| --- | --- |
| Packed integer kernels | Direct bit expansion, coalescing and grain choices helped particular width/ISA/carrier combinations. Some apparent wins used an invalid interleaved H16 fixture and were explicitly excluded. Re-establish applicability against today's wire and caller before reusing a lowering. |
| Ordinary range driver | Strong inner bodies did not repay broad short/suffix losses or roughly 135–156 KiB of added native text. Preserve facts established at admission through the hot call; a general expression driver need not own every ordinary operation. |
| Clipped striped boundary loop | 51 of 72 affected cases lost despite aligned wins. Extra edge machinery and another helper were not justified by this comparison. |
| Joint H16 projection | Dense encoding wins coexisted with small/gapped losses and about 33 KiB of extra text. Sharing head computation must not force independently placed children into one traversal geometry. |
| Short-region stores | Fifteen affected Granite Rapids get16 cases improved 7.6–54%, with about 1.1 KiB extra text; short scalar-tail regressions and other-host sensitivities remained. This was an uninstalled predecessor candidate, not a ready change to current Ikea. |
| Trusted and checked call boundaries | Removing unused trusted arguments avoided aggregate copies. Relinking also moved unchanged controls; the surviving [executable-placement study](../executable-placement/README.md) owns that discriminator. |

The original [delivery assessment](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/ikea-composition/seriespack-assessment.md)
connects these judgments to exact source identities, repetitions, countercases
and code costs. Its present-tense descriptions refer to that historical module.
The [replacement campaign](ikea2-campaign/README.md) retains the later comparison
and its exceptions. Do not combine ratios across those source snapshots.

## Workloads worth recovering

The old suite includes variable resident footprints, dependent-point access and
Calico controls beyond the replacement suite's coverage at switchover. Its
[workload definitions](https://github.com/ashtonsix/SixDB/blob/275698524c58dea08f19961dbc2940cae68b405b/workbench/spikes/ikea-composition/seriespack-predecessor/workloads.md)
remain useful when a new consumer supplies a reason to port one. Preservation
does not imply it is already measured on today's Ikea.

The maintained [SeriesPack suite](../../benchmarks/seriespack/README.md) still
uses the optional [frozen comparator](ikea2-campaign/reference/README.md) and
integer-probe controls. Those executable dependencies remain in the live tree.
The independent scalar wire oracle stays with Ikea's correctness tests.

For the runnable pre-switchover module, tools and suite, recover commit
`08187281bb59f81d0b3b5fbcc0f9689ce7019c1d`; the retiring module checkpoint is
`4ab2b64cc56b841e798da0088e24e04a216d9c48`. A historical timing can refer to an
earlier capture: its artifact and source/binary hashes, not a rebuilt comparator,
identify the measured program. See [recovery](../../notebook/retired-spikes.md#recovery).
