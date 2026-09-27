# TLC worker calibration — 2026-09-27

Question: for these two Orbital checks, does Zen5 or Graviton5 finish sooner
and cost less, and does using four TLC workers help?

Zen5 finished the comparable checks in 20–26% less wall time. At the recorded
Spot quotes, its completed-check compute cost was 17–22% lower. On-demand
journal costs were nearly equal; admission favored Zen5 by about 8%.
Four TLC workers reduced completed-check times by 3.67–3.80× on Zen5 and
3.78× for the Graviton5 journal check. This supports trying four workers on
Zen5 for similar checks; it does not establish a universal hardware ranking.

Each cell below is the median of two attempts, in seconds. Cost columns are
Zen5/Graviton5 ratios for completed checks only; below 1 favors Zen5.

| Check | TLC workers | Zen5 seconds | Graviton5 seconds | Spot cost ratio | On-demand cost ratio |
|---|---:|---:|---:|---:|---:|
| Journal authority | 1 | 96.610 | 121.296 | 0.834 | 0.987 |
| Journal authority | 4 | 25.436 | 32.076 | 0.830 | 0.983 |
| Admission domain loss | 1 | 162.625 | Both timed out at 180 | — | — |
| Admission domain loss | 4 | 44.281 | 59.584 | 0.778 | 0.921 |

Fresh `c8a.2xlarge` (AMD EPYC 9R45/Zen5) and `c9g.2xlarge`
(Graviton5/Neoverse-V3) VMs in `us-east-1a`/`use1-az4`: eight cores,
one thread per core, 16 GiB RAM, Ubuntu 24.04. Each case used a new JVM,
OpenJDK 21.0.12.1, `-Xmx512m -XX:+UseParallelGC`, TLC fingerprint 0,
and one or four workers. No CPU pinning, checkpoint adapter or live output sync.
Identical captured model/config bytes, runner, TLC jar, Java version and JVM
options were verified across all 16 attempts; native JDK binaries differ by ISA.

The recorded hourly USD rates were Zen5 Spot **0.172** (17:00 UTC quote),
Graviton5 Spot **0.1643** (12:00 UTC quote); on-demand **0.43108** and
**0.34776**, respectively (September 1 effective prices). Costs are elapsed
check seconds × rate / 3600, excluding startup, setup, EBS, collection and
other charges. These are quote-based estimates, not billed costs or future prices.

All completed journal runs reached **214,989 distinct / 2,092,799 generated**
states; admission reached **318,860 / 1,657,545**; all completed queues were empty.
The catalog's `JournalConcreteRecovery` label actually selects `JournalAuthority`
with `configs/journal/abstract-two-ballots.cfg` (`RequireRecovery=FALSE`): this is
an authority workload, not a concrete-recovery test. Graviton5's two admission
one-worker timeouts remain in [runs.csv](runs.csv); partial work is not a winner.

[Context](context.json) records exact source/tool hashes, parsed dependencies,
invocation, prices, hardware and limitations. Two cases, two repetitions and one
VM per architecture cannot characterize large heaps, eight-worker scaling,
checkpoint overhead or availability. Peak observed RSS stayed below 0.7 GB.

Full raw logs and captured sources remain in the existing [Zen5](workers/zen5.json)
and [Graviton5](workers/graviton5.json) S3 worker archives. Fetch either reference
with `workbench/tools/artifacts.py fetch REF build/recovered/NAME`. This directory's
[artifact reference](artifact.json) recovers the small analysis bundle, including
raw price receipts and `summarize.py`; after fetching workers into sibling
`zen5/` and `graviton5/` directories, run `python3 summarize.py WORKER_ROOT OUTPUT`
from that recovered analysis bundle to regenerate and verify the CSV and context.
