# Composition probes

These probes are owned and maintained by the
[Ikea composition investigation](../README.md). They developed while we were
finding the design; their implementations are experimental evidence, not
recommended examples for new components or studies. The owning investigation
separates lessons to carry forward from incidental structures and rejected paths.

| Probe | Evidence and question |
| --- | --- |
| [Bitset primitives](ikea-blocks/README.md) | Plain/Bec256 codecs, size prediction, native ABI and initial composition experiments. |
| [Packed integers](ikea-integers/README.md) | LocalPack/ScanPack, placement, native grains and reuse in body/tail reconstruction. |
| [Heterogeneous structures](ikea-heterogeneous/README.md) | Dependent metadata, independently bound inputs, selected regions, masked algebra and admission. |

Select `-DSIXDB_SPIKES=ikea-composition` to configure the owned probes, then build
the existing target needed for the check. Their individual runners and optional
prior inputs are documented beside each probe. From macOS, prefix Linux commands
with `orb -m ubuntu`, as in the [build guide](../../../../BUILDING.md).

The probes moved here from the top-level spike directory on 2026-09-10.
Retained evidence, hashes and provenance are unchanged. Historical source paths
and regeneration commands refer to the captured source trees; use the current
probe documents for relocated commands, or recover the original tree with the
[artifact tools](../../../tools/artifacts.md). Dataset objects and experiment
output locations are unchanged.
