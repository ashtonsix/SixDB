# Orbital-backed read timings, Neoverse V2, 22 October 2026

Two worker jobs on `c8g.4xlarge` (Graviton4, 2.8 GHz), release preset, one
pinned CPU, seven alternating repetitions, 16 GiB Loom arena:

- `reads.csv`: [`bench.sh`](../../bench.sh), job `20261022T031650Z-1113663b`.
- `inner.csv`: [`bench-inner.sh`](../../bench-inner.sh), job `20261022T032708Z-b1dc0234`.

Full job records are under `s3://calico-fleet-artifacts/sixdb/workers/<job>`.
`NAME@orbital` rows read built layouts published as `engine::material` Objects
through one `Store::Scope` (`Scope::bind` per visit, `Selection::part` per
Part). Plain `NAME` rows read the flat Image in the same process. Every view
was checked against the model before timing, and all 92 counter checks passed.
Compare Image rows only within a job: the first Image-only panel ran on a
`c8g.2xlarge` with less memory in use and reads 20–40% faster at 1M and 4M.
