# Local ARM Intervals qualification

Selected final cohort from `run.py --cpu 8 --output build/intervals/local-20261022-v2`.
All 327 cells, all arms, 22 raw samples per cell: 7,194 samples. `summary.csv`
reports ns per complete operation, including losing cases. `bytes.json` separates
readable geometry, control, capacity and bindings; resize effect bytes are issued
source-qualified stores. Directory rows are seven-repetition summaries per process,
not raw inner timings. Source/binary identities and toolchain are in `identity.json`.

Warm Apple/OrbStack ARM Linux VM, Clang21.1.8 O3, vCPU8. No DRAM/Zen5/fleet ranking.
The primitive sources disable auto-vectorization and retain explicit native kernels.
The full archive preserves raw JSONL, per-process Directory CSV, commands and build
validation log. Recover with the shared artifacts helper and `artifact.json`.
Interpretation and rejected replacements belong in [findings](../../FINDINGS.md).
