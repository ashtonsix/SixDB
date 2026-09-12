# Capabilities and limits

Ikea provides operations over caller-owned storage. Both modules support checked
calls, explicit trusted calls, physical descriptions for recovery, and composition
with source-qualified mutation effects.

| Capability | SeriesPack | TuplePack |
| --- | --- | --- |
| Values | Unsigned arrays, widths 1–64 bits | Units of 1–64 bytes containing 1–128 byte-contained codes, widths 1–8 bits |
| Representation choice | `compact`, `bulk_x86`, `bulk_arm` presets; explicit formats | Manually specified code offsets, shifts and widths |
| Placement | Tiled payload and optional head planes; independent strides and interleaving | Unit offset and row stride; multiple units per record or separate planes |
| Reads | Checked point/range reads; scalar and native evaluation | Ordered scalar projections; 64-byte packets covering 1/2/4/8/16/32/64 rows |
| Mutation | Construction, point/range replacement and selected writes | Construction, point/packet/range replacement and selected writes; buffered and native |
| Composition | Recursive bit-window joins and child substitution | Recursive mutation groups, child substitution and bit-route normalization |
| Maintenance | Optional modulo-u64 replacement delta; authored contribution laws | Separate observation projection; old/new/coordinate-only callbacks over rows or packets |
| Execution | NEON, AVX2, AVX-512; inline and straight-through CPS | NEON, AVX2, AVX-512/VBMI; inline and straight-through CPS |

Use the [SeriesPack reference](seriespack/reference.md) or
[TuplePack reference](tuplepack/reference.md) for exact bounds, borrowing
requirements and failure guarantees. [Owner integration](integration.md) covers
leases, cancellation, resumable work and coordinated publication. Engine supplies
field semantics and collection schemas; its layout analyser is experimental.
StreamPack has no operations available.

## Choosing an operation

SeriesPack's runtime dense-read binder covers headless formats. Placed and
composed operations use concrete compiled bindings. Writable admission rejects
overlapping whole fields across leaves; a read transform needs an explicit
assignment operation before it can participate in mutation.

TuplePack requires a supplied layout and operation map. Use its scalar eight-slot
interface for small transactional projections. Packet shape is independent of
physical tuple size: a one-byte tuple can contribute one code to a 64-row packet,
or several codes to a packet with fewer rows. Performance depends on physical
placement, map and active mask; the [benchmark guide](../../workbench/benchmarks/tuplepack/README.md)
links the measured cases and their limits.

Native ISA selection is a build choice. CPS uses a compiler-specific calling
convention and bounded straight-through tables; suspension occurs after return
to the owner. Choose inline execution or fusion for work too small to amortize
continuation overhead. Both modules' benchmark guides provide comparisons:
[SeriesPack](../../workbench/benchmarks/seriespack/README.md),
[TuplePack](../../workbench/benchmarks/tuplepack/README.md).
