# Semantic and execution reference

This reference specifies SeriesPack's operation contracts. Exact physical bytes
and recovery metadata are in [representation.md](representation.md).

## Values, coordinates and substitution

SeriesPack values are unsigned K-bit patterns, K=1..64. A materialized output type
is one of u8/u16/u32/u64 and must accommodate K. A selected replacement value must
fit K; inactive values need not fit. Arithmetic maintenance provided here is
modulo 2^64. Signedness, floating interpretation and order (including NaNs/zero),
nullability, collation, FOR bases, patches and predictive relationships belong to
the enclosing semantic composition and Engine schema.

The signed-rank [example](../../examples/seriespack/pipeline.cpp) supplies one transform.
SeriesPack arrays are bounded by addressable storage and checked extent arithmetic;
Engine defines the size and representation policy of its record segments.

Positions and counts always refer to the original logical array. Masks do not
compact or renumber them. Byte offsets refer to the named plane's origin. Physical
tiles, execution grain, scheduling chunks and segment boundaries are distinct.
A replacement child preserves its parent's logical width and coordinates; it may
change geometry, stride, byte owner and suitable kernels. Admission and effects
follow only used leaves. Different segments can retain different representations,
including indefinitely cold segments undergoing lazy migration.

## Borrowing and failure guarantees

| Resource | Required lifetime |
| --- | --- |
| Attached bytes | Every operation using the view; owner synchronizes concurrent access |
| Named view used by a placed binding or expression | Until all borrowed invocations/effect identity resolution finish; placement stays stable |
| Typed expression reader or writer | Until erased endpoints finish; moving it requires rebinding |
| Dense source or typed dense reader wrapper | Can expire after erasure; pointer/count proof is copied and bytes remain borrowed |
| Input, selection words, journal storage and summary | Through invocation; through a stop when retained by the driver |
| CPS plan and bindings | Through the whole chain; no stage-local address crosses a tail hop |

Read output, mutation input, selection, summary, journal and binding metadata must
be disjoint from occupied data in ways that could alter the command or corrupt
results. A byte span's stride gaps can hold siblings; it is not ownership of the
entire envelope. View admission checks used planes' occupied spans, not gap data.

Checked get/read/set/replace/initialize report command errors before that call's
reads/writes or output effects as specified at the declaration. A failed checked
read leaves output unchanged. A failed checked mutation leaves data, summary and
journal unchanged. Mutation binding reuses a whole admitted view's placement
proof without allocation. Composed views may need temporary alias-analysis
storage; allocation failure is a typed error. Kernel execution does not allocate.
Diagnostic formatting and optional source identities stay outside hot bodies.

Trusted `get_unchecked`, `read_unchecked` and `replace_unchecked` calls require
prior proof of range, value domain, accessible
extent, stable selection, effect capacity, disjointness and owner lifetime.
`read16_unchecked` and `replace16_unchecked` require original coordinates divisible
by 16 and a full sixteen-row logical region.
A nonempty native write can read all sixteen input slots, even where inactive.
An empty region reads no input/data and emits no contributions. Scalar edges read
only selected input positions. Checked does not imply isolation or publication.

Effects cover issued bytes before each local write group, including preserved
neighbors. They do not promise minimal differences, beforeimages or a transaction
barrier. Cancellation after a successful chunk needs owner resolution; it is not
a failed command with an unchanged-effects guarantee.

## Binding lifetimes

`bind_reader(view)` and `bind_writer(view)` borrow the named admitted view.
Bindings over expressions own their small node tree and borrow its named leaves.
A general typed reader's `erase<U>()` and a writer's `erase<U,Summary>()` borrow
the typed operation; temporary erasure is deleted. Direct
`bind_reader<U>(view)` instead borrows the view itself and selects the physical
range body at binding. Its rvalue-view overload is deleted. Dense sources copy
a pointer/count proof into either form of erased reader and require only the
bytes to survive. None of these forms extends the data owner's lifetime.

## Scalar point building blocks

`<ikea/seriespack/point.h>` exposes ordinary `get` and `get_unchecked` over
an admitted physical view. `<ikea/seriespack/author/store.h>` exposes the raw
physical `author::store` and `author::store_unchecked` leaves for composition
authors and private construction. They retain the original row coordinates and
packed bytes of bound operations.

`visit_point_reads(view, row, emit)` reports the exact bytes `get_unchecked`
reads for that row, as `(address, size)` spans: body bytes, residual fragments,
then head bytes. It reads nothing, so a caller with independent future rows can
prefetch or schedule those spans ahead of the read, as Hashtable's staged probes
do. A span is neither a lease nor evidence that the bytes are ready.

`get` checks the row. `author::store` checks the row and unsigned value width
before data access; refusal leaves bytes unchanged. Their unchecked forms require
those proofs from the caller. Both forms borrow placement, accessible byte
extents, owner lifetime and synchronization from the admitted view.

Raw point stores preserve other fields sharing a packed byte. Those bytes must
already be initialized, and the caller must isolate the complete bytes touched
by the store from concurrent writers. A raw point store does not clear final-tile
slack, emit write effects or contribute to summaries. For coordinated owner
mutations, use `bind_writer` and its bound `set`/`replace`/`initialize`
operations, which report issued source-qualified writes and apply the chosen
summary law. A raw leaf store does not inherit those operation-level guarantees.

## Execution profiles

| Profile | Supported execution |
| --- | --- |
| Linux AArch64 NEON | Native reads, mutation, composition, straight-through CPS; measured on Neoverse V2 |
| Linux x86 AVX2 (`x86-64-v3`) | Same capabilities; native wide-value carriers can occupy several YMM/XMM arguments |
| Linux x86 AVX-512 (`x86-64-v4`) | Wider grouped bodies where useful; VBMI/VBMI2/GFNI paths compile only with their feature flags |

Select the ISA at build time. Headless dense reads have a compiled runtime
width/layout binder; placed/composed operations bind concrete types. The current
Ikea minimum is AArch64 with SIMD or x86-64 with AVX2 (`x86-64-v3`). Ordinary
operations are present on both supported targets. Scalar point/wire algorithms
remain internal building blocks; they do not establish a lower public target
minimum. ISA-specialized internals require their stated extra feature flags.

Manual fusion, inline execution and CPS retain the same logical contract. CPS is
straight-through with early completion, a bounded power-of-two function table and
an explicit native carrier. It uses the pinned compiler's `preserve_none` and
`musttail`; that ABI is private to the build, not stable across compilers, shared
libraries or persisted plans. Incompatible carriers require real bridges.
Suspension happens after returning to the owning driver.

The in-process recorder exposes loads, bit joins, predicates and reduction with
source identity and selection. Its addresses refer to live bindings. Use
[shared byte-law descriptions](representation.md#recovery-and-shared-byte-law) for recovery.
