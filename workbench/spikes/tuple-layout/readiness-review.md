# TuplePack readiness review

Review against the [starting proposal](README.md), [viability findings](viability.md),
[maintenance review](maintenance-review.md) and Ikea implementation.

| Obligation | Finding |
| --- | --- |
| Code schema, runtime ordered maps, holes/duplicates, recovery | Implemented with independent wire/description checks |
| Point/range/construction and caller storage | Implemented; packet ranges preserve original-row coordinates and short tails |
| Nested replacement, whole-call admission and byte effects | Implemented and tested, including shared physical bytes and substituted owners |
| Independent before/after observations and publication | Implemented with exact/conservative/bypass owner examples and event checks |
| Native multi-row reads **and writes**, including small projections | Unified reader/writer shapes cover 1/2/4/8/16/32/64 rows; native adapters retain the same shape through groups and erasure |
| Native payload continuity | Point and packet bodies retain registers; sparse wide writes use native encoding and selected stores |
| Representative composition evidence | Executable inline/CPS packet updates, nested native observations, and maintained body/checked/CPS comparisons |
| Technique curation | AOT fixture recognition, alternate selectors, layout analyser and cost model remain in Workbench; the standalone read-only batch API is removed |
| Caller struct conveniences | Native `word<I>` exposes extraction to GPRs; application type interpretation remains caller-owned |

The initial audit found that batching was the broad capability omission: the
standalone two/four-row reader had no writer, checked operation or composition
contract. A sparse native-write array bridge was an adjacent payload boundary.
The completed shaped operations replace both gaps. One row is the default case
of the caller surface while its physical kernels retain point specialization.

All children of a composed packet operation share its Rows. The erased type
retains Rows too: a 64-byte C++ input alone cannot distinguish one original row
from 64. Observation dependencies remain independent of mutation children;
packet observers can retain native payloads, and point observers are adapted
without observing partially updated groups. Admission errors leave the complete
call's bytes, effects and observations unchanged.

Manual layouts, deferred Engine analysis, SVE/JIT, distribution-aware patching
and mixed-workload layout policy remain deliberate scope choices. Source/data
lifetime, suspension and publication remain owner obligations. Their absence
from inner kernels does not settle the corresponding research questions or
require another scheduler inside TuplePack.

The [packet experiment](batching/README.md) owns performance interpretation and
remaining transfer-method questions. Module capability is documented in the
[ordinary guide](../../../ikea/docs/tuplepack/usage.md), [composition guide](../../../ikea/docs/tuplepack/extending.md)
and [reference](../../../ikea/docs/tuplepack/reference.md).
