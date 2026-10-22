# Intervals checks

- `search.cpp` retains independent ordered-search oracles across native unsigned
  widths, logical values, exact/protected tails, duplicate endpoints, borrowed
  selection, prepared numeric bindings and independent query batches.
- `sparse.cpp` checks 1920 fixture states with scalar endpoint/intersection oracles,
  near-top u32/u64 origins, zero groups, signed/wider queries, partial native tails,
  sparse checkpoint/read-grant variants, serial/independent execution, prepared
  windows, carried selected ranges and ordered replacements.
- `seams.cpp` checks Directory domains, Frame end-minus-one/tiny/wide projections,
  strided StreamPack companions and external filters, exact protected16/32/48 tails,
  full G64 padding, and4536 source-qualified checked edits/refusals.

These compile against maintained Intervals alone. The ordinary example also
links only `ikea::intervals`; no compact layout, bitmap or value owner is required.
StreamPack's separate checks cover compact admission/descriptor bytes, legacy
compatibility, payload/retirement/packing/transfer and four-plane damage.

Use the root pinned toolchain and build `ikea_intervals_search_check`,
`ikea_intervals_sparse_check`, `ikea_intervals_seams_check` and
`ikea_example_intervals_ordinary`. They are included in `ikea_validate` and CTest.
Performance qualification belongs to the [graduation record](../../../workbench/spikes/intervals/GRADUATION.md)
and its frozen native cohorts, not checker runtime.
