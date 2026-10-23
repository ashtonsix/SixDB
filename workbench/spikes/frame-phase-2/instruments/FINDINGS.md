# Read calibration and native controls

Front A, 23 October 2026. These are read-only native-loop measurements,
not Frame throughput, measured bus traffic or a selected execution interface.
The prior [memory findings](../../memory-characterisation/FINDINGS.md) already
showed the distinction between random concurrency and sequential streams;
[phase-1 scans](../../frame-phase-1/scan/FINDINGS.md) retained both pruning wins
and scattered-cold losses. The new instrument adds private concurrent mappings,
explicit cohorts and actual per-worker intervals. Complete Frame/peer cells
still have to run under the same trace and account for metadata and output.

## The useful calibration

Each worker pins before first touch. Sequential SUM reads every u64 and checks
its modulo-u64 answer. Random chase uses deterministic shuffled cache-line
cycles, with 1/4/16/32/64 independent chains and no timed index array. Complete
edge/coverage checks and independent expected endpoints validate the cycles.
Private guarded mappings use base pages; actual smaps/NUMA receipts remain in
the archives. One warm pass precedes each timed interval. Cache service is not
inferred from footprint or conditioning.

The original fixed-work panel ran equal pass counts per worker. The Graviton
large scans exposed very unequal finishing times despite sub-ms starts and
near-one thread running fractions. At 96 cores, a 256-MiB scan took 16.6–64.6 ms
per worker. Its common overlap was only 25.6% of cohort makespan. Those curves
measure completed-batch throughput; they are **not steady saturation ceilings**.
The cause of the unequal service is not attributed.

The revised `--steady` panel uses a common 200-ms deadline and bounded chunks
(64 KiB of scan work or 4,096 chase loads). Workers retain their actual completed
units, distinct lines visited, timestamps and running fractions. All workers
stop their clocks before any worker verifies a potentially large partial scan.
It also reads process page tables once per cohort, replacing the wasteful
per-worker scan of the entire process's mappings. Receipt collection is outside
timing in both versions.

Zen 5 common-window rates, 256 MiB mapped per worker. Scan is useful GB/s;
chase is **nominal addressed-line GB/s**, not measured line fills or traffic.

| Cores | Scan AVX2 | Scan AVX-512 | Chase, 32 chains AVX2 | Chase, 64 chains AVX2 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 49.62 | 49.72 | 14.00 | 22.93 |
| 8 | 56.52 | 55.98 | 55.79 | 56.00 |
| 32 | 225.45 | 224.96 | 222.77 | 223.50 |
| 96 | 530.76 | 509.37 | 399.60 | 398.89 |
| 192 | 1056.41 | 1015.26 | 782.46 | 780.53 |

The guest reports 32-MiB LLC groups of eight cores. The first group's plateau
is visible without using the rest of the host. More groups increase aggregate
useful work; a one-group plateau is not the whole-host bandwidth ceiling.
At 96/192 cores the one-chain chase remains far below 32/64 chains, so independent
requests still matter under concurrency. This microkernel does not select a
Loom ring width or predict Frame's chained acquisition cost.

Both steady profiles have 20 cells and 160 samples. Minimum common-overlap
fractions are 99.30% (AVX2) and 99.16% (AVX-512); primary/duplicate rate ratios
are 0.9963–1.0022 and 0.9959–1.0023. The native and ASan/UBSan checks each pass
96 cases and seven refusals per profile. The local NEON checks cover the new
control flow too; **there is no native Graviton common-window panel**.

The retained fixed-work curves cover six footprints, ten core counts, scan and
five chase widths, 360 cells/2,880 samples per ISA. The fixed Graviton scan rises
from 32.75 useful GB/s at one core to 787.78 at 192; its unequal drain means
that number has the completed-batch interpretation above. Fixed-panel duplicate
ranges are 0.9259–1.0908 (Zen AVX2), 0.9299–1.0280 (Zen AVX-512), and
0.8984–1.0280 (Graviton). Small differences in those curves are not rankings.

## Counter limits

On Zen 5, the exposed generic L1D-miss event opens but stays zero in the old
controlled chase sweep; generic LLC-load-misses is unavailable. Core cycles and
instructions respond. The guest exposes no uncore traffic PMU. These events do
not establish a serving tier or permit a DRAM-bandwidth claim.

On Graviton 4, generic L1D misses respond. The PMU catalogue exposes
`armv8_pmuv3_0` as type 10, and no CMN/DMC device. Raw L2/L3 refill sensitivity
is still unqualified. `cache_events.py` can probe events separately from basic
cycles/instructions to avoid oversized counter groups. Do not substitute event
availability for demonstrated sensitivity and CPU/kernel event semantics.

## Cost and next use

Ashton asked for sparing use of expensive full-host instances. The existing
full-host evidence is enough for this calibration. The planned Graviton steady
follow-up is dropped; its quota-failed attempt allocated no instance. All
completed large workers are terminated.

The earlier c8a.2xlarge pilot already exposes eight physical cores in one
reported 32-MiB LLC group. Reuse that small shape for within-domain questions.
Further calibration verifies topology before timing and sweeps core counts
within the required cache group. Smaller guests can expose a partial group, so their results are not
automatically whole-CCD measurements. Use one ISA when the question is bandwidth,
reuse built probes, and reserve a brief larger-host run for a specific remaining
scaling question. Complete Frame query comparisons on small guests come before
another broad saturation sweep.

The native codec foundation is [qualified separately](codecs/README.md).
The bounded [IEEE corpus recipe](corpus.py) retains original Jena numeric
lexemes and ALP fixtures rather than inheriting phase 1's scaled integer law.
Actual peer encode/decode plus consumption, native query-engine controls,
complete same-trace Frame residence/saturation and wider change support remain
front A work.

## Recovery and source qualification

Selected curves retain all measured cells and their losing/duplicate controls:
[fixed Zen 5](evidence/fixed-zen5),
[fixed Graviton 4](evidence/fixed-graviton4), and
[common-window Zen 5](evidence/steady-zen5). Raw samples, per-worker mappings,
linked assembly, compile commands, source archives, PMU intervals and exact
executables remain in each verified S3 bundle. Selected bytes match the archive.

The fixed workers share capture
`d1e77b82ff77f2f27c222ed33db3bf3ab42ab8151396bd60d0fba6290ffb040a`
(all 3,648 captured source files compared). The steady worker uses
`7cf59154d57a3caecaa2a4e8c300a71fe267eb78a812297dfe2a87198ef08301`
(all 3,649 files compared), differing only in the instrument and counter
runner. Before/after probe hashes match recovered executable archive identities.
The linked scan uses YMM, ZMM and NEON in the respective native builds.
No fully instrumented Ikea or codec-library build is implied by this
instrument's sanitizer target.
