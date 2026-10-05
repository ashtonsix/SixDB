# Completed-cut follow-up

The first ownership run exposed a shared progress hot line after independent
object ownership removed other serialization. This follow-up compares that
bitmap/watermark with one cache-line-separated progress counter per producer.
It uses the **same supplied interleaved IDs and strictly ordered producer
streams**: producer `p` supplies epochs `round * producer_count + p + 1`.
Neither allocation of those IDs nor admission is measured.

```sh
bash workbench/spikes/orbital-views/control/run-cut.sh
```

Four allowed CPUs are required. The new targets/runner produce 24 separate cells
and leave the original 72-cell comparison unchanged. Native and ASan/UBSan probes
pass. One compiled `cut.cpp` is shared by both callers; compilation receipts are
recorded independently after timing.

For each update, both policies write an immutable 64-byte version with distinct
words and exact sum, release-publish its local head, then complete progress.
Versions stay retained until readers finish. A cut query resolves the actual
historical bytes behind its returned epoch, checks their independent expected
values, and checks the local installed head reaches that epoch. Preallocation
and final verification are outside timing; native writes, summary, publication,
progress, query work and small receipt counters are inside it.

The counter query acquires each producer's completed count, derives its next
missing epoch, and returns the prefix before the earliest missing epoch. A scan
may return a conservative cutoff as producers advance; it is not an atomic
snapshot of all producer counters. Queries are monotone for a reader and never
advertise an uninstalled version. The probe compares both policies with an
independent completion-array oracle under arbitrary **cross-producer** ordering.
Within a producer, the counter rejects skipping a round. It cannot represent
arbitrary local out-of-order completion without additional local holes.

Updates run at 1/2/4 pinned producers with queries every update, every 16 updates,
or final-only. Both policies pay the same final query/source resolution.
Readonly cells separately measure repeated queries against completed, warm
metadata/bytes; they do not estimate query latency under active writers.
Update time per publication is aggregate wall-time throughput, not individual
publication latency. Per-query timing is reported only for readonly cells;
dividing a combined run by its query count would mix writing into reader cost.

`queries` records actual reader work. `max_unqueried_updates` records local
cadence. `max_head_distance` is the largest distance between a producer's newly
published epoch and its last queried cutoff **before the next query**. It includes
real prefix holes/producer imbalance as well as querying less often; it is not
pure visibility staleness. The deterministic probe demonstrates exact cached-cut
lag without adding a contended oracle or timestamp to the timing loop. Query
checksums may differ across schedules because legal observed cutoffs differ;
publication bytes/sums and query counts must match.

A stalled/failed producer keeps both cutoffs behind its hole even if later
object heads are installed. A counter failure does not automatically become a
terminal no-op. The owner must supply that decision. This comparison shifts
progress work from writers toward readers; it is useful only where actual
producer order, reader frequency and scan width make that trade worthwhile.
It establishes an alternative under these assumptions, not a chosen protocol.
