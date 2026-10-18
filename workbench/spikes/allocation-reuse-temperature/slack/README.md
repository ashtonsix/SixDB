# Reuse, accessible capacity and retained pages

A 4 KiB request can reuse a previously prepared 64 KiB Window. This study
separates the cost of choosing closer backing from the cost of releasing pages.
Virtual address space is useful for stable growth; a large reservation alone is
not a reason to reject reuse. This study changed no production selector or
reclaim policy; integrated reclamation followed in the
[live-tail study](../../allocation-live-tail/README.md).

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

One captured source tree supplies common Orbital TUs. Only `window.cpp`'s
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

The architectural direction is cheap address reuse plus separate page maintenance
inside allocation. Group owns packing and shared-page occupancy; Window owns one
contiguous range. Pool/Arena settle physical work and charge. Loom chooses when
to request maintenance; it does not reconstruct page safety.

The subsequent [live-tail spike](../../allocation-live-tail/README.md) implements
explicit writer-extent relinquishment and asynchronous reclamation through ordinary
Pool maintenance. It replaces this study's tentative maximum-issued-prefix idea
with bounded reader endpoint counters, so an old wide reader can retire without
waiting for continuous prefix readers. Allocation, Hold and View need no public
fractional or sparse-memory interface. This raw diagnostic remains separate from
that integrated lifetime/accounting implementation.

## Live-tail consumer requirement

A StreamPack owner can exhaust its 256 ranks below its schema's byte limit.
That encoding ceiling does not require touching the full payload span. The
reported Iceberg–Swiss eager-zeroed arena and whole-stride copies were owner
policy, not a format requirement. The live-tail work follows that real consumer:
relinquish unused whole native pages while preserving useful bytes, VA, sidecars,
readable padding and older physical footprints. Interior gaps between packed
blocks remain a placement question; a tail operation does not reclaim holes.

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
