# TuplePack implementation decisions

This note records the implementation choices made during promotion from the spike.
The maintained [reference](../../../ikea/docs/tuplepack/reference.md) owns the caller contract.

TuplePack stores unsigned byte-sized **codes**, with caller-defined meaning. The
first implementation accepts manual layouts. Layout analysis remains in the
[TuplePack spike](README.md); manual layout
selection is a temporary facility for dependent spikes, not the proposed burden
on higher-level production components.

## Representation and binding

A physical unit contains 1–64 bytes and 1–128 non-overlapping codes. Each code
has a byte offset, bit shift and width 1–8; it cannot cross a byte boundary.
Spare bits have no semantic value. A versioned physical description records
these facts independently of ISA, prepared controls and layout-selection policy.
Placement adds caller-owned storage, row count, stride and unit offset. There is
no universal segment-size limit. Multiple units can occupy one larger record or
separate planes; rebinding a description never migrates existing bytes.

An ordered operation map selects code ranks into byte slots. Reads allow
duplicates and zero-fill holes; writes ignore holes, reject duplicate code
destinations and reject selected values outside their unsigned widths. A code
rank is local to its physical description, not an Engine field identifier.
Reader/writer preparation owns its controls. Bound operations borrow stable
plans, named views and storage. Descriptions need not survive preparation.

Scalar operations carry eight bytes in a uint64_t; native operations carry 64
bytes. Ordinary buffered access and native authoring use the same semantics and
kernel bodies. AVX2 native endpoints use Clang regcall, since a two-vector SysV
aggregate otherwise returns through memory. NEON uses a four-vector homogeneous
aggregate; AVX-512 uses one vector. Metadata stays outside native payloads.

## Mutation and maintenance

Replacement preserves every unselected bit, including spare bits. Construction
explicitly initializes an entire unit, zeroes spare bits and requires every
defined code to be supplied exactly once across its input packets. It does not
read uninitialized destination bytes. Neither operation allocates storage.

Checked commands admit range, selected values and effect capacity before this
call changes data, maintenance state or effect output. Lifetime, isolation and
command/output disjointness are explicit borrowed-owner obligations; admission
does not pretend to prove arbitrary C++ object lifetimes. Compound commands
preflight every child and packet before invoking any child. Trusted entry points
reuse that proof and cannot fail or suspend. Range traversal remains inside the
bound operation; erasure does not introduce a dynamic call per code or byte.

Effects describe issued byte spans, including preserved neighbors. Their source
identity names the actual bound leaf, independently of the logical row and
semantic maintenance context. Hooks execute before each local store group and
must not fail or suspend. A checked error leaves this call unchanged; it does
not roll back earlier successful chunks or provide concurrent publication.

Maintenance invocation is independent of demand for old values. An observation
projection may include unchanged dependencies absent from the write map. For
example a shared row signature can recompute from the complete observed row,
widen conservatively from new contributions, or invalidate the row/block;
subtracting one field's hash bits is unsound when fields share bits. The caller
owns that law, semantic reconstruction, signature schema/version and publication.
Callbacks receive original row coordinates; they cannot infer them from call order.

The owner reserves all resources and retains binding, input, selection, leases,
effect output, partial maintenance and progress across suspension. Suspension
and cancellation occur between completed chunks. Data and summaries become
visible together, or the owner establishes an explicitly conservative/bypassable
summary state. In-place writes are supported, including Orbital UFFD COW.

## Composition and shared Ikea facilities

Share issued-span vocabulary and journals now: both modules have the same
owner-resolution obligation. Share straight-through pipeline mechanics where
the carrier and mask remain explicit. Do not standardize a universal container
base, scalar-value type, summary law, byte permutation API or per-region virtual
writer. SeriesPack's bit-window joins and TuplePack's ordered byte-code maps have
different semantics and traversal needs.

Authoring exposes physical descriptions, inline-friendly ISA bodies, read/write
requirements and whole-operation adapters. A parent can replace a named child,
rebind maps and retain its logical operation. Preparation chooses optimized
lowerings only after proving applicability and retains a complete legal fallback.
Source storage, physical placement and execution strategy remain independent.

## Technique selection

Retain explicit ISA permutation/shift/mask bodies, compacted source chunks,
exact tails, direct single-code writes, scalar eight-byte points and useful native
batching. Keep native controls specific to the compiled ISA; the spike's combined
multi-ISA control object is not the production representation. Preserve a general
legal-map path rather than promising every map is equally fast.

Structural route normalization has strong compound evidence and belongs in the
authoring story. Its restricted applicability is explicit; the general route
model is not an Engine expression language. Do not retain AOT fixture recognition,
multiple experimental execution selectors or an automatic layout cost model as
ordinary API. SVE2, JIT, distribution-aware patching and optimal layout search
remain research. New shapes and specializations need a supported consumer or
measurement justification, not inclusion merely because they were enumerated.

The usage/reference guides and executable checks describe this implementation.
Its [module evidence](module-evidence/README.md)
measures Ikea endpoints separately from the historical spike sources.
