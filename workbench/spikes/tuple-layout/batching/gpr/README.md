# Does an eight-byte GPR packet earn its place?

TuplePack's eight-byte interface currently operates on one original row. Before
adding multi-row shapes, this probe asks whether packing two/four/eight rows into
one GPR helps actual consumers enough to justify another maintained kernel family.
No Ikea implementation or interface changes are part of this experiment.

[Findings](findings.md) records the measured niche, stronger point comparison,
losing cases and the limits on any implementation that follows.

The possible niche is a small operation whose caller needs only a few rows or a
scalar materialized result. Full scans can instead fill a SIMD packet; making
only the small-packet comparison would hide that alternative.

## Comparisons

The same runtime layouts and maps feed these implementations:

- `point`: repeated prepared scalar point reads and checked writes.
- `gpr_inline`: a provisional word carrier with shared inline bodies.
- `gpr_compiled`: those word bodies behind one compiled endpoint per packet.
- `simd`: current native TuplePack operations with the same original-row count.
- `simd_full`: current native operations over eight times as many rows, for scans.
- `gpr_points`: the provisional GPR lowering applied one row at a time, then
  combined by the caller. This separates transfer improvements from batching.

The prototype reuses the module's point controls and checked mutation/effect shell.
It includes a structural fast path for consecutive physical bytes with a common
bit shift, allowing tight units to coalesce into one word. Other maps use bounded
scalar projection and the point writer's single-code/word/selected-byte techniques.
The prototype accepts the one/two/four/eight selected-code counts in this study;
it is not a complete proposed API. Compiled endpoint dispatch is per packet.

`word_hash` consumes the materialized eight-byte word with scalar arithmetic.
The same-sized SIMD comparator compacts its row slots into that word; full SIMD
scans consume its eight consecutive words. `sum` permits each carrier's natural
byte reduction. `update` uses the same byte toggle and checked replacement, with
width admission and source-qualified byte effects. Point calls admit each row
separately; packet calls admit the complete packet. Summary maintenance,
publication and CPS are outside this motivation test.
Physical store footprints can differ: a SIMD window may reissue preserved bytes
where GPR issues separate selected-byte stores. The ordinary comparison includes
the resulting journal work; it is not an equal-store-count instruction test.

The matrix varies one/two/four/eight requested rows, physical extents 1/4/8/16/64,
tight/64-byte stride, ordered/uniform versus reversed/mixed and spread/mixed maps,
full/alternating masks, sequential versus a deterministic random permutation of
1,024 resident rows. Counts and timings are per original row, including inactive
rows. A full SIMD scan requests more rows per call and is not a valid substitute
for an isolated small request. These are warm microbenchmarks, not cold-memory or
database-level evidence. Every timed implementation is independently checked
against the description's individual bits for reads and one update pass.

Run with the repository worker helper:

```sh
python3 workbench/tools/worker.py run workbench/spikes/tuple-layout/batching/gpr/run.sh \
  --machine zen5 --env 'TUPLE_PROFILES=avx2 avx512'
python3 workbench/tools/worker.py run workbench/spikes/tuple-layout/batching/gpr/run.sh \
  --machine neoverse-v2 --env TUPLE_PROFILES=neon
```

Use arguments after `--` for a benchmark filter. CPU pinning and sequential
repetitions are required; the runner captures sources, build cost and disassembly
alongside timings. Useful evidence must show where the GPR choice loses as well as
where it wins, and separate forced scalar materialization from native consumers.
