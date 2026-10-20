# StreamPack extraction and cleanup evidence

[Absolute scorecard](scorecard.md). This supplements the original Boundlist
search/reverse/Loom evidence; it does not replace those results.

`reads.csv` retains old/current and independently fused complete readers.
`mutation.csv` retains old/current append and replace with source-qualified effects.
Both have five counterbalanced repetitions per cell. `packing.csv` has seven
repetitions per cell, shared aligned journal placement and the selected immutable
count capture. Its ns are per 256-rank block; B8 is an independent block stream,
not eight interleaved slots. Fragmented input is restored outside every clock;
independent byte/geometry checks run afterward.

`packing_initial.csv` preserves the earlier five-repetition experiment with
per-treatment stack journals and the uncached count. Its Granite serial losses
motivated the follow-up; the latter changes both placement control and the loop,
so their contributions are not separately isolated. No claim assigns all uplift
to one of them. Individual losses remain in both matrices.

`*-inputs.json` identifies selected raw CSV hashes. `context.json` joins exact
source captures, host receipts and verified worker archives, including all raw
repetitions, binaries and disassembly. `calls.csv` accounts for 2,040 audited
complete consumers: no ordinary kernel helper calls; memory-library calls are
allowed for mutation/packing. Native tests/examples and 192 actual retained-reader
histories per profile pass in the reader/mutation cohort. The packing follow-up
runs its independent oracle after every block.

`*-reader-sequences.json` compares all 180 old/current point/projection consumers.
Every pair has the same normalized instruction sequence after ignoring annotated
relocations and padding, while preserving immediates. This diagnoses zero extra
instructions at those extraction seams; it is not semantic proof or an instruction
latency model. Large x86 timing swings have wide ranges in both directions and
cannot establish a signed seam cost. Carried ranges are a separate composition;
the largest median loss is 0.121 ns/value, with gains elsewhere.

`control-audit.json` verifies frozen access/writer bodies and four moved leaves
against commit4fb8fcea0ae309f2cf19d69ee1f6159e3fa57dae. Shared public view/layout
and unchanged leaves isolate the access/mutation extraction; this is not a build
of the entire historical repository. `header-cleanup.json` records byte-identical
local complete executables for the dependency-only cleanup before the separately
qualified packing count change.

Replay the scorecard from this export with:

```sh
python3 parity-report.py .
```

To rebuild the matrices from recovered worker jobs, use `consolidate.py` with
`parity-inputs.json`. Source/control and assembly diagnostic scripts are included;
the source-origin audit additionally needs the recorded Git commit. Actual
residence is not inferred from allocated capacity or RSS. Logical operands,
allocated payload, metadata, binding and journal bytes have separate columns.
Search, prepared phases and actual Loom measurements remain in the earlier
`../graduation/` export. That export is unchanged and independently verified.
