# First executable contention slice

2026-09-25. The useful outcome is a small, replayable policy counterexample and
a model that makes preparation dependencies, protection and unfinished work
visible. It is enough to iterate on contention policy while the brief develops.
It neither settles the full simulator architecture nor tests how agreement,
durability, object folds or recovery are implemented.

## Reservation ownership can prevent the useful yield request

Run preset `reservation` with `older`, or choose **Reservation blocks an older
contender** in the browser. Three transactions share two keys:

| Transaction | Arrival, µs | Preparation order |
| --- | ---: | --- |
| T1 | 0 | B.y, then A.x |
| T2 | 10 | A.x, then B.y |
| T3 | 20 | A.x |

Shard A has a 100 µs period, B a 300 µs period, and control reports have 100 µs
delay. Reservations start after three failures; retries occur every epoch.
All keys use write protection. These are scenario inputs, not measurements.

T1 holds B.y and T2 holds A.x. T3 reaches the reservation threshold on A.x
before T1's A part does. T1 cannot reserve A.x because T3 already reserved it.
T2 later reserves B.y. Under the selected age policy, T2 rejects T3's yield
request and T1 rejects T2's. T1 is older than T2, but cannot emit the useful
yield request to T2 because T1 has not established a reservation.

At the 8,000 µs horizon, all three remain pending, with 183 failed attempts and
no discarded work. This is a concrete reservation-lifecycle limitation of this
age policy. The brief allows rejection and arbitration and does not claim that
this policy ensures progress; the example is not evidence that the brief is
inconsistent.

The work-based policy completes these three transactions, discarding one work
unit. The instantaneous global cycle oracle also completes them, but it first
invalidates T3's reservation repeatedly before resolving the retained-lock
cycle: four invalidations, one discarded prepared work unit. Even in the
idealized model, choosing the youngest participant is not automatically the
most direct way to remove the blocking dependency.

The next useful policy question is who can change an established reservation
when it blocks the party capable of making progress. Reservation expiry,
priority-aware transfer, requests without reservations and explicit arbitration
are different candidate policies with different consequences; this spike has
not selected or implemented those changes. A reservation blocks access but does
not itself establish that its owner will make progress.

## Completion-only latency can reverse the apparent conclusion

The seeded 80-transaction hotspot workload has 35% nominal local transactions,
otherwise two or three cross-shard parts, arrivals spread over 2,400 µs, and
80% hot-key probability at each part. At a 20,000 µs horizon:

| Policy | Completed | Pending | Completed p99, synthetic µs | Discarded work units |
| --- | ---: | ---: | ---: | ---: |
| Reject all | 7 | 73 | 314 | 0 |
| Favor older | 7 | 73 | 314 | 0 |
| Retained work | 7 | 73 | 314 | 0 |
| Older + cycle oracle | 41 | 39 | 19,328 | 122 |

The low p99 in the first three rows describes the small subset that completes.
It gives no latency bound for the 73 unfinished transactions. The oracle gets
further but still has 39 pending; it does not establish adequate throughput or
eventual completion. The UI therefore shows completion counts and oldest
pending age alongside latency, never latency alone.

For the paired 0% hotspot input, arrival times, transaction shapes, work weights
and cold-key choices are preserved. Only which requests address the hot key
changes. All 80 complete under `older`, `work` and `arbitrate`; rejection leaves
13 pending. This is one finite seed and parameter setting, not a workload-wide
policy ranking or a calibrated prediction for SixDB.

## What changed in our understanding

The brief is sufficient to model meaningful contention without first inventing
witness election or recovery. The strongest early outputs are dependencies and
counterexamples, not estimates of production latency. Keep the following
distinctions as the model grows:

- Granted protection and provisional reservations need separate state and
  wait edges. Looking only at granted locks misses the reservation that prevents
  the resolving request.
- Coordinator knowledge differs from the simulator's authored DAG. Successor
  registration and current-generation completion observation must be considered
  together before C2 authorization.
- Invalidation must discard transitive dependent preparation and fence delayed
  work. A generation number is this model's implementation choice, not a new
  requirement that the brief prescribe one.
- Agreed epochs, atomic invalidation, static authored dependencies and an
  instantaneous cycle oracle are strong abstractions. They need to stay visible
  when comparing with an eventual implementation or formal model.

The code keeps scenario generation, logical transitions, deterministic event
timing and UI projection small enough to replace independently. This is a
useful starting separation; there is no evidence yet for a larger framework or
a permanent production module split.

## Reproduce and inspect

[The compact comparison](evidence/comparison.json) retains all four policies
across seven authored/generated scenarios, with model, scenario and trace
identities. Full traces are generated on demand rather than checked into Git:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/compare.py \
  --output build/orbital-scenarios/comparison.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py \
  --preset reservation --policy older \
  --output build/orbital-scenarios/reservation.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py \
  --replay build/orbital-scenarios/reservation.json
```

The [checks](check.py) exercise this model's invariants and specific races, plus
48 combinations of policy, seed and equal-time shard order. Replay agreement
establishes reproducibility under the same sources and input. Neither those
checks nor the sampled schedule variations prove confluence, serializability
of real durable objects or distributed protocol correctness.
