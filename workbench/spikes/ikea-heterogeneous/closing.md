# Closing assessment

Closed on 2026-09-10 at Ashton's direction. The bitset, packed-integer and
heterogeneous exercises have supplied enough concrete experience to implement
Ikea's approach to basic data structures. Further probes are not a prerequisite.
The exact production APIs remain to be written; the prototype classes and
benchmark policies are evidence for that work, not interfaces to copy wholesale.

## What carries into implementation

**Separate physical definitions, native compute and enclosing operations.**
A format defines meaning and byte geometry; placement supplies addresses and
access extents. Native implementations state the operands and access they need.
An enclosing operation supplies traversal, dependencies and output duties.
LocalPack/ScanPack now serve both values and BEC metadata without adopting one
customer's semantics. The [metadata definitions](metadata_format.h),
[native readers](metadata_native.h) and [authored range](authoring.h) demonstrate
this separation.

**Curate fine combinations; share at boundaries that earn their cost.**
The objective remains `A × B + C × D`, with selected inline/fused regions and
coarser reusable calls or continuations. The range experiment shares
decode/AND/count behind a scalar/pointer boundary. The [algebra](algebra.cpp)
shares unary metadata readers outside six heavy source-family/operator kernels,
keeping decoding and Boolean work inline. Input layouts and lookup strategies
bind independently; two curated Local/Local kernels measure the alternative.
Each directory choice does not require another complete decoder for every
other directory choice.

**Keep logical identity independent of native grain.**
Metadata16 can feed BEC pairs while retaining unused entries. A selected ordinal
is its original position, not its compacted rank. Two decoder inputs can be the
two operands at one ordinal, or adjacent ordinals from one source. The outer
contract determines logical progress and completion; compatible native fragments
should meet without compulsory splitting and reassembly. No universal iterator,
carrier or physical width is established by this.

**Establish obligations before repeated trusted execution.**
Source admission checks actual owner association, framing and access windows;
binding checks attached operands and output restrictions. Callers still supply
common coordinates and immutable snapshots. Hot bodies trust those facts,
without per-slice validation or `std::expected` returns. Locality, ordered/unique
data laws and allowed effects belong in block/operation descriptions with an
identified owner or proof. Tags can name requirements; they cannot establish
their dynamic truth.

**Preserve dependencies and postconditions through substitution.**
A mask can omit a body while preceding lengths remain necessary to locate
another body. A decoder window can read beyond that body's logical length.
A selected-only writer and a complete zero-filled result have different duties.
The parent must account for those accesses and effects: child locality alone
did not preserve parent locality here.

**Distinguish estimates, established facts and planning policy.**
A body-size estimate needs metadata, suffix and allocation costs to become a
storage estimate. It is neither a capacity bound nor proof of CPU benefit.
A coarse candidate mask is not an exact predicate result. Ikea supplies the
operation, applicability and evidence; Engine owns intelligent conjunction
composition. Calico's boxes and dynamic programming are not part of this direction.

These are implementation commitments supported by the exercises and Ashton's
steering. Recordable composition functions and optional named components remain
the [working authoring model](../ikea-composition/synthesis-1.md); production
syntax, reflection and lowering should express these distinctions while keeping
local kernel authoring small.

## Evidence to keep attached

| Observation | Consequence and scope |
| --- | --- |
| Packed directories halve metadata bytes but cost 6–14% in repeated fully inline range reads. | Storage size and execution cost are separate choices; cold access was not measured. |
| Sharing decode/count costs about 5–6% on Zen 5, 0–2.5% on Granite Rapids and roughly nothing on V2 for packed full-range reads. | A coarser ordinary call is credible; this does not establish a universal continuation budget. |
| Local metadata point access uses one line; Scan can need five. | Re-establish parent locality against the selected reader and placement. |
| Algebra's frame seam materialises and retains 64 bytes per compressed input. | Price both the interface output and the caller's retained state. |
| V2 grain one reduces selected-kernel text from 91,092 to 24,888 bytes, yet loses about 5–9% on the principal dense/mixed full-window cases. | Inspect actual ABI and spills, then measure throughput. Grain two remains this probe's default. |
| Full prediction has low natural-corpus storage regret; fixed sampling can err by 10,528 body bytes on correlated structures. | Estimates need evidence and an explicit heuristic contract. Confirm actual bytes when savings must be guaranteed. |

The [range measurements](measurements.md), [locality witness](locality.md),
[operation measurements](operations/measurements.md),
[analyser findings](operations/analyser-findings.md) and
[actual boundary audit](notes/operations-boundaries.md) own the detailed claims.
The integer study separately retains its [performance gaps](../ikea-integers/next-probe.md)
and [locality evidence](../ikea-integers/locality/README.md); closure does not
turn those gaps into completed coverage.

## What closes here

Completed scope: heterogeneous range/count, whole-window size analysis and
masked union/intersection; three metadata layouts, plain/BEC combinations,
source admission, independently bound readers and two output postconditions.
Providers remain linked directly from their owning spikes. Sanitizers,
provider/combined checks, three-target timings, actual assembly and report
integrity checks support the findings. Fourteen compact evidence exports retain
selected repetitions and provenance; their bundles preserve full sweeps,
binaries, measured sources and input references. [Recovery](../../tools/artifacts.md)
does not depend on future live providers producing the same code.

Implementation can now give these responsibilities stable homes in `ikea/` as
concrete data structures need them. Engine-visible deep replacement, progressive
observations, aggregate/filter mutation, MVCC changed-byte reporting and suspension
belong to their actual integration work. Full kernel/width coverage, heterogeneous
cold access and compressed Boolean output also remain explicit unfinished work.
None is an instruction to prolong this spike or invent a general framework
ahead of its consumer.
