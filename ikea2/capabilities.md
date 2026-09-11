# Capabilities and deliberate limits

Ikea2 is a replacement candidate for Ikea, kept separate from the live module.
Ikea's scope is composable containers, codecs and kernels for records and standalone
data structures. Its first implemented component is SeriesPack; the capabilities
below describe that component and the composition/owner seams exercised with it.
TuplePack and StreamPack remain thin stubs, with no format contract implied.

The usable surface is organized around [ordinary SeriesPack operations](seriespack/usage.md),
[composition authoring](seriespack/extending.md), [owner integration](integration.md) and
[reference contracts](seriespack/reference.md).

| Supported capability | Scope |
| --- | --- |
| Packed unsigned arrays | Widths 1..64; Local and curated striped families; three construction presets |
| Placement | Independent or interleaved planes, explicit tile strides, zero/eight/sixteen head bits; caller-owned bytes |
| Ordinary operations | Checked construction, point/range reads and writes; explicit trusted hot entries; selected bulk mutation behind one erased whole-operation binding |
| Composition | Recursive bit-window joins; substituted children with different tiling/placement/owners; scalar/native evaluation and an in-process recorder |
| Mutation | Actual-leaf admission and diagnostics, native old/new maintenance, issued-byte journals, conservative capacity queries, preserved siblings/slack rules |
| Execution | Explicit NEON/AVX2/AVX-512 bodies; inline and manually fused recipes; shared-body straight-through CPS with early completion |
| Representation recovery | Versioned resolved physical descriptions independent of mutable presets; independent retained wire fixture |
| Owner interoperation | Executable contracts for retained leases/live state, cancellation, effects and coordinated visibility; in-place and private-copy test adapters |
| Maintained validation | Independent wire checks, 206-format matrix, substitution with inaccessible retired bytes, boundaries/effects/ownership; executable guides and selectable benchmarks |

The deliberate limits are:

- Engine owns signed/float/schema interpretation, segment definitions, heterogeneous
  representations and long-lived plans. The candidate supplies useful composition
  seams and physical descriptions; it does not implement schema analysis, automatic
  layout tuning, progressive-plan adaptation or an equivalence-graph engine.
- Byte ownership, residency, exclusion, MVCC, asynchronous scheduling, recovery and
  publication remain with owners. Serialized adapters are contract demonstrations,
  not concurrency or durability implementations. UFFD COW needs no codec beforeimages.
- Writable admission rejects overlapping whole fields across leaves. Finer bit-level
  aliasing needs an explicit stronger proof; arbitrary read-only transforms do not
  automatically gain a mutation inverse.
- Runtime dense read binding covers headless formats. Placed/composed operations
  require a concrete compiled binding; there is no universal dispatch table for all
  combinations. Physical descriptors omit owner mappings and semantic graph schema.
- CPS uses a private compiler/ISA-specific carrier ABI and bounded straight-through
  tables. It does not provide arbitrary control flow or suspend inside a kernel.
  Tiny stages are not a performance promise; stage grain/fusion remains a plan choice.
- Measurements cover warm operation/consumer cases, not full queries, cold DRAM,
  page faults or scheduler/publication cost. The [candidate evidence account](../workbench/spikes/ikea-composition/ikea2-campaign/README.md)
  owns measured operating points, remaining exceptions and their disposition.

Changes can be validated without campaign knowledge through `ikea2_validate`, the
[benchmark guide](bench/README.md) and [source boundaries](source.md). This document
states supported behavior; experimental history belongs in Workbench.
