# SeriesPack lengths inside a BEC count consumer

This bounded adapter replaces the six-bit length child in the closed
[heterogeneous probe](../ikea-composition/probes/ikea-heterogeneous/README.md). It keeps the
same scan128 wire, population packets, checkpoints, dense BEC body allocation,
64-readable-byte body suffix, query bitset and authored `count_range` operation.
It proposes no public BEC/cursor interface and changes no production reader.

Three children feed the existing native prefix/address reconstruction:

- `specialized`: the old metadata provider in the new common `Source`/`Cursor`
  wrapper. This is not a byte-identical historical whole-pipeline baseline.
- `native`: current SeriesPack's private 16-byte striped region over the actual
  admitted length plane, with its own tile identity and stride.
- `materialized`: the ordinary bound reader into `u8[16]`. A bounded x86 refill
  screen chooses one global AVX2/AVX512 target; ARM uses NEON. This is a
  refill-screened control, not a claim of the fastest target for every count.

`Source` retains all owners and stores borrowed views by value. Its logical
length is the real body count N, separate from allocated scan128 capacity C.
A refill starts at the ordinal's 16-record checkpoint, including preceding
lengths needed for the exclusive prefix. Native reads may include initialized
slack in the owned tile. Ordinary decode stops at N and zeroes only the unused
tail of its scratch array, inside the measured refill. Empty requested ranges
perform no refill. Native metadata lanes survive across body pairs; the first
entry of a pair survives a checkpoint refill for the second entry.

The whole count measurements include both existing inline/split body-provider
cuts. Standalone refill measurements include a common `u32[16]` output sink;
whole count never constructs that array. They are distinct operations, and
their timings must not be subtracted to infer a separable boundary cost.

## Checks and measurements

Select `SIXDB_SPIKES=bec-packed-metadata` and build `ikea_bec_metadata_check` and
`ikea_bec_metadata_bench`. Use the pinned Linux toolchain and the repository's
[build conventions](../../../BUILDING.md). The check covers independent
wire/metadata/count oracles, logical tails, checkpoint/physical-tile crossings,
query access under ASan, source admission and owner/view copies and moves.

The runner reuses the existing [prepared windows reference](windows.json),
including all 256 cells of each sampled window. Twelve RealRoaring archives
with eight windows each join structural, random-half and a 129-block structural
tail workload. Six requested ranges cover full traversal, a nonzero first,
checkpoint crossings at 15 and 127, and the last block. Three standalone
refill positions include the final logical group.

```sh
python3 workbench/tools/worker.py run \
  workbench/spikes/bec-packed-metadata/cloud.sh \
  --machine zen5 --instance-type c8a.large \
  --sync-seconds 0 --deadline 1800 -- --target zen5
```

The x86 profile is x86-64-v4 plus VBMI, VBMI2, GFNI, VPOPCNTDQ and BITALG, tuned for
the named CPU. Compiled BEC providers need this feature ceiling; an AVX2
materializer label does not make the whole executable a pure AVX2 program.

All cases/repetitions run sequentially on CPU0. The materializer screen uses
AVX2/AVX512/AVX512/AVX2 process order, with nine refill contexts per target and
12 samples per context. The median of their nine AVX2/AVX512 median ratios
selects AVX2 below one, otherwise AVX512. The full comparison then retains
identical allocations in one process across specialized/native/materialized/
materialized/native/specialized blocks, with three 30ms repetitions each.
Each iteration performs 256 operations over eight windows; reported costs are
ns per operation, not per decoded value. `report.py` checks all 1,350 preflights
and 4,050 timing samples and keeps both paired ordering edges. Address and
footprint counters describe the actual run; residency is not assumed from a
nominal data-size label.

The [paired consumer findings](findings.md) retain every native/control repetition
and the remaining limitations. Full per-block sample rows are
[recoverable](../ikea-composition/archive/validation-20260911.md).
