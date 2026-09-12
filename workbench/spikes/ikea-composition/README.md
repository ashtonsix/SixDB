# Ikea composition and ergonomics

This investigation asks how containers, blocks and kernels compose while keeping
a kernel author's local context manageable. It owns the bitset, packed-integer
and heterogeneous probes and the experiments that informed both Ikea implementations.

For current use, start with [Ikea](../../../ikea/README.md),
[SeriesPack](../../../ikea/docs/seriespack/usage.md), or the
[maintained benchmarks](../../benchmarks/seriespack/README.md).
The [replacement campaign](ikea2-campaign/README.md) records its evidence and limits;
the [predecessor account](seriespack-predecessor/README.md) preserves earlier findings,
workload definitions and source recovery. Neither historical implementation is a
second supported module.

The [composition design](design.md) develops the organising principles and open
choices. [Value semantics and owner integration](semantics-and-integration.md)
explore the Engine/Loom/Orbital seams; module guides own implemented contracts.

## Evidence by question

| Question | Start here |
| --- | --- |
| How can shared bodies cross native and erased boundaries? | [Bitset primitives](probes/ikea-blocks/README.md), [ABI](probes/ikea-blocks/abi/README.md), [composition](probes/ikea-blocks/composition/README.md) |
| Can format, placement and native grain vary independently? | [Packed integers](probes/ikea-integers/README.md), [body/tail composition](probes/ikea-integers/composition/README.md), [locality](probes/ikea-integers/locality/README.md) |
| How do dependent metadata, independent sources and selected regions fit together? | [Heterogeneous structures](probes/ikea-heterogeneous/README.md), [masked operations](probes/ikea-heterogeneous/operations/README.md), [closing assessment](probes/ikea-heterogeneous/closing.md) |
| Where should traversal and call boundaries sit? | [Native regions](native-regions/README.md), [call boundaries](call-boundaries/README.md) |

Codec grains, range execution, head projection, metadata substitution and executable
placement have [separate study homes](../README.md). The
[predecessor assessment](seriespack-assessment.md) reconciles the first implementation;
[initial inclusion decisions](seriespack-decisions.md) preserve its rationale.

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
