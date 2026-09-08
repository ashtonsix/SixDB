# Local ARM evidence, 2026-09-08

Interpretation: [first findings](../../FINDINGS.md). Implementation and timer
contract: [probe](../../probe.md). These are single-threaded, CPU-0-pinned Linux
ARM64 VM measurements on an Apple CPU, Clang 21.1.8, `-O3`, generic tuning.
The guest reports the vendor but no model name; its reported frequency/cache
topology is not used to claim a hardware residency tier. No cloud instance was
provisioned.

| Retained run | Comparisons × repetitions | Minimum time | Intended use |
| --- | ---: | ---: | --- |
| [Broad screen](broad/artifact.json), `20260908T001437.110301Z` | 862 × 5 | 0.025 s | Density sweep, fixed population, packed/ranked controls, footprints |
| [Confirmation](confirmation/summary.md), `20260908T002153.951085Z` | 260 × 7 | 0.030 s | Selected density, dependencies, stable IDs, secondary reads, conversion |
| [Gap-policy follow-up](gap-policy/summary.md), `20260908T003030.377425Z` | 164 × 7 | 0.035 s | Tail-aware gaps, ordering, hotspot histories, corrected collision queries |

Minimum time is the Google Benchmark request for each repetition; it is not
the total experiment duration or a minimum operation latency. Comparisons
overlap between runs. Each scenario has one deterministic mutation history;
repetitions do not represent independent data seeds.

Full bundles contain measured uncommitted source, binaries, hardware/compiler
reports, build configuration, validation logs, raw repetitions, accounting,
tables, and the successful source-stable receipt. Optimized and separate
ASan/UBSan correctness checks passed in all three retained runs. Full bundles
were uploaded and verified by downloading their actual bytes. Git retains only
the broad screen's bundle reference; both focused runs retain compact samples
and accounting as well. Earlier exploratory implementations remain local and
are not the evidence for these findings.

## Which source/result to use

The broad screen already has bulk packed shifts, common projected-scan loops,
25% natural growth slack, actual-container summaries, inline root fences,
larger-natural-terminal controls, and post-mutation scans. Its collision-case
misses were sampled over the whole 64-bit domain, outside most shared prefixes.
Use the final follow-up for collision point/scan comparisons. Broad density
and fixed-population cases are unaffected by that query-domain issue.

The confirmation corrects initial collision point misses. Its final-state
collision scan misses still use the broad domain, so **its collision
`scan256_after` values are superseded**. Churn, secondary, rebuild, and density
results used in the findings do not depend on that generator. It also includes
an empty-scan fix; no timed case in the retained runs has an empty collection.

The final follow-up unifies initial/final query-domain generation, checks the
collision hit/miss domain, and adds `gapped_tail`, including its redistribution
counter. That method appears in both correctness output and measured rows.
All prior methods consume the same mutation digests across these runs. Changes
in timings between runs are not an isolated code-effect measurement.

## Regenerate or recover

From the repository root on Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
python3 workbench/spikes/trie-remapping/analyze.py \
  workbench/spikes/trie-remapping/evidence/local-arm-20260908/gap-policy
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/trie-remapping/evidence/local-arm-20260908/gap-policy \
  build/recovered/trie-remapping-gap-policy
```

Both compact follow-ups regenerate identical `summary.csv` and `summary.md`
without S3 access. Fetch accepts a focused evidence directory or the broad
`artifact.json` file and verifies the restored receipt. The full gap-policy
bundle was restored and its tables regenerated as a recovery check. A new
fetch destination is required; the shared [artifact guide](../../../../tools/artifacts.md)
describes source restoration and retry behaviour. Each provenance/receipt
records the exact selection regex, repetitions, CPU affinity, compiler, flags,
and measured source identity.
