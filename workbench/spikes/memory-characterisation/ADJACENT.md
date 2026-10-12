# Controlled adjacent-fetch observations

The original [probe.cpp](probe.cpp) already tests a forward neighbor after one
demand at both starting-line parities, with randomized cold/trained/hot controls.
Its [retained findings](FINDINGS.md) show forward benefit on the tested Zen hosts,
no repeatable neighbor benefit on the tested Intel hosts, and mixed Graviton
results. Those are historical observations under the recorded history. Their
conditional helper lacked an entry barrier, so they are not newly qualified
production prefetch inputs. Loom now imports only corrected-protocol facts and
learns missing facts through its
[cold hardware service](../../../loom/docs/hardware.md).

[adjacent.cpp](adjacent.cpp) extends that evidence with the controls needed to
interpret a hashtable's 64/96/128-byte spans. It does not choose a bucket format.
The complete plan mixes cases and arms before every repetition. Addresses come
from randomized regions in a first-touched, base-page mapping. Every trial
conditions the same surrounding cache lines and matched far control with
CLFLUSH or DC CIVAC, then performs one observer load. Hardware prediction history is not
reset or claimed cold.

| Axis | Cases |
| --- | --- |
| Starting position | Interior, first two lines of a page, last two lines of a page |
| Starting-line parity | Even and odd; same-pair and cross-pair forward cases remain separate |
| Distance | −8, −4, −2, −1, +1, +2, +4, +8 native cache lines |
| Stimulus | None, one demand, PREFETCHT0, PREFETCHT1, demand then target reflush, target hot control |
| Observed target | Neighbor, matched far line, or anchor itself in separate trials |
| Requested slack | 0, 100 and 300 ns; actual trigger-to-observer interval is retained |

The separate anchor trial is a positive control for software hints. A cold
neighbor after a software hint is informative only alongside evidence that the
anchor hint had time to work. The far target controls a broad warming effect.
Target reflush after the selected wait tests removal, while retaining possible
speculative refetch. Raw trials retain trigger latency, observer latency, actual
slack, addresses/offsets, page crossing, CPU AUX stamps and checked data values.

Conditional demand/hot/hint helpers fence before issuing a memory instruction;
this blocks an untimed wrong-path stimulus before the arm guard resolves. The
first unfenced cut exposed that contamination and remains diagnostic evidence.
On x86 maintenance finishes with MFENCE/LFENCE; ARM uses DSB SY/ISB. The runtime calibration and the
original probe's conditional touch helper use the same entry-barrier rule.

The timer uses fenced RDTSCP or barrier-ordered CNTVCT around one explicit load
at separate trigger and observer instruction PCs. Target values are checked.
Timebase calibration is retained; timer ticks are not reported as core cycles.
No PMU, IPC, stall or cache
occupancy assertion is made by this diagnostic. The zero-delay case still has
instrumentation slack. Page boundaries are virtual; physical page adjacency is
unknown. A latency benefit alone cannot name an L2 prefetcher or servicing level.

This control design draws on [FetchBench](https://tschlueter.com/research/publications/23-fetchbench/ccs23-fetchbench.pdf),
which separates forward fetching from aligned two-line-block fetching and
observes one line per experiment. [Intel's CLFLUSH specification](https://cdrdv2-public.intel.com/782156/325383-sdm-vol-2abcd.pdf)
allows speculative fetching around cache maintenance; cold/hot controls are
therefore measured distributions rather than guaranteed cache states.

## Run and interpret

The native worker entry point is [adjacent-native.sh](adjacent-native.sh). It
builds with the pinned toolchain, runs real Loom hardware tests, and records the
real service's forward-only fallback using explicitly synthetic known scheduling
fields. Those fields isolate the test and are not measured machine candidates.
Two seeds each run three repetitions of 128 trials per complete arm. Reported
cache geometry, actual page receipts, raw CSV, binary/disassembly and source
capture remain with the worker results.

```sh
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/memory-characterisation/adjacent-native.sh \
  --machine zen5 --capacity on-demand --idle-seconds 0 --deadline 1200
```

The diagnostic supports x86 CLFLUSH/RDTSCP and ARM DC CIVAC/CNTVCT. In the
standalone ARM process a disposable child first tests maintenance and timer
access; a trap or incompatible reported geometry produces an explicit unsupported
receipt and exit 77. ARM conditional stimuli use entry DSB SY/ISB barriers, the
sequence described in [Arm's speculation-barrier guidance](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Security%20Update%2008%20June%202020/Straight-line_Speculation-v1.0.pdf).
Production startup on ARM preserves unknown rather than testing privileged
instructions inside its threaded process.

CSV arm names stay stable for comparison: on ARM `prefetcht0` denotes PRFM
PLDL1KEEP and `prefetcht1` denotes PRFM PLDL2KEEP. These are distinct ISA hints,
not a claim of equivalent cache behavior. Metadata records the actual instruction,
timer/frequency and unavailable per-load CPU stamp. ARM trials use a pinned
sentinel with initial/final affinity checks; they do not claim RDTSCP AUX proof.

[adjacent_report.py](adjacent_report.py) verifies values, migration stamps,
address/distance/page/parity relations and the complete balanced trial plan.
`distributions.csv` preserves seed/repetition quantiles; `pooled.csv` summarizes
all repetitions without replacing them. The analysis reports benefit only when
all separated-control repetitions agree on at least 30% normalized cold-to-hot
latency saving. Below 15% is no observed benefit; disagreement, weak controls or
the middle band stays unknown. These thresholds are an observation rule, not a
statistical confidence probability or engine detector.

```sh
orb -m ubuntu python3 workbench/spikes/memory-characterisation/adjacent_report.py \
  build/workers/JOB/results
```

## Corrected native observations, 12 October 2026

The corrected runs used one c8a.medium (EPYC 9R45, Zen 5) and one c8i.large
(Xeon 6975P-C, Granite Rapids), pinned to CPU 0. Both report 64-byte lines and
4-KiB base pages. Reported L1D/L2 sizes are 48 KiB/1 MiB and 48 KiB/2 MiB
respectively; these identify the tested geometry, not the servicing level of a
timed load. Hardware, actual mapping receipts and timebase calibration accompany
each retained run.

Each host supplied 1,990,656 checked trials. Independent verification checked
raw/binary/source hashes, complete unique sequences, balanced arms, address
geometry, data values and CPU stamps. All 5,184 cold/hot control pairs across
the two hosts were separated; the minimum cold-p10/hot-p90 ratio was 4.33 on
Zen and 5.42 on Granite. The captured corrected x86 source matches
`450049ad4d8578af60de4d7bd5d5054edc4ad5dde0a6dd768003d540085ff12e`.

These are ranges of **per-repetition target medians**, not pooled medians, at
300 ns requested slack. Actual median trigger-to-observer intervals were about
320 ns on Zen and 333–337 ns on Granite. Interior rows cover both starting
parities unless stated otherwise.

| History and observed target | Zen 5 | Granite Rapids |
| --- | --- | --- |
| Cold / hot controls | 140–150 / 20 ns | 137–148 / 24 ns |
| One demand, forward +1 line | 20–30 ns, benefit in all repetitions | 27–148 ns, mixed; unknown |
| One demand, backward −1 line | 140–150 ns, no observed benefit | 144–148 ns, no observed benefit |
| T0 hint, forward +1 line | 30 ns, benefit | 27–147 ns, mixed; unknown |
| T1 hint, forward +1 line | 150 ns, no observed benefit | 27–147 ns, mixed; unknown |
| T1 hint, anchor positive control | 20–30 ns, benefit | 27 ns, benefit |
| Demand at last page line, forward across page | 150 ns, no observed benefit | 147 ns, no observed benefit |
| Demand, matched far target | 150 ns, no observed benefit | 147–148 ns, no observed benefit |

Zen's forward +1 benefit at both parities, including odd-to-next-pair, and lack
of a consistent backward benefit support directional forward behavior under
this history rather than an aligned two-line-pair interpretation. Reflushing
the neighbor removes the consistent benefit. Far and cross-page targets had no
benefit in every tested stimulus/delay cell on both hosts. Larger distances
include unknown cells, so the sweep does not establish an exact fetch depth.
T0's Zen neighbor takes about 90 ns with zero requested slack and about 30 ns
with 100/300 ns; T1 remains cold despite a successful anchor hint. A caller
considering a second bucket line should therefore distinguish demand, T0 and T1
histories. These observations alone do not change the LUT or justify eliding a
software hint.

Granite's mixed forward repetitions remain unknown. There is one host per
architecture, no hidden-predictor reset, and no PMU or cache-level attribution.
Neither CPU name nor bucket tiling substitutes for a measured observation.

The final implementation was checked again natively after quarantining the old
table facts. Both binaries contain 12 exact contexts, 11 width candidates,
**zero imported training candidates and zero known forward parities**. The real
bounded fallback completed in about 1.1 ms: Zen learned benefit for both
parities; Granite left parity 0 unknown and learned no benefit for parity 1.
That small fixed-offset startup history differs from the diagnostic's broader
variable-offset history; its result remains a local conditioned observation.

## Corrected ARM observations

The c8g.medium Neoverse V2 run (`20261012T233419Z-7e0f643f`) completed the same
1,990,656 balanced trials with 64-byte lines and 4-KiB base pages. Reported L1D,
L2 and L3 sizes were 64 KiB, 2 MiB and 36 MiB. MIDR was `0x410fd4f1`; the
recorded kernel was `7.0.0-1012-aws`. Its standalone maintenance/timer child
succeeded. CNTFRQ reported 1 GHz, providing a 1-ns timer quantum; actual base
mapping receipts showed no huge pages.

Independent raw verification checked the complete trial plan, addresses, values,
affinity receipts and hashes. All 2,592 cold/hot pairs were separated, with
minimum cold-p10/hot-p90 ratio 3.97. Every tested neighbor and far conditioned
cell showed no observed benefit across all six repetitions. That covers the
tested directions, distances, parities, page positions, stimuli and waits; it
does not prove that the hardware has no spatial prediction under other histories.

At 300-ns requested slack (about 357 ns actual demand-to-observer median),
forward medians were 141–145 ns, backward 142–145 ns, cold 142–145 ns and hot
33 ns. Both PRFM forms left the neighbor cold while their separate anchor
controls reached 33 ns (PLDL1KEEP) and 35–36 ns (PLDL2KEEP). The last-page-line
forward crossing was 141–143 ns. No servicing cache level is identified.

Captured ARM source SHA256 is
`74bb2427d2a940ed739df54202a12ede93e4bfae5769ef18ebfd5c189ad3e233`.
Assembly checks confirm entry barriers and one observer LDR between timer reads.
These standalone observations do not establish a safe maintenance route inside
Loom's threaded startup service: its ARM adjacency stays explicitly unsupported
and unknown. The local 128-byte-line/4-KiB-page VM correctly refused this sweep
because its matched far controls would not fit the required neighborhood.

## Evidence and recovery

The selected evidence keeps every seed/repetition distribution, including all
negative controls, with hardware and mapping receipts. Raw trials, disassembly,
binaries, build/test logs and captured sources remain in the verified worker
archives. Pooled summaries and full conditioned classifications regenerate from
those archives; they do not replace repetition-level evidence.

| Evidence | Worker job | Recovery reference |
| --- | --- | --- |
| Corrected Zen matrix | `20261012T230913Z-fd66ac6a` | [artifact](evidence/adjacent-zen5-20261012/artifact.json) |
| Corrected Granite matrix | `20261012T230913Z-05bdc89b` | [artifact](evidence/adjacent-granite-rapids-20261012/artifact.json) |
| Corrected Neoverse V2 matrix | `20261012T233419Z-7e0f643f` | [artifact](evidence/adjacent-neoverse-v2-20261012/artifact.json) |
| Final Zen service/table check | `20261012T231936Z-c06edf95` | [artifact](evidence/adjacent-runtime-zen5-20261012/artifact.json) |
| Final Granite service/table check | `20261012T231936Z-adb1a473` | [artifact](evidence/adjacent-runtime-granite-rapids-20261012/artifact.json) |
| Unfenced Zen cut, diagnostic only | `20261012T225855Z-acf0e72b` | [artifact](evidence/adjacent-unfenced-20261012/zen5-artifact.json) |
| Unfenced Granite cut, diagnostic only | `20261012T225855Z-ab7e8604` | [artifact](evidence/adjacent-unfenced-20261012/granite-rapids-artifact.json) |

The unfenced cut is retained by reference to explain the control failure, not
as hardware policy evidence. Its conditional hot load could speculatively warm
the target; adding an entry barrier restored separated controls. The original
spike and runtime helper are corrected too. Protocol qualification is separate
from file-format version and is conjoined when matching contexts are merged.

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/memory-characterisation/evidence/adjacent-zen5-20261012 \
  build/recovered/adjacent-zen5
```

Local qualification of the final implementation passed all twelve Loom runtime
checks, four focused ASan/UBSan checks with always-on stack-use-after-return,
the hardware-service TSan check, and offline inference/lookup regressions.
