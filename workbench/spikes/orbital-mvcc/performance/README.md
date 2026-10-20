# Historical versioned-consumer performance

This completed study compared actual Object/codec compositions, LMDB and
rejected metadata alternatives. Its timing executables, candidate matrices and
campaign launchers are retired. The [retirement guide](../../../benchmarks/retired-drivers.md)
records source recovery, preserved current correctness checks and offline reports.

[Iteration feedback](FEEDBACK.md) retains paired read/publication results,
larger-body surveys, interleaving, append/conversion and ready-Message service
observations, including negative results. The earlier
[thin-cursor findings](FINDINGS.md) remain a historical baseline comparison.
Each observation belongs to its own source/binary/host capture. Original
artifact references recover those measured bytes.

`run.py` now provides only `--summarize`, `--abi`, `--counters` and `--report`.
`broader_run.py` and `cooperative_run.py` keep `--report-only INPUT --output OUTPUT`.
Their original report calculations and validation remain available without
rerunning an experiment. Full reports need the recorded input files and binaries.

Current Object/codec/Loom consequence tests use their own private fixture,
independent of this old LMDB adapter and rejected implementation. Future
comparisons can link [actual LMDB](../../../peers/README.md) directly; scenario
setup, encoding, snapshots and timing remain the owning caller's responsibility.
