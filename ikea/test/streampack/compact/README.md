# Private compact-image checks

These are StreamPack implementation checks, not a public standalone geometry API.
They preserve useful coverage from the retired Boundlist owner while generic
search/cut/domain checks live under Intervals.

- `ikea_streampack_compact_geometry_check`: Absolute Ends, Sizes and Relative
  Ends, fixed/zero geometry, liveness, literal legacy/current descriptors,
  partial initialized G16 tails, native packets, original-coordinate rewrites
  and exact source-qualified issued stores.
- `ikea_streampack_compact_cuts_check`: all four-slot 0..2-size geometries and
  every tombstone pattern against the same independent literal-cut oracle as
  borrowed Intervals; raw and explicitly selected geometry must agree.
- `ikea_streampack_compact_edits_check`: 41,472 boundary/suffix-resize cases,
  carrier and G16 crossings, refusal before bytes/effects, and selected prepared
  completion while the original named source and backing remain coherent.

The public StreamPack checks separately cover value semantics, U, payload,
retirement, packing/transfer and four-plane effects. CTest and `ikea_validate`
include all three private-image executables.
