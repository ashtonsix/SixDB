# Window growth and reuse

Does indexing idle Windows by reservation size remove searches through backing
that cannot satisfy growth, while preserving reuse choices and the prepared hot
path? This measures the allocator itself; it does not explain every cost in an
Ikea container append.

## Finding

Keep the exact reservation-size index. A fresh cohort of 6,144 independent
4 KiB buffers growing to 8 KiB previously inspected 18,871,296 undersized idle
Windows. The index avoids those list visits, with 6,143 global-head probes and
6,143 map-entry visits. A buried fitting Window and mixed-size independent
placement models retain the original choices. Default headroom is unchanged;
the buffers still relocate, copy and leave their old backing idle.

Six alternating fresh-process trials on pinned Spot workers measured these
median whole-cohort means. Growth rows include appending 64 bytes:

| Case | Zen 5, baseline → indexed | Neoverse V2, baseline → indexed |
| --- | ---: | ---: |
| 6,144 buffers, 4 → 8 KiB | 6.545 → 3.041 µs/grow | 11.450 → 4.041 µs/grow |
| 6,144 buffers, 12 → 16 KiB | 8.736 → 5.042 µs/grow | 13.762 → 6.360 µs/grow |
| Prepared 4 KiB reuse | 33.87 → 33.63 ns/op | 49.45 → 52.48 ns/op |
| Prepared 16 KiB reuse | 33.52 → 33.70 ns/op | 49.41 → 52.45 ns/op |

The median paired changes for 4 → 8 KiB are −53.7% / −64.8%; prepared reuse is
roughly flat on Zen 5 and +6.1% (about 3 ns) on V2. We accept that V2 cost for
this repair. The [Zen 5](../evidence/growth-zen5/comparison/context.json) and
[V2](../evidence/growth-v2/comparison/context.json) receipts retain every paired
sample, separate counters and source/binary identities. Both native runs used
131,072 prepared operations; the current runner defaults to 1,048,576. The
compile-contended local results remain attribution diagnostics, unselected.

Window records grow by 32 bytes, included in their existing charge. PoolState
grows from 368 to 424 bytes; index nodes are per distinct registered reservation
size and remain controller overhead outside the payload/record budget. Payload
mapped/reserved/idle accounting is unchanged. The cold search can still visit
eligible size entries with no idle members, and alignment can reject eligible
reservations. This is a repair of undersized traversal, not a general bound on
all selection work.

All 16 Orbital checks pass in Release and ASan/UBSan with always-enabled stack
use-after-return checks on the local Linux host. Focused cases cover ranking,
uncertain mapping retries, cancellation, trim failure, Holds, refusal, rollover
and implicit Arena teardown. Independent review caught and repaired a final-Hold
teardown error in the first draft; the reviewed version matches the native core
hashes. Complete Ikea container lifetimes are a separate consumer comparison.

## Reproduce

Run on Linux with the pinned toolchain:

```sh
python3 workbench/spikes/allocation-reuse-temperature/growth/run.py \
  --output build/window-growth-comparison --cpu 0 --trials 6
```

`native.sh` runs the same comparison through the ordinary worker launcher, using
`SIXDB_RESULTS` and `SIXDB_CPU`. It chooses another allowed CPU for reclamation
when available; one-CPU workers use that CPU for both. `--checks-only` and
`--sanitizers` support a small qualification pass without collecting timings.

The runner freezes the current source with `capture.py`, then reconstructs the
pre-index baseline using the small reverse patch and exact before/after hashes
in `baseline.json`. Only the private header and Window TU differ. A changed
candidate must have its patch and hashes reviewed and regenerated; the runner
refuses a stale patch. Both versions compile all their own Orbital TUs: their
private layouts differ. Within each version, common TUs are shared between the
timing and separately instrumented counter executables.

Each case starts with a fresh Pool. Growth timings contain only `grow` and writing
the appended 64 bytes. Initialization, accounting, complete old-byte/new-byte
checks, the independent placement model and zero-charge close occur outside the
interval. Six fresh-process trials alternate baseline/candidate order.

- Uniform live cohorts of 64, 256, 1,024 and 6,144 buffers grow 4→8 KiB or
  12→16 KiB, with ordinary requests or explicit initial VA headroom.
- One older fitting Window is buried behind undersized idle inventory. It must
  remain the chosen replacement.
- Mixed prepared sizes exercise global LIFO choices against an independent
  model while grown buffers of several sizes remain live.
- Prepared 4 KiB and 16 KiB allocation/release loops guard the common reuse path.

`samples.csv` and `summary.csv` report whole-cohort means and paired changes,
not per-operation latency percentiles. `counters.csv` comes from a separate replay:
list visits, undersized list visits, global head probes and map-entry visits are
distinct. Uninstrumented rows have zero counters; instrumented rows have zero
timings. An indexed search can avoid list traversal while still probing the head
and visiting an entry whose Windows are all active.

Mapped/idle/reserved bytes are Pool accounting, not RSS. Metadata bytes,
`sizeof(Window)`, `sizeof(PoolState)` and index-node counts expose structural cost;
they do not constitute a complete allocator heap census. Source receipts and
binary/instrumentation hashes remain beside results. Native evidence should
decide performance policy; local VM timing is an attribution aid.
