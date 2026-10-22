# Read timings, Neoverse V2, 22 October 2026

`reads.csv` is the complete output of [`bench.sh`](../../bench.sh): worker job
`20261022T025022Z-42fd36b7` on a `c8g.2xlarge` (Graviton4, 2.8 GHz), release
preset, one pinned CPU, seven alternating repetitions per case. The full job
record, including the source capture and host receipt, is at
`s3://calico-fleet-artifacts/sixdb/workers/20261022T025022Z-42fd36b7`.
All 80 counter checks and the per-variant view checks passed before timing.

Times are nanoseconds per operation over Images of each variant's built layout
(no edits applied). Object visits are probes of one flat name table, a
stand-in for Orbital name resolution; the [README](../../README.md#read-timings)
states what that does and does not represent.
