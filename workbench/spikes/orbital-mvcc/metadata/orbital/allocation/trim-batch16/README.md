# Bounded16 trim control

This separate control replaces each Group's worst-case trim-run reserve with
16 spans. Apply [Direct4](direct4-batch16.patch) or [Direct8](direct8-batch16.patch)
to the corresponding frozen [direct-Domain](../routes/direct-domain/README.md)
source, then apply [comparison counters](comparison-counters.patch). The
[Direct4 cut](direct4-cut.json) and [Direct8 cut](direct8-cut.json) record exact
inputs and outputs. Main runtime, index and preceding controls are unchanged.

The enrollment guard checks before appending run 17, while permitting span 16
to continue coalescing. Unselected pages remain mapped and charged. Existing
reservation, helper, failed-span retry, Arena pins, refund and wait/pulse paths
remain. Empty Groups still retire through one existing whole-retirement job.

Before its warm pass, each call checks whether a currently cold Group has a
pending job or retained trim spans. If so, it defers warmer Groups and Windows
until an owner retry after completion. This also waits for cold batches with
no remaining backlog, including whole retirement; that extra completion round
is a deliberate conservative cost. Current hints govern the choice. There is
no persistent backlog flag or scan cursor.

Six initial native checks passed: the existing frozen reclamation, finite and
lifetime checks on both layouts, with assertions enabled. The
[readiness receipt](native-readiness.json) records the checks and limits.
Boundary/policy consequences and trim-heavy timing have separate probes;
this receipt does not establish their results or sanitizer qualification.

[The matched comparison](comparison-qualification.json) adds 30 explicit
`-DNDEBUG` histories across the unchanged Direct4, batch16 and
[heap metadata](../heap-control/README.md) controls. They check the actual
16-run guard, span16 coalescing, independently counted page/run inventory,
failed discard exclusion and charge, retry, a non-page-aligned floor, repeated
pending calls, cold-before-warm Group ordering, live bytes and whole retirement.
No warm Window or continuing-mutator fairness case is covered.

| Requested metadata per Group | A | B |
|---|---:|---:|
| Original Direct4 | 7,384 B | 8,400 B |
| Batch16 | 6,144 B | 6,208 B |
| Batch16 + exact metadata heap | 4,016 B | 4,600 B |

These totals exclude Arena/thread/libc setup, Object metadata and the first
8-byte Group-owner-vector capacity. They are requested bytes, not RSS.

[Logical work](logical-work.csv) shows fragmented A/B inventories need 20/35 jobs instead of one; the idle census
examines 13,356/39,456 page indices instead of 1,272/2,192. Contiguous inventory
and a long span16 tail still take one job. This is the price of the smaller
buffer with the existing start-from-zero traversal.

[Plain timing medians](comparison.csv) come from 12 alternating-order rounds
on the pinned AArch64 OrbStack guest, with 4 KiB pages. Batch16 fragmented
A/B drain takes 1.02/2.17 ms versus 0.62/0.96 ms; mixed inventory takes
3.41 ms versus 1.56 ms. These synthetic drains expose a maintenance cost,
not database or Zen5 performance. Heap has the same logical trim work and no
established timing advantage. Retain batching as a separately measured control;
its metadata saving does not establish that its repeated scans are acceptable
for the composed workload. Heap placement is separately attributable.

[`trim_probe.cpp`](trim_probe.cpp) seeds real mapped/charged idle pages outside
the measured interval. Mixed inventory fills A's identities to force a B Group,
then releases the blockers; it is not exactly alternating after that setup.
[`run.py`](run.py) builds matched plain and instrumented cuts, checks source
hashes before/after, runs the consequence histories and records timing:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/orbital/allocation/trim-batch16/run.py
```

Use `--owner`, `--helper` and `--rounds` to change the local run. The recipe needs
the frozen local cuts identified above. Raw outputs and compiler products stay
under ignored `build/orbital-mvcc-metadata/trim-batch16/root-comparison/`;
the small comparison and qualification receipt retain the finding.

`SIXDB_TRIM_COMPARE=1` adds owner-only `PoolState::trim_comparison`; omit it from
plain census and timing builds. The counter patch also applies to the unchanged
frozen controls. `jobs`, `ranges` and `pages` count finite Group submissions,
including retry spans and whole-retirement mapped pages. `retry_jobs`,
`retire_jobs`, `retire_pages`, `new_ranges`, `new_pages` and `max_ranges`
distinguish them. Counters record enrollment, rather than confirmed physical
success; `Statistics::trim_pages` remains the successful completion count.

`census_pages` counts the full payload indices examined in the idle census;
`enrollment_pages` counts indices examined while selecting spans, including the
prospective run 17. Live-mask builds for census and reservation validation are
separate counters. `batch_overflows` and `warm_pass_deferrals` count truncated
enrollment and deferred warm passes; `last_call_truncated` and
`last_call_deferred_warm` describe the last call. Polling retries affect `calls`
and scans, so comparison drivers must report their scheduling policy.

The 16-span bound limits retained run storage and partial-job range calls.
It does not bound owner census or live coverage, pages in a span, total Pool
jobs, or completion fairness under continuing low-index remapping. Pending
Groups are omitted from the idle census, preserving a conservative retention
floor while potentially postponing other trimming.
