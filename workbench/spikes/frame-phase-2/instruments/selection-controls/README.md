# Native selection and reorder controls

Ikea HELPER owns this optional front-A control foundation and
`workbench/peers/frame-selection.cmake`. It supplies real pinned CRoaring32 and
Highway vqsort primitives, not a Frame mask representation or reorder policy.
No speed comparison or cloud worker.

The [Real Roaring corpus](../../../../datasets/real-roaring/README.md) retains
source identities and complete nonempty high16 windows. The closest
[analyser findings](../../../ikea-composition/probes/ikea-heterogeneous/operations/analyser-findings.md)
show severe sample aliasing and family imbalance; the
[operation findings](../../../ikea-composition/probes/ikea-heterogeneous/operations/measurements.md)
show that equal-cardinality clustered/dispersed masks differ and compressed
size does not establish a speedup over plain bitsets. We reuse bounded whole
windows with their lineage, include structural empty/full cases explicitly,
and keep preparation and run optimization separate.

Current `ikea::bitset256` has a portable 256-position law, and Intervals borrows
ordered endpoints and external rank selection. Neither fixes Frame's mask
storage. The peer controls need no adapter that imposes such a choice.

## Use the peers

Include `workbench/peers/frame-selection.cmake` explicitly from the repository
root and link `workbench::croaring` or `workbench::vqsort`. Their public headers
are `<roaring.h>` and `<hwy/contrib/sort/vqsort.h>`. Acquisition and compilation
are optional; the libraries are excluded from the default build. The provider
uses pinned Clang 21.1.8, caller ISA/tuning flags and unmodified upstream code:

| Peer | Pin | Qualified calls |
| --- | --- | --- |
| CRoaring 5.3.2 | `5fce11dbf8f8227125a466ce895266967e708816` | Native u32 sets: add/range construction, enumeration/iteration, membership, cardinality, inclusive rank, zero-based select, AND/OR/ANDNOT and their cardinalities, portable serialization and safe deserialization, run optimization |
| Highway 1.4.0 | `2607d3b5b0113992fe84d3848859eae13b3b52c1` | `hwy::VQSort` ascending/descending for signed and unsigned 16/32/64-bit keys, `K32V32` and `K64V64` key/payload pairs |

CRoaring uses the official 1.3 MB release amalgamation; Highway uses a 3.7 MB
source archive. Downloads are hash-verified. The Highway library compiles the
actual dynamic-dispatch integer/payload translation units, alongside upstream
core sources. Floating sorts, 128-bit keys, partial sort and selection are
outside this qualification. Equal-key payload order is unstable.

## Native correctness findings

[The compact receipt](qualification.json) records source, corpus and all six
executable identities. On Linux AArch64 with Clang 21.1.8:

| Profile | CRoaring | Highway integer / payload sorts | Highway implementation |
| --- | --- | --- | --- |
| Optimized | 108 images, 4,032 Boolean operations, 864 truncated-wire checks | 1,848 / 528 | Vector route enabled, including empty calls |
| UBSan | Same counts | 1,848 / 528 | Vector route enabled, including empty calls |
| ASan + UBSan | Same counts | 1,764 / 504 | Upstream HeapSort fallback, nonempty calls only |

CRoaring checks independent sorted-vector set algebra, iteration, membership,
rank/select, serialization extents and store canaries. Ordinary and
run-optimized portable sizes are recorded separately; each Boolean operation
checks all four input optimization combinations. Structural cases include
empty/full windows, array/bitset density boundaries, clustered/dispersed sets,
high16 transitions and the u32 endpoint. A separate analytic full-universe
case verifies cardinality `2^32`, rank/select, serialization and set difference
without enumerating four billion keys; it is outside the 108-image count.

The 96 real inputs are eight complete high16 windows from each of twelve
archives, reused from the existing heterogeneous study. Every 256-position cell
is retained. The bit-wire oracle rebases positions to low16; lineage records
the original bitmap and high16. Adjacent windows are arbitrary correctness
pairs, including a labeled empty predecessor, rather than a natural query mix
or a reconstruction of original whole bitmaps. The runner verifies dataset
identity, file hashes and lineage cardinalities. The other twelve images are
explicit structural controls.

Highway checks 22 lengths from zero through 65,537, seven integer distributions
and six payload distributions, in both directions. Independent sorted vectors,
multisets, monotonicity, integer store canaries and payload permutation/key
association detect errors; duplicate-payload negatives check that oracle.
The optimized ELF's actual u32 ascending dispatch-table slot 14 resolves to
`hwy::N_NEON::SortU32Asc`. Its linked targets are SVE2_128, NEON_BF16,
NEON_WITHOUT_AES and NEON. Dispatch is established from the running program
and linked table, rather than inferred from the caller's `-march` flag.

### ASan changes the algorithm, and the empty fallback fails

At this pin, upstream `hwy/contrib/sort/shared-inl.h:151` sets
`VQSORT_COMPILER_COMPATIBLE=0` on AArch64 ASan. Consequently the sort uses
HeapSort even though the dynamic target still reports NEON. ASan results
qualify that fallback; UBSan supplies instrumented vector-route coverage.
Every compiled peer translation unit is instrumented in its sanitizer profile.

A valid non-null `uint16_t*` with length zero reaches upstream
`vqsort-inl.h:161`'s HeapSort. Its early return covers one key, then the
build-heap counter subtracts from zero and underflows. The original debugger
sample shows zero input length and a huge counter. The runner separately
reproduces this non-passing case, terminates its own process after a two-second
resource guard, and records the failure. That guard is not a latency result.
Optimized and UBSan empty calls pass; the ASan nonempty counts do not hide an
empty-case pass or qualify vector vqsort. No compatibility macro is overridden.

## Reproduce and recover evidence

From the repository root on native Linux AArch64 (prefix with `orb -m ubuntu`
from the Mac):

```sh
python3 workbench/spikes/frame-phase-2/instruments/selection-controls/run.py
```

The runner incrementally builds only `frame_roaring_check` and
`frame_vqsort_check` in three isolated profiles under
`build/frame-helper/selection-controls`, using one build job and CPU 8. It
reuses the prepared sample at key
`536c2f7c742270d974839b658fa20ef0c88342666d8dac5b7dc3021f6fddeaef`;
the existing [window recipe](../../../ikea-composition/probes/ikea-heterogeneous/prepare_data.py)
can recreate it from the pinned Real Roaring input. `--windows PATH` selects
an existing prepared sample; `--synthetic` explicitly reduces qualification
to structural cases. `--build-root PATH` relocates builds. A cold provider
downloads its pinned peer files.

The full command/source receipt, compatibility macros, linked dispatch and
original empty-fallback diagnostic have a verified
[recovery reference](evidence/artifact.json). The compact receipt selects
identities, final counts, algorithm distinctions and that failure; routine
build output and reproducible binaries remain ignored. There is no throughput,
native x86, floating-order or stable-sort qualification here, and these
controls select no Frame mask representation or reorder policy. Parent wiring
and broader Frame comparisons remain with front A.
