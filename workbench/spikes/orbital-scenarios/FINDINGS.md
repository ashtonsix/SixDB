# Component independence and fold semantics

2026-09-25, following the brief revision in `31dba60`. The useful result is that
preserving a pending decision does not require pausing unrelated collection,
and read-scope overlap need not enlarge an arbitration component. A separate
payload probe makes the brief's execution freedom concrete. These observations
come from deliberately small models, not a distributed implementation.

[The retained study](evidence/component-study.json) contains 24 comparisons and
the fold probe, with source-referenced fixtures, policy inputs and source/trace hashes. Its 500 µs retry
period and 1,200 µs arbitration delay are synthetic inputs unless noted otherwise.
[MODEL.md](MODEL.md) owns the assumptions; [HISTORY.md](HISTORY.md) retains the
predecessor's useful counterexamples and reproduction pointers.

## Independent components need independent opportunities

Two two-transaction preparation cycles use disjoint keys. The first starts at
0 µs, the second at 700 µs, while the first arbitration is pending.

| Collection policy | First / second component collected, µs | All four complete, µs | Discarded work |
| --- | ---: | ---: | ---: |
| Keep each component independently | 500 / 1,000 | 2,900 | 2 |
| Pause all collection for a pending verdict | 500 / 2,000 | 3,900 | 2 |

The first verdict arrives at 1,700 µs in both cases. Independent collection starts
the second decision before that verdict without replacing the first reservation.
Selection, delays and release behavior are otherwise the same. This isolates
avoidable coupling in the old global hold; it does not test distributed collection.

The stale-verdict control still matters. On the three-transaction preparation
cycle, replacing reservations every 500 µs with 1,200 µs arbitration completes
0/3 by 8,000 µs and rejects 13 obsolete verdicts. Both retention policies complete
3/3 by 4,900 µs. With 100 µs arbitration all three policies complete by 2,000 µs.

## Compatible readers need not join or wait

Two preparation cycles have separate write keys but share a read scope. At
700 µs a local reader and writer arrive on that shared key.

| Reservation semantics | Initial components | Reader completes, µs | All six complete, µs |
| --- | ---: | ---: | ---: |
| Read/write compatibility | 2 of size 2 | 700 | 2,400 |
| Any scope overlap | 1 of size 4 | 3,200 | 3,300 |

Both discard two logical work units. The reader can pass the read reservations;
the writer cannot pass either existing read protection or those reservations.
The late writer connects the two active components. This candidate policy makes
it wait for their decisions rather than restarting them, and it completes in the
finite fixture. That is an alternative worth retaining, not a proof that deferral
handles arbitrary dynamic merges or continuous arrivals well.

## The hotspot remains a problem

Independent collection does not help when most contention joins one component.
Across the five retained 80-transaction seeds, independent and global collection
have identical completion counts at 20,000 µs: 23, 24, 16, 21 and 26. Between 54
and 64 transactions remain unfinished. Moving the lifetime boundary alone has
not solved that workload. The oldest anchor, choice of what work to preserve,
and repeated discovery/invalidation remain separate questions.

## A fold can combine writes without a transaction lock lifecycle

The integer-delta probe admits whole transactions against boundary protection,
then combines five contributions in all 120 orders and 14 binary groupings.
All 1,680 schedules produce the same two counters **and** completion/deferred
metadata. Protecting one counter defers every transaction touching it, including
its contribution to the other counter; the two remaining schedules agree too.

Changing the operation to return each intermediate counter value produces six
different observable results across six orders, even though every order leaves
the counter at 6. Thus final object equality is insufficient for the brief's
fixpoint requirement. An object's operation/result semantics determine the
available freedom; a key-only conflict graph cannot discover that freedom.

This is a finite algebra check over unbounded integer addition. It is not a
parallel executor, a model of arbitrary database transactions, or a replacement
for preparation protection. The scheduler and fold probe remain separate so that
neither silently supplies missing semantics to the other.

## What to investigate from here

The [reconsideration](reconsideration/README.md) now goes beyond broad read
protection: it examines 38 read/write SQL and ETL cases, prior art, and whole
alternatives that remove component arbitration or preparation locks. Seventeen
finite histories distinguish valid overlap from cycles, stale bulk results and
partial visibility. These earlier comparisons justify neither mandatory retained
read locks nor component-wide reservation; the retained history probe is not a
performance comparison of the replacement candidates.

Composition with actual object results and the fold remains unresolved. The
current scheduler also assumes authored DAGs and complete global collection;
concurrent merging, partial verdict application, authority and recovery remain
unmodeled. Those are limits of the existing candidate, not a reason to implement
its distributed arbitration before revisiting protection semantics.
