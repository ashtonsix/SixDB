# Production StreamPack through Orbital

This is a concrete caller of the new `ikea::streampack` implementation. The old
spike codecs are not on this path. One Object contains four StreamPack planes
and a caller-owned holder map. Ordinary reads select once at a coherent Scope;
Loom consumes named views over the selected bytes. No allocator or Object facade
is added to Ikea or Loom.

The [material](implementation_orbital.h) uses actual Allocation storage for bytes.
PMR serves Orbital's catalog and Representation metadata only. A grouped layout
captures disjoint windows from one Allocation; a separate layout permits payload
and lengths growth independently. Growth initializes new private bytes and
rebinds addresses. Publication captures a validated representation, prepares a
finished change and installs it. Parts retain the shared immutable format even
after the Store and original format owner disappear.

The [check](implementation_orbital_check.cpp) compares against application facts
created before encoding. It exercises late discovery, retained readers and
escaping Parts, zero-payload/count-only append, partial groups, extent-only pack,
byte/effect refusal preservation, and private growth with/without Holds. Hard
transfer repairs the actual holder map and embedded successor ranks together.
The Loom check publishes at a returned native boundary, checks fresh-current
separately, and keeps all pending old reads on their original physical source.

## What the measurements isolate

The [benchmark](implementation_orbital_bench.cpp) measures complete publication
operations for Direct/Width geometry, grouped/separate storage, 3/15/64 assigned
rows, and replacement, append, soft pack and hard transfer. Count-only append is
included. Every case and repetition is retained, including losses.

Each individual operation includes Scope selection, clone or new destination,
the checked command, resolving issued effects, capture, publication, closing the
aged reader and bounded maintenance. The driver uses warmed, precreated distinct
Objects. Its whole-clone policy copies unchanged planes and initializes full
private metadata capacity. It is a policy control, not an optimal point-update
policy or a database/LMDB comparison. Final residual-reader cleanup is separate.

Reported p50/p99 are individual wall-clock operations with unadjusted clock
overhead. Interval throughput and whole-population throughput are separate.
Copied bytes, issued stores, selected retained footprints and allocator counters
have different meanings: selected footprints omit other history protected by a
Scope's cut and are not unique pages or RSS. Allocator counters include warmup.

The [retained Zen 5 trials](evidence/implementation-orbital-zen5/summaries.csv)
contain 360 summaries: all 60 cases, three repetitions, AVX2 and AVX-512. Real
publication histories passed both builds; local histories and all-case smoke
also passed ASan/UBSan. The [context](evidence/implementation-orbital-zen5/context.json)
records the source cut, verified binary identities, host and raw archive.

For the AVX2 control, medians across the three operation-quantile trials put a
three-row grouped replacement at 960/1,200 ns p50/p99 for Direct and
980/1,250 ns for Width. Separate storage was 1,520/1,950 and 1,570/1,890 ns.
Width copied 1,194 bytes versus Direct's 1,654, yet both issued only 19 command
bytes. Count-only append still copied 1,058 bytes to issue three. This caller's
copying, private initialization, full validation and holder-map checks obscure
small codec savings. No performance win is attributed to AVX-512 here.

These numbers establish a real integration control and expose its policy cost.
They do not establish near-optimal publication or an embedded LMDB target. The
native Spot worker and the initial runner-failure worker were both terminated.

## The sharing boundary

A partial-group append changes zero lanes that an old SIMD reader may load.
Old logical count does not authorize those stores. Preserve old lengths groups,
the current full directory and control. Even maximum-zero append changes count.

Sharing an unchanged payload while cloning metadata is possible through
`independent_shared` capture with actual Hold-backed owners. A shared pointer
alone does not pin a growable Allocation. New payload bytes can be disjoint from
an old occupied prefix, but old Holds prevent relocation; exhausted reservation
then needs replacement storage. The current material measures whole cloning
and does not claim the economics of that sharing policy.

## Reproduce

Select `-DSIXDB_SPIKES=streampack` and build
`streampack_implementation_orbital_check` and
`streampack_implementation_orbital_bench`. A quick benchmark uses
`--samples 32 --warmup 8`; quantile investigations should use more observations.
`--raw` retains individual samples. Prefix Linux commands with `orb -m ubuntu`
in the Mac workspace.

[The native runner](implementation_orbital-native.sh) records source and binary
identities, checks real lifetimes, and runs pinned complete-operation trials.
Use the ordinary worker helper; it defaults to Spot. After collection,
`implementation_orbital_report.py RESULTS --output IGNORED_SELECTION` produces
all summary rows and context with the original raw recovery reference. Retain
that small selection with the existing artifact helper. Raw samples, binaries
and compiler output stay in the worker's verified archive.
