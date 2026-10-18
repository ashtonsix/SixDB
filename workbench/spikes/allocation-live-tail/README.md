# Live-tail reclamation

Keep allocation extent, retained readers and physical backing separate. A caller
can relinquish a suffix without moving its useful bytes or VA. Ordinary Pool
maintenance then reclaims whole eligible pages on the Arena helper. This is a
common allocation contract; a finite home need not yield independently
reclaimable pages, and Window remains a private backing choice.

## Caller and lifetime

```cpp
// Include all sidecars and readable padding in the retained extent.
auto accepted = orbital::allocation::relinquish_tail(value, useful_bytes, padding);
// At a maintenance boundary, batch work across the Pool. Poll/wait through
// returned Progress; done covers currently eligible work, not reader retirement.
auto progress = orbital::allocation::trim_cold(pool, keep_idle_bytes);
```

The owner must authorize loss of the suffix and end unretained borrows before
changing extent. The same-size call marks inherited excess capacity reclaimable.
New body/padding cannot exceed the old extent; zero is allowed. The operation
performs no VM work and initializes nothing. Native bindings must be rebuilt
against the changed extent. Useful bytes, old Holds and VA remain valid.

The first real Window Hold admits a charged array of atomic reader endpoint
counters. The last page touched by a footprint selects a power-of-two page bin.
Maintenance protects the larger of the writer's retained extent and the highest
occupied reader bin's upper bound.
This needs no per-Hold list and leaves Hold/Storage sizes unchanged; a far reader
can retire while prefix readers continue. Bins can protect almost twice the
page-rounded reader endpoint. Metadata grows with log(reservation/page), not
reader count. Allocation-capture tokens conservatively protect the writer's
last relinquished bound; this does not infer smaller per-Part footprints.

Maintenance freezes each dispatched range. Growth inside the protected prefix
remains legal; growth reaching the dispatched tail conflicts until owner delivery.
Discard/protection must both succeed before refund. Failed or partial work stays
charged and unavailable until retry or confirmed restoration. Closing, capturing
or releasing the Allocation does not abandon accepted physical work. Reader
retirement itself does not trigger a notification/trim; revisit maintenance.

[Architecture](ARCHITECTURE.md) compares conservative watermarks/cohorts, reader
registries, bounded bins, small initial extents and copying. The
[prior diagnostic](../allocation-reuse-temperature/slack/README.md) separately
prices raw page advice; its out-of-band syscall is not a supported caller path.

## Evidence

The actual [allocation check](../../../orbital/test/live_tail.cpp) uses real byte,
VA, residency and protection oracles, paused helper/reply delivery, foreign
retirement, historical padding, duplicate Holds, capture/close, partial syscall
failures, budget refusal and regrowth. [Limits](CHECKS.md) distinguish what those
checks establish. The real StreamPack/ObjectStore consumer lives in
[implementation_orbital_check.cpp](../streampack/implementation_orbital_check.cpp);
it retains an old Scope/escaped Part while a newer prefix interpretation lives.

Six alternating fresh-process trials on pinned two-vCPU Spot hosts compared the
same harness with independently compiled pre-change and candidate allocators.
Both hosts used 4 KiB base pages; the helper ran on the other CPU. Native checks
passed before timing. [Zen 5](evidence/zen5/comparison/context.json) and
[V2](evidence/v2/comparison/context.json) retain all samples, controls and identities.

| Whole-loop mean, median across processes | Zen 5 baseline → candidate | V2 baseline → candidate |
| --- | ---: | ---: |
| Prepared finite allocate/release | 56.58 → 56.12 ns | 106.35 → 105.96 ns |
| Prepared Window allocate/release | 32.07 → 32.25 ns | 58.63 → 59.03 ns |
| Finite Hold + retain + two releases | 30.27 → 32.36 ns | 65.38 → 70.13 ns |
| Window Hold + retain + two releases | 30.27 → 54.37 ns | 65.16 → 95.40 ns |

Window tracking therefore adds about 24 / 30 ns per measured retention cycle,
even if reclamation is never requested. The first process Window Hold, including
lazy heap/budget setup, measured 1.38 / 0.62 µs median versus 30 / 46 ns before;
this is one cold grant per process, not steady first-grant cost for every Window.
Window records grow by 24 bytes (344→368 / 352→376); first Hold adds 20 charged
bytes for a 16-page reservation. Hold stays 32 bytes and Storage stays four.
General heap/controller overhead is outside those record estimates.

A cohort of 128 touched 64 KiB allocations contracts to 4 KiB each:

| Candidate operation, median batch mean | Zen 5 | V2 |
| --- | ---: | ---: |
| Foreground relinquishment | 4.88 ns/allocation | 8.46 ns/allocation |
| Async reclamation through owner settlement | 7.47 µs/allocation | 9.26 µs/allocation |
| Later mapping regrowth | 1.35 µs/allocation | 2.39 µs/allocation |
| First writes to the 15 reclaimed pages | 8.87 µs/allocation | 14.15 µs/allocation |

Mapped backing and observed residency fall from 8 MiB / 2,048 pages to
512 KiB / 128 pages; reserved VA stays 8 MiB. Regrowth preserves every prefix
and address, then incurs 1,920 minor faults per cohort on the first tail writes.
The retained-backing controls regrow in 9 / 17 ns and write those pages in
151 / 120 ns per allocation without those faults. These are whole-loop means,
not individual latency percentiles. Hold cycles are owner-local; this does not
measure foreign-release contention. Reclaiming immediately before reuse loses.

With five-page old readers, bins retain eight pages. After those readers retire,
trim reaches one page while both older and newer prefix Holds remain. The
watermark/cohort/exact page rows are explicit trace models, not timings of
alternative implementations. Exact tracking could save bin rounding; late
opt-in cannot reconstruct unknown historic footprints without conservative
retention, earlier enrollment or a per-Hold token.

Keep the implemented separation: cheap writer relinquishment, scheduled backing
maintenance and pointer-only native access. Accept the tracking cost where retained
readers need this behavior; reconsider prior enrollment if a real composed point
path starts granting Window Holds per operation. Allocation-backed Representation
transfers/opaque pins do not add those bin operations.

The [real Iceberg–Swiss phase comparison](../../notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
on Granite Rapids provides a separate consumer result: across three seeds, trim
reduced median Pool mappings from 3.652 to 2.661 GB and paired process RSS by
991 MB (about 945 MiB), taking 28.22 ms. This is Orbital's own before/after delta:
post-trim process RSS was essentially equal to compact heap (6.507 / 6.509 GB),
not 991 MB lower. Construction took 20.426 s versus
20.366 s for the same compact layout on the heap; retired growth backing still
contributed to the build peak. This used the earlier frozen v16c allocator before
the arithmetic-only bin refinement. Its [owning study](../../notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
keeps complete footprint and lookup comparisons; primitive timings here do not
attribute a container throughput change to reclamation.

The frozen 2026-10-16 implementation passed 17 Orbital checks in Release and
ASan/UBSan with always-enabled UAR, 18 Loom checks, and the actual
StreamPack/ObjectStore Release+sanitizer histories. These native runs do not
qualify 16/64 KiB hosts or every concurrent interleaving.

## Repeat

For a current-tree feature diagnostic, use Linux and the pinned toolchain
(prefix with `orb -m ubuntu` from the Mac):

```sh
python3 workbench/spikes/allocation-live-tail/run.py \
  --candidate-only --output build/live-tail/new-diagnostic --cpu 0
```

Choose allowed CPUs; a second allowed CPU runs reclamation. This freezes current
sources, checks real bytes and zero charge after close, and runs candidate
retained/reclaimed controls. It does not reproduce the historical paired figures.

The paired evidence above uses frozen 2026-10-16 sources. Recover its original
comparison, source captures and binaries through the retained references:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/allocation-live-tail/evidence/zen5 build/recovered/live-tail-zen5
```

The V2 reference is `evidence/v2`. The archived context records the original
invocations. `native.sh` launches that paired runner, whose reverse patch and
before/after hashes deliberately refuse newer allocator sources. A new paired
study needs a reviewed, regenerated control; do not refresh historical hashes
merely to bypass preflight. Retained measurements keep their original identities.
Raw sources, binaries and build output belong in the archive, not Git.
