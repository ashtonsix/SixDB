# Reuse, accessible capacity and retained pages

A 4 KiB request can reuse a previously prepared 64 KiB Window. This study
separates the cost of choosing closer backing from the cost of releasing pages.
Virtual address space is useful for stable growth; a large reservation alone is
not a reason to reject reuse. No production selector or reclaim policy changed.

A Window is private backing for one contiguous Allocation. A Group packs small
allocations; neither is an Object, Part or public memory-management interface.
The caller owns its body and readable padding. Holds retain physical storage;
Views borrow bytes under their owning lifetime and interpretation.

| Quantity | Meaning |
| --- | --- |
| `size` plus readable padding | Issued byte extent; body initialization remains caller work |
| `capacity` | Confirmed accessible bytes, possibly inherited from earlier use |
| `reserved_bytes` | Virtual address space retained for growth |
| Observed resident pages | Residency at observation time, not a lifetime promise |
| Arena charge | Conservative backing/metadata bound, not process RSS |

## Selector comparison

One current-source capture supplies common Orbital TUs. Only `window.cpp`'s
selector differs: first-ready control, first-ready preference within twice the
rounded requested extent, and smallest prepared ready fit with an exact-size
fast path. Both alternatives still reuse oversized backing when nothing closer
exists. Neither changes reservation eligibility, preparation or reclamation.

Six fresh-process trials rotate policy order on a pinned Zen 5 CPU. Each process
runs six traces with fresh Pools, four blocks and roughly 16,384 requests per
block. Touch modes write/read 64 bytes or the entire requested body. Timings are
block means, **not individual allocation p50/p99**. Separate deterministic
replays check retained payloads, disjoint live ranges and owned-capacity peaks;
all runs check zero charge after close. VM wrappers count actual calls.

Selected 64-byte-touch results, median of paired trial ratios:

| Trace | Smallest fit | Within-2x preference | Resource consequence |
| --- | ---: | ---: | --- |
| Same-size | +1.7% | +2.3% | No change |
| Repeated buried small | +4.4% | +6.1% | Removes 60 KiB live slack; Pool backing unchanged |
| Oversized only | +52.8% | +76.8% | No change |
| Mixed bursts | +36.6% | +44.3% | About 3% less accessible Pool backing |
| Small then large, both live | +1.7% | +0.6% | 135,168 → 69,632 accessible bytes; one new mapping avoided |

Full-body touches largely hide selector cost; oversized-only still loses
7.5%/10.5%. These narrow traces do not justify replacing first-ready. They also
do not establish a resident-RAM saving: accessible and resident bytes differ.
The original [temperature study](../FINDINGS.md) supplies wider selection context.

## Tail-page diagnostic and architectural direction

`tail_probe.cpp` dirties all sixteen 4 KiB pages of a real 64 KiB Allocation,
releases it, reuses that address for 4 KiB, and advises only its private tail.
It checks every live prefix byte, unchanged address, regrown writes and complete
shutdown. `mincore` observes the range without touching its data. This raw
diagnostic deliberately leaves Orbital's accounting unchanged; it is not an
integrated reclamation implementation or asynchronous concurrency test.

Twelve independent processes per mode on the pinned Zen 5 worker measured:

| Mode | Resident pages after advice | Advice median | Writing the fifteen tail pages again |
| --- | ---: | ---: | --- |
| Retain | 16 | No syscall | 45 ns; no minor faults |
| `MADV_DONTNEED` | 1 | 2.855 µs | 6.220 µs; fifteen minor faults |
| `MADV_FREE` | 16 | 4.905 µs | 500 ns; no minor faults |

All modes retain the same 64 KiB address range, capacity and conservative charge;
the 4 KiB live prefix stays valid. The DONTNEED result releases 60 KiB of resident
pages. FREE's residency is observed without imposed memory pressure, so it does
not establish eviction behavior under pressure. These are isolated synchronous
syscall/write episodes, not an integrated or multicore policy comparison.

`MADV_DONTNEED` can remove anonymous tail contents while keeping the address
range. `MADV_FREE` can make pages reclaimable later; writes after completed
advice cancel that eligibility. Neither makes a late background syscall safe
after a new owner has written. See the [Linux contract](https://man7.org/linux/man-pages/man2/madvise.2.html)
and prior [residency](../../orbital-views/residency/README.md) and
[TLB](../../orbital-views/tlb/README.md) measurements.

The coherent direction is cheap address reuse plus separate page maintenance
inside allocation. Group owns packing and shared-page occupancy; Window owns
one contiguous range and its writable prefix. Pool/Arena reserve and settle
physical work and charge. Loom chooses placement and when to request maintenance;
it does not reconstruct page safety. Allocation, Hold and View need no new
fractional or sparse-memory interface.

Start at idle or exclusive preparation boundaries. Preparation already withdraws
an idle Window before physical work and issues its Allocation after owner
settlement. Restoring only the useful prefix needs no new Hold tracking there:
the existing reservation can exceed the writable prefix. This does not reclaim
a long-lived small Allocation already issued from oversized backing.

Only that live-tail case needs additional state. One conservative maximum issued
prefix can protect all readers without a per-Hold registry. Include historical
readable padding: growth can reduce padding while an older Hold still covers it.
Reclaim only whole native base pages above that bound. Reset it only after every
use and foreign return settles. Pending discard
must exclude growth or reissue into its target until physical settlement.
Unused pages inside live Groups already contribute to `idle_bytes`; a proven
unused Window tail could use the same meaning. None of this is implemented.

Physical settlement is separate from construction success. If a preparer discards
an old tail and then its constructor fails or demand is cancelled, the owner must
still settle the confirmed discard; failed or uncertain VM work remains charged.
Raw `madvise` does not lower accessible capacity or Arena charge. Confirmed discard
plus an inaccessible tail preserves the conservative model and permits refund;
lazy-free advice alone does not. Retain the VA for later growth, which reacquires
budget before restoring access. Arbitrary interior holes do not help this case.
The 3 KiB Group cutoff is a separate palette/packing question, particularly on
larger base pages.

## Reproduce

From Linux (prefix commands with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/spikes/allocation-reuse-temperature/slack/run.py \
  --output build/window-reuse-slack/new-comparison --cpu 0
python3 workbench/spikes/allocation-reuse-temperature/slack/report.py \
  build/window-reuse-slack/new-comparison
```

The runner captures inputs, compiles common TUs once, builds only the selector
alternatives, and runs the harness plus existing Window checks. Add
`--sanitizers --checks-only` for ASan/UBSan with always-enabled UAR checks.
`native.sh` runs both comparisons on one worker and reuses the compiled common
TUs; `tail-native.sh` builds only the diagnostic. Both rotate tail modes through
twelve independent processes and require 4 KiB base pages.

```sh
python3 workbench/tools/worker.py run \
  workbench/spikes/allocation-reuse-temperature/slack/native.sh \
  --machine zen5 --env SIXDB_REUSE_TUNE=zen5
python3 workbench/spikes/allocation-reuse-temperature/slack/retain.py \
  SELECTOR_JOB TAIL_JOB --output build/window-reuse-slack/export \
  --evidence workbench/spikes/allocation-reuse-temperature/evidence/NEW_NAME
```

Pass the same job twice for the combined run. [Selected Zen 5 results](../evidence/slack-zen5/context.json)
link the two original worker archives with exact sources/binaries; that study
used separate jobs. Interrupted builds remain recovery diagnostics, outside
its comparisons. Bulky compiler outputs and raw blocks are archive material;
selected comparisons retain trial/resource costs alongside timing. No worker
is kept alive by these reproduction commands beyond the standard idle lease.
