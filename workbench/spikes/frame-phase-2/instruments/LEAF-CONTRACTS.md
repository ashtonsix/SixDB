# Front C: leaf contracts and native peer entry points

Review of `01818143`, 24 October 2026. I also inspected the current uncommitted
SVE/compaction changes; those do not change the ownership conclusions below.
This is a research contract review, not a new public Ikea API or a requirement
to slow every leaf down.

## Keep the fast leaf, make its physical authority explicit

An initialized, immutable whole-image read window can admit the current fast
loads once when binding the image. It need not add a check to every load or
force the last block exact when a real suffix grant exists. Keep that fast
route. An exact/window-boundary route is also needed for a sliced image, a
protected neighbour or an owner unable to grant the larger read window.

Allocation padding by itself is insufficient. The **whole actual load
envelope** must be initialized, readable and stable for the borrow, including
interior bytes from the next body and the image suffix. A mutable neighbouring
body can race a vector load even if its lanes are discarded. A coarse mask
does not supply that permission. If the owner promises excluded block storage
is inaccessible, reading a selected block cannot cross into that storage.
If it deliberately grants the larger immutable image, document the bounded
read halo rather than claiming selected-body-only physical access.

This is the same distinction current
[TuplePack author access](../../../../ikea/include/ikea/tuplepack/author/native.h)
makes between active units and an explicitly admitted initialized, readable,
read-isolated window. Its current window does **not** itself grant suffix
padding. [SeriesPack's ordinary contract](../../../../ikea/docs/seriespack/usage.md)
continues to require bounded access; these experimental leaves must not
silently weaken it.

Use actual operation/ISA envelopes. A blanket “256 extra bytes” is a
conservative fixture grant, not the byte law. Do not add 256 bytes to every
block just to make the proof simple: that changes the compressed footprint
being evaluated. One image suffix plus an admitted interior window, with
boundary tails where necessary, preserves the compact representation.
For run leaves this includes both end-vector and value-vector tails.
Patched leaves have their own position/value-vector tails too.

Empty coarse domains must be rejected before body/evidence address formation.
A whole selected block may still grant its complete body despite a fine row
mask; distinguish that existing physical unit from crossing into another body.
Protected-page tests should exercise the actual promised boundary.

## Partial results and coordinate laws

The leaf returns a result in the **residual** domain. For a selected SUM the
substrate adds `base * selected_count`, with the count from the final
aggregate domain. Do not use 256, the predicate count before B validity, or a
run count. K <= 32 full-block sums fit u64; 64-bit run values require the stated
wide result. The dispatch ABI must preserve that distinction.

Four u64 words mean original rows 0..255 of this block. A run-ordinal mask is
different. Fine input selection, A validity and B validity are different
inputs too. SUM(B) drops invalid B values, while filter/project(B) normally
keeps the selected row and carries B's validity into the output. Reusing the
aggregate's B-valid mask for compaction would silently drop NULL rows.

Descriptor bounds may resolve an out-of-range predicate before entering a
leaf, but must retain the current coarse/fine/validity domain and the operation's
empty-result law. A selected MIN/MAX needs an explicit empty result; a first
match needs the original coordinate or a declared sentinel.

The run-start bitmap is correctly classified as optional, derivable access
evidence, like a checkpoint, rather than new meaning of the body. Bind it to
the same body/edition: bit zero is set, popcount equals n, and each later set
bit is the preceding end plus one. Point rank is inclusive through the row,
then minus one. The ends remain a usable fallback when evidence is absent.
An update that leaves boundaries unchanged need not rebuild it; a split/merge
must rebuild or invalidate it coherently before readers use the new ends.
Afterimages over an immutable base can keep the base evidence and compose the
override separately. Charge the 32 bytes, construction, any validity/identity
state and cold acquisition. Do not make the extra plane mandatory solely
because it wins one mask microbenchmark.

## Compaction is an append reservation, not just a new length

The destination should be a privately owned, pre-reserved output region.
Before the first store, the caller admits the physical capacity, writable
slack, initialized bytes needed by preserving ORs, and enough effect records.
The logical selected-row count is available from the mask without loading
the payload, so this admission need not add a payload-dependent round trip.

Publish actual issued store ranges through a live output source/plane/offset:
zero initialization, whole-word OR stores, preserved neighbours and any
over-store are all effects. They are not only the logical appended byte
interval. A before-store hook is not a substitute for admission of the whole
operation; [the change witness](../change/README.md) establishes that distinction.
The live output view must remain resolvable until its effects are consumed;
reallocation/rebinding cannot silently retarget old journal entries.

There are two useful distinctions in the current code:

- `compactor<L,K>` compresses byte slices into its local `c[72]` scratch,
  then copies exact selected byte counts to the destination. Its plane
  initialization and word ORs can touch the unfilled part of a tile. It does
  **not** issue the same final 64-byte store as the native lane control.
- `lane_compactor<T>` does write beyond its logical result (64-byte stores on
  AVX-512, or the final repeatedly overwritten slot in the scalar lane path).
  It also reads/writes for an all-zero selection unless the caller skips it.
  Give both the candidate and native control explicit, accurate reservations;
  do not claim no-access-on-empty for that raw control as currently written.

The plane compactor clears R planes on first touch but does not initialize
every unused byte-slice slot in a partial output block. A later full-width
masked reader needs those bytes initialized if it loads the whole block.
Either initialize that readable tail and charge its stores, or use a tail
reader whose declared envelope avoids it. Capacity alone does not initialize it.

Input, selection/evidence and output require a non-overlap proof covering
physical read/write envelopes, not just logical rows. In-place compaction is
not implied: clearing output planes can destroy a source plane needed by a
later group. The output cursor is a builder cursor; publishing it to other
readers is a separate owner action after the required bytes are complete.

Two composition seams need to remain visible:

1. Raw residual append requires **the same FOR base as well as width**.
   Equal K does not mean equal base. Combining blocks with different bases
   needs conversion/rebasing or separate output segments with their own
   interpretation. Synthetic base-zero tests do not establish the general law.
2. Stable output order requires source blocks and selected rows to retain
   their original order. Kind batching cannot feed this single append cursor
   in arbitrary order. It can instead use preassigned stable destinations
   derived from counts, or retain ordered segments. Exact integer SUM's freedom
   to reorder does not transfer to compaction.

Useful boundary witnesses: all-zero selection with both input/output
inaccessible; output cursors 0,1,63,64,255; selection crossing a tile; partial
last byte slices; odd run counts around 15/16/17; patched positions/values at
1/15/16/17/63/64 exceptions; rejected capacity before any store; independent
canaries around physical reservations. Keep these local and bounded.

## Wire identity and repair

`plane64` and `plane256` are new byte laws, even at the same K/byte count.
Keep explicit private geometry identities in the spike. If one graduates into
SeriesPack, width alone must not select the reader: tile grain, byte/plane
ordering, head separation and tail/read authority must be represented.
Current `sp::geometry` only names Local and Striped; do not relabel these as
existing Striped. No maintained geometry addition is needed to keep researching.

The optional leading-byte filter planes also need an interpretation that is
available before acquiring the body they are intended to prune. The current
byte-slice layout is useful evidence for that design, not automatically the
same physical contract as SeriesPack's H=8/16 separated heads.

For patched bodies, keep sorted unique original-row positions, n <= 64,
u32 full residual exception values relative to the same base, and zero packed
exception slots as admitted invariants. Replacement can change width, exception
membership or count; it is not automatically an in-place point overwrite.
The current “pad to 4” rounds the positions area: because of the n byte, value
addresses remain unaligned. The existing memcpy-based loads allow that;
do not accidentally introduce an aligned-u32 promise.

## Native peer targets are ready

Include the already qualified [provider](../../../peers/frame-codecs.cmake):

```cmake
include(${PROJECT_SOURCE_DIR}/workbench/peers/frame-codecs.cmake)
target_link_libraries(frame_bodies_measure PRIVATE
  workbench::turbopfor workbench::fastlanes_ffor)
```

The static peer builds are optional/excluded from all until linked. The provider
uses pinned/hash-verified upstream sources, the caller's ISA/tune for FFOR, and
TurboPFor's original separate baseline/AVX2 translation units. An AVX-512 caller
does not imply an AVX-512 TurboPFor implementation. Do not link ALP and
FastLanes FFOR into the same executable: they export overlapping FFOR symbols.

### TurboPFor

The actual calls are in [the boot control](codecs/turbopfor_check.cpp):

```cpp
#include <ic.h>
#define TURBOPFOR_DAC
#include <vp4.h>
```

Do not define TURBOPFOR_DAC before ic.h: the amalgamated header duplicates
inline DAC definitions through bitpack.h on that route.

- Plain FOR: `bitpack32(input,n,packed,K)`, then
  `bitgetx32(packed,row,K)`. Pair that accessor with the horizontal bitpack
  image, not a different SIMD bitpack layout.
- PFor direct access: `p4encx32`, then `p4ini(&prepared,&values,n,&K)`;
  points use `p4getx32(&prepared,values,row,K)`. Keep the updated values
  pointer, exception map and cumulative ranks. Price fresh preparation versus
  a retained context, its bytes and its lifetime; don't present both as the
  same operation.
- Whole-array encode/decode: `p4nenc32/p4ndec32` and
  `p4nenc64/p4ndec64` (byte counts returned). These are complete codec paths,
  not the same image contract as the direct-access primitive.

Native AVX2/AVX-512/NEON boot qualification covers all 128 DAC rows at widths
0..32, constants and sparse full-width exceptions, 12,672 points per ISA.
The source declares P4D_MAX=256. A single 256-row DAC block should be exercised
at every row, especially exception ranks 63/64,127/128,255, before timing it;
256 DAC was not part of that boot receipt. Plain FOR DAC was source-inspected,
not part of the original DAC execution receipt either. Do not claim wider
qualification from the target name.

The subsequent [local block qualification](codecs/README.md#per-block-point-follow-up)
now checks every 128/256-row PFor point, shuffled access, large exception prefixes
and horizontal FOR points on Linux AArch64. It also retains an upstream inline
accessor alignment diagnostic: patched bitmap words at image+2 require an aligned
placement for the instrumented caller. The explicit image-start 6-modulo-8
route passes without modifying the peer; account for its placement bytes.
This does not extend the native x86 receipt or qualify a fully sanitized library.

The boot control reserves initialized read slack (+64 bytes); include the
actual allocation and context in capacity and conditioning. A tight guarded
input is a different contract. Full-width u64 encode/decode passed, but this
does not invent a PFor u64 direct-access API.

### FastLanes FFOR

Use `<fls/ffor.hpp>`, `<fls/unffor.hpp>` and typed
`ffor::ffor(input,packed,K,&base)` /
`unffor::unffor(packed,output,K,&base)` for u8/u16/u32/u64.

These primitives consume **1024 values**, not 256. There is no n argument.
For a homogeneous K/base leaf comparison, group four Frame blocks, encode
and consume all 1024 values on each side, then normalize by four if reporting
per 256 rows. Never pass 256 initialized values, decode 1024 padded values and
divide the time while consuming only one quarter. On mixed real blocks, a
natural 1024-row FOR choice needs its real base/width and bytes priced; it
cannot silently inherit four different Frame bases.

For query comparisons, time decode plus the actual sum/filter consumer.
The 1024-value scratch can stay hot and be reused; do not charge an invented
second DRAM pass. For unpack comparisons, compare complete materialized
outputs. The pinned FastLanes named random-access benchmark was not a qualified
point-access control; TurboPFor is the useful current point peer.

These are actual primitive controls. Full FastLanes encoding/format and
complete system-query comparisons remain separate.
