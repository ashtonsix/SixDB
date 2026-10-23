# Sustained owner histories: semantic qualification

The eight-batch [program-cost](PROGRAM-COSTS.md) and [Zen 5](ZEN5.md) comparisons
do not settle accumulated override debt or a folding cadence. `--sustained`
extends their existing `Machine`, writer, query and physical owner chronology;
it does not introduce another format, writer algorithm or public policy.
This first cohort qualifies the extension. It supplies no timing recommendation.

Nine arms compare native FOR, coalesced native FOR, inline repair/fallback,
batch overrides without folding, folding every 8 or 64 genuine publications,
and thaw/reseal. Native FOR and fold64 each have an exact duplicate. An odd
Williams square balances treatment position and predecessor twice in 18 trials.
Requested trials round up to complete squares.

Periodic folds use actual logical publication counts. Masked-out command batches
do not advance that count. Old readers can remain throughout or renew after a
specified number of genuine publications; their oracle follows the corresponding
original logical snapshot. A fold does not advance logical time. The next real
write clones the selected alternative, so these traces can observe retirement
after periodic conversion without fake publication. The final folded version
still retains its primary/alternative under the existing owner contract.

```sh
orb -m ubuntu BUILD/workbench/spikes/frame-phase-1/lifecycle/frame_phase_measure \
  --sustained INPUT 65 1 1 2 moving-hot 1 mixed verify 0xffff
```

Arguments after INPUT are command-batch count, requested trials, queries after
each batch, final queries, pattern, old-reader age, trace, mode and optional
original-block mask. Age -1 omits the old reader, 0 retains the initial reader,
and a positive age renews it before the next update after that many publications.
Patterns are repeated-hot/dispersed/moving-hot/novelty. Traces are mixed or
values-only. Plain mode disables the full Pool census; capacity and verify modes
are separate instruments. Verify checks every original-coordinate point on
current and old readers in trial zero, including after final conversion.
As before, setup and final current-object disposal are outside the phase clock.

The qualified AArch64/Clang21.1.8 release executable passes:

| Input / pattern | Batches | Reader age | Original block mask | Histories | Original points |
| --- | ---: | ---: | --- | ---: | ---: |
| Pressure / dispersed | 17 | 8 | alternating | 162 | 2,580,480 |
| URLHash / full-width novelty | 65 | whole history | all | 162 | 9,658,368 |
| UserID / moving hot set | 65 | 1 | first group | 162 | 9,658,368 |
| Pressure / repeated hot set | 9 | 1 | none | 162 | 1,400,832 |

All 648 histories check complete aggregates independently of physical encoding;
23,298,048 point comparisons preserve flags, values, old readers and folded
bases. Transition inventories independently check publication/fold counts.
For example, the 65-batch moving-hot trace selects only 33 batches: fold8 makes
five conversions including its final partial period, and fold64 makes one.
Counting command batches would give the wrong cadence. The none case publishes
and converts zero times; its verification deliberately reads unchanged values,
so this check is not a proof of payload-inaccessible query execution.

The original eight-batch CLI also passes 800 aggregate histories with unchanged
publication/conversion inventories. New JSON fields identify batches, reader
age, fold interval and conversions. No results from these capacity-instrumented
qualification histories are interpreted as throughput, pause distributions or
memory-policy wins. Histories beyond 65 batches remain unqualified here.

Source `816e564b47bf2fd252e566138b189bc4a793466be9f055e91edde0aac98a6b09`
changes only `phase_measure.cpp` from the frozen Zen 5 source. The
[checks and recovery](evidence/sustained-checks/) retain that source delta,
base-archive reference, exact executable identity and commands; the full archive
contains replacement source, raw histories and exact ELF. Current cold/selective
scan work has priority over choosing a lifecycle cadence from these controls.
