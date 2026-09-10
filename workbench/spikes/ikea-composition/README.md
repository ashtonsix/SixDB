# Ikea composition and ergonomics

This investigation owns how Ikea's blocks, containers and kernels compose while
keeping a kernel author's local context manageable. It now directly owns the
bitset, packed-integer and heterogeneous probes, including their code, runners,
evidence and maintenance.

Start with the [current design](design.md) for the authoring model, vocabulary,
interface responsibilities and open choices. The basic-data-structure probes
are complete. The [initial implementation sketch](../../../ikea/implementation.md)
and [SeriesPack specification](../../../ikea/seriespack.md) now develop the first
module scope. [Value semantics and execution integration](semantics-and-integration.md)
inform its seams without requiring every open question to be settled first.
The experimental classes remain evidence, not production interfaces.

## Evidence by question

The implementations below developed while we were finding our way. They are
owned evidence, not recommended templates for new components or studies.

| Question | Findings | Reproducible probe |
| --- | --- | --- |
| How can shared bodies cross native and erased boundaries? | [ABI](probes/ikea-blocks/abi/README.md), [composition](probes/ikea-blocks/composition/README.md) | [Bitset primitives](probes/ikea-blocks/README.md) |
| Can format, placement and native grain vary independently, with value reuse? | [Body/tail composition](probes/ikea-integers/composition/README.md), [locality](probes/ikea-integers/locality/README.md) | [Packed integers](probes/ikea-integers/README.md) |
| How do independent sources, dependent metadata and selected regions fit together? | [Masked operations](probes/ikea-heterogeneous/operations/README.md), [closing assessment](probes/ikea-heterogeneous/closing.md) | [Heterogeneous structures](probes/ikea-heterogeneous/README.md) |

[Probe maintenance and recovery](probes/README.md) explains build selection and
historical source paths. For an authoring task, use the design's
[responsibility map](design.md#authoring-and-interface-responsibilities) first;
follow a probe when checking how a particular claim was tested.

## Rationale and alternatives

[Operation granularity](operation-granularity.md) develops region, grain and
lifetime tradeoffs. [Value-reuse sketches](value-reuse-sketches.md) retain
competing carrier approaches. Both inform the current design without selecting
a universal protocol.

The [original brief](brief.md), [first](sketches.md) and
[second sketches](sketches-2.md), [first-probe review](probe-review.md) and
[predictor review](predictor-review.md) preserve historical questions and
corrections. [Reading](reading.md) maps relevant prior work.

Collaboration focuses on composition, substitution, authoring ergonomics and
the costs of the chosen boundaries. Requests from other tasks for unrelated
correctness review, cleanup or implementation approval are reoriented unless
Ashton asks for them.
