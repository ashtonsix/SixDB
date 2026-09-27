# Ikea composition and ergonomics

This investigation asks how containers, blocks and kernels compose while keeping
a kernel author's local context manageable. It owns the bitset, packed-integer
and heterogeneous probes and the experiments that informed both Ikea implementations.

For current use, start with [Ikea](../../../ikea/README.md),
[SeriesPack](../../../ikea/docs/seriespack/usage.md), or the
[maintained benchmarks](../../benchmarks/seriespack/README.md).
The [replacement campaign](ikea2-campaign/README.md) keeps its comparative evidence
and the frozen controls still used by benchmarks. [SeriesPack history](seriespack-history.md)
consolidates the first implementation's useful findings and recovery pointers.

The [composition design](design.md) develops the organising principles and open
choices. [Value semantics and owner integration](semantics-and-integration.md)
explore the Engine/Loom/Orbital seams; module guides own implemented contracts.

## Evidence by question

| Question | Start here |
| --- | --- |
| How can shared bodies cross native and erased boundaries? | [Bitset primitives](probes/ikea-blocks/README.md), [ABI](probes/ikea-blocks/abi/README.md), [composition](probes/ikea-blocks/composition/README.md) |
| Can format, placement and native grain vary independently? | [Packed integers](probes/ikea-integers/README.md), [body/tail composition](probes/ikea-integers/composition/README.md), [locality](probes/ikea-integers/locality/README.md) |
| Should striped12 also centre its residual? | [Ordinary access and mutation comparison](placement/README.md) |
| How do dependent metadata, independent sources and selected regions fit together? | [Heterogeneous structures](probes/ikea-heterogeneous/README.md), [masked operations](probes/ikea-heterogeneous/operations/README.md), [closing assessment](probes/ikea-heterogeneous/closing.md) |
| Where should traversal and call boundaries sit? | [Surviving SeriesPack lessons](seriespack-history.md#composition-lessons), then the historical sources it identifies |

[Executable placement](../executable-placement/README.md) remains an independent
measurement question. The retired kernel, range, head and metadata experiments
are absorbed into the history above.

## Rationale and recovery

[Operation granularity](operation-granularity.md) develops region, grain and lifetime
tradeoffs; [value-reuse sketches](value-reuse-sketches.md) retain competing carrier
approaches. [Reading](reading.md) maps prior work. The [original brief](brief.md),
[first sketches](sketches.md), [second sketches](sketches-2.md),
[first-probe review](probe-review.md) and [predictor review](predictor-review.md)
retain earlier questions and corrections.

[Probe maintenance](probes/README.md) explains historical build selection and source
paths. The former validation directory's [inventory and recovery guide](archive/validation-20260911.md)
locates moved studies, archived full sweeps and retired campaign scripts. These
probes are evidence to understand and revisit, not templates for new components.
