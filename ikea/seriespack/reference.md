# Semantic and execution reference

This reference describes the current SeriesPack surface. Exact physical bytes
and recovery metadata are in [representation.md](representation.md).

## Values, coordinates and substitution

SeriesPack values are unsigned K-bit patterns, K=1..64. A materialized output type
is one of u8/u16/u32/u64 and must accommodate K. A selected replacement value must
fit K; inactive values need not fit. Arithmetic maintenance provided here is
modulo 2^64. Signedness, floating interpretation and order (including NaNs/zero),
nullability, collation, FOR bases, patches and predictive relationships belong to
the enclosing semantic composition and Engine schema.

The signed-rank [example](../examples/seriespack/pipeline.cpp) supplies one transform. It does
not make a general signed/float container policy. Engine defines segment or
record-slice nodes and may impose the agreed maximum of 2^16 record positions.
Standalone SeriesPack arrays have no such limit; they are bounded by addressable
storage and checked extent arithmetic.

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
| Typed prepared mutation | Until erased endpoints finish; moving it requires rebinding |
| Dense read descriptor wrapper | Can expire after binding; bytes remain borrowed |
| Input, selection words, journal storage and summary | Through invocation; through a stop when retained by the driver |
| CPS plan and bindings | Through the whole chain; no stage-local address crosses a tail hop |

Read output, mutation input, selection, summary, journal and binding metadata must
be disjoint from occupied data in ways that could alter the command or corrupt
results. A byte span's stride gaps can hold siblings; it is not ownership of the
entire envelope. View admission checks used planes' occupied spans, not gap data.

Checked get/read/set/replace/initialize report command errors before that call's
reads/writes or output effects as specified at the declaration. A failed checked
read leaves output unchanged. A failed checked mutation leaves data, summary and
journal unchanged. Cold mutation binding may allocate temporary alias-analysis
storage; allocation failure is a typed error. Kernel execution does not allocate.
Diagnostic formatting and optional source identities stay outside hot bodies.

Trusted `_unchecked` calls require prior proof of range, value domain, accessible
extent, stable selection, effect capacity, disjointness and owner lifetime.
Sixteen-row entries require aligned original coordinates and a full logical region.
A nonempty native write can read all sixteen input slots, even where inactive.
An empty region reads no input/data and emits no contributions. Scalar edges read
only selected input positions. Checked does not imply isolation or publication.

Effects cover issued bytes before each local write group, including preserved
neighbors. They do not promise minimal differences, beforeimages or a transaction
barrier. Cancellation after a successful chunk needs owner resolution; it is not
a failed command with an unchanged-effects guarantee.

## Execution profiles

| Profile | Supported execution |
| --- | --- |
| Linux AArch64 NEON | Native reads, mutation, composition, straight-through CPS; measured on Neoverse V2 |
| Linux x86 AVX2 (`x86-64-v3`) | Same capabilities; native wide-value carriers can occupy several YMM/XMM arguments |
| Linux x86 AVX-512 (`x86-64-v4`) | Wider grouped bodies where useful; VBMI/VBMI2/GFNI paths compile only with their feature flags |

Selection is a build/profile choice, not a runtime CPU detector. Dense width and
layout selection has a compiled runtime binder. Placed/composed operations bind
concrete types; they do not preinstantiate every possible erased composition.
Scalar wire/point functions exist as building blocks and correctness fallbacks;
a scalar-only full mutation/CPS product profile is not promised.

Manual fusion, inline execution and CPS retain the same logical contract. CPS is
straight-through with early completion, a bounded power-of-two function table and
an explicit native carrier. It uses the pinned compiler's `preserve_none` and
`musttail`; that ABI is private to the build, not stable across compilers, shared
libraries or persisted plans. Incompatible carriers require real bridges.
Suspension happens after returning to the owning driver.

The in-process recorder exposes loads, bit joins, predicates and reduction with
source identity and selection. It is illustrative reflection for composition,
not an Engine-owned equivalence graph, complete planner IR or serialized schema.
Physical descriptors do not persist its addresses or function pointers.
