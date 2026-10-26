# Intervals checks

All five executables compile against maintained Intervals alone. Their expected
results come from independent endpoint/intersection walks, literal cuts and
original input values rather than a second call to the tested kernel.

| Target | Consequential coverage |
| --- | --- |
| `ikea_intervals_search_check` | Native unsigned u8/u16/u32/u64 counts 0..513; unaligned/exact protected tails; negative/wider queries; duplicates and sparse/inverted selection; logical domains; strided G16 deltas; high origins; numeric preparation, hidden skew/jumps, exact repair and independent query batches. |
| `ikea_intervals_cuts_check` | Every four-rank size combination 0..2 and every eligibility pattern; raw/selected literal-cut ownership, overlap, range preflight; nonzero near-top u64 origins, strings/multiword keys, 1025 intervals, nested selection and UINT_MAX sentinel arithmetic. |
| `ikea_intervals_sparse_check` | 1920 fixture states across G16/32/64/128/256, u8/u16 fields, u16/u32/u64 checkpoints, exact/full-capacity read grants, zero regions, partial native tails, serial/independent prefixes, carried ranges, overlap, phases and ordered replacement. |
| `ikea_intervals_seams_check` | Directory comparison domains; Frame logical endpoint 256, counts 0..256 and tiny projections; strided companions with poisoned siblings; exact/inverted filters; no geometry entry for empty selection; protected16/32/48 and G64 tails; 4536 checked append/resize/refusal/damage cases and an independent retained old image. |
| `ikea_intervals_wide_check` | Modular u64 reconstruction/search, partial checkpoint regions, every boundary-crossing range, xor-translated fields and in-place reconstruction, UINT_MAX count arithmetic. |

The ordinary example is a standalone Intervals consumer: cuts, rank filtering,
borrowed size editing and immutable run ends. The composition example adds
SeriesPack packed endpoints, prepared checkpoint search, cached numeric search
and independent queries. Neither requires a compact owner or tombstone format.
Build `ikea_example_intervals_ordinary` and `ikea_example_intervals_composition`.

The former Boundlist search suite was a duplicate of `search.cpp` and is removed.
Its useful generic cut/domain coverage now runs here. Its compact image histories,
literal legacy descriptors, bitmap/retirement and 41,472 boundary/resize cases
run under `ikea_streampack_compact_{geometry,cuts,edits}_check`; payload, U,
packing/transfer and four-plane effects remain in the other StreamPack checks.

Use the root pinned Linux toolchain and a build configured with CTest. These
targets are included in `ikea_validate`:

```sh
cmake --build BUILD --target ikea_intervals_search_check ikea_intervals_cuts_check \
  ikea_intervals_sparse_check ikea_intervals_seams_check ikea_intervals_wide_check \
  ikea_example_intervals_ordinary ikea_example_intervals_composition
ctest --test-dir BUILD --output-on-failure -R '^ikea_(intervals_.*_check|example_intervals_.*)$'
python3 ikea/test/headers.py BUILD --module intervals
```

The header check includes ordinary and author headers independently; explicit
native bodies are selected by the build profile. Checker runtime is not caller
latency. [Native qualification](../../../workbench/spikes/intervals/RAISE-BAR.md),
[graduation](../../../workbench/spikes/intervals/GRADUATION.md) and the
[live caller audit](../../../workbench/spikes/intervals/AUDIT.md) own performance
and integration evidence.
