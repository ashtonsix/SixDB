# Selected Boundlist evidence

The selection contains **33,100 median cells**, including all controls and losses
within the chosen final cohorts. It keeps serial latency, independent reciprocal
throughput, explicit operand/capacity bytes and execution setup separate.
`*-inputs.json` names and hashes every contributing raw CSV. `context.json`
records exact worker/source identities, machine profiles and raw artifact recovery.

| Matrix | Selection |
| --- | --- |
| `search.csv` | Prepared search and complete query/child/hash, N64/256/1024/4096/16384, u16/u32/u64, positive/duplicate/skew controls; serial and eight-chain |
| `ranges.csv` | Complete logical point/range consumers, u16/u32/u64/128, duplicate/gap/sparse cases and larger/scattered lists |
| `geometry.csv` | Final physical range consumers against current StreamPack, separate/paired and independent fused controls |
| `reads.csv` | Hot/small-population complete readers; all Direct/Width, U/full, fixed and R/U-placement controls in the runner |
| `reads-complete-cycle.csv` | Large complete 1,048,576-record cycles, replacing unstable short-prefix large-reader timings |
| `packed.csv` | Native versus SeriesPack Local endpoints at K8/12/16/24/32/40/48/64, complete point consumers |
| `cold-small.csv` | Populations1/4096 with explicit line coverage, point/range, fixed/mixed assigned counts, direct/manual/Loom controls |
| `cold-large.csv` | Final 524,288-source population, 2,097,152 requests per repetition, three repeats, explicit line coverage and the same execution controls |

`median_ns`, `min_ns`, `max_ns` are per request/value as identified by `metric`.
Independent values are reciprocal throughput, not elapsed batch latency.
`width` in cold tables is request concurrency; `mixed` selects fixed256 versus
varying64–256 assigned ranks. Execution names distinguish direct, manual hints,
Loom hints and Loom without hints. Memory fields are operand/allocation accounting,
not proof of individual cache-level service. Both endpoint and width stores are
present in the cold fixture; do not count its RSS as a single-format footprint.

Source files state query generation and complete-consumer feedback. Consolidation
checks completed worker state, at least three repetitions, stable accounting and
matching checksums across compared implementations. Timing runs also check against
independent expected traces. These checks do not make benchmark residence or
coverage assumptions true; [findings](../../FINDINGS.md) describe those limits.

`diagnostics/native-checks.json` retains the ten native semantic/material check
outputs from all five profiles. The bounded NEON count assembly/MCA output is
kept for diagnosis; its modeled cycle throughput is not a native timing.
Full linked disassembly, binaries, compiler commands, PMU results and process
memory receipts remain in the corresponding raw worker archives. The early
outlined-helper cohort, unstable large-reader prefix and first-line-only cold
hints are superseded diagnostics, not the selected comparison basis.

## Recovery and reproduction

`artifact.json` and `provenance.json` recover and hash the selected bundle.
From the repository root (prefix `orb -m ubuntu` on macOS):

```sh
python3 workbench/tools/artifacts.py verify workbench/spikes/boundlist/evidence/native
python3 workbench/tools/artifacts.py fetch workbench/spikes/boundlist/evidence/native build/recovered/boundlist
```

For full raw repetitions or a binary/source capture, use the relevant
`context.json` job's `raw_artifact` with `artifacts.py fetch` into a fresh ignored
directory. The [retention guide](../../../../tools/artifacts.md) explains exact
member recovery. The original worker jobs can also be fetched by their job IDs.

After recovering those worker result directories to `build/workers/JOB/results`,
the current selection is reproducible with:

```sh
python3 workbench/spikes/boundlist/consolidate.py workbench/spikes/boundlist/evidence-inputs.json build/boundlist/consolidated
```

Native runners and [findings](../../FINDINGS.md) explain how to reproduce new
measurements. The retained sources identify the original timings independently
of later edits to this checkout.
