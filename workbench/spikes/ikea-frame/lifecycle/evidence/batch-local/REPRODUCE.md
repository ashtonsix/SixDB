# Batch-program comparison recovery

Source capture f375808ae0780897cd8649138b317b6dbb18bfcd7f24bb9234e58fb976b3ce0b
is complete in source.tar.gz, with its original receipt. It is encoding
2c3bdf87/Ikea 2ce62539 plus four private lifecycle changes, without live NULL-fill
edits. Use pinned Clang 21.1.8, O3/native/generic, C++23; exact commands are in
identity.json. Exact qualified executables are in binaries/.

The four 8192-row u64 slices are in inputs/; inputs.json identifies original
paths, source/prefix hashes and offsets. They suffice to reproduce the actual
phase inputs without downloading complete columns. Point at those files when
replaying identity.json commands. Source construction and SHA assertions in
run-phase-batch.py identify the original local capture/input paths; remap those
paths to the recovered source/slices. Whole-column SHA provenance is distinct
from the hash-verified measured prefix. No slice is reselected from timings.

All 1536 median cells retain old/new/native programs, masks, old readers and
novelty. Bounded cells use 32 balanced trials, full-surface cells use 16; do not
pool their timings. Raw repetitions, independent original-coordinate verification,
focused check outputs and exact duplicate controls are archived. Tiny/empty
measurements are retained but not ranked. Equal-work comparisons are recorded
in controls.json; these are logical work/copy counts, not measured traffic.

Setup/final current-object disposal is outside phase clocks. Codec/stdalloc/query
scratch is excluded from native Pool capacity. Same-version fold retains its
primary; no-fold arms owe later work. This local AArch64 VM study selects no
Frame API/default and establishes no cold/Loom, fleet or production-tail result.
