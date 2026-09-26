# Release writer latches after fixing the position

2026-09-26. **Holding writer gates throughout computation is not required by the
fixed-position safety argument.** Releasing them after every effect authority
durably accepts the exact position allows preparation to overlap and lets truly
blind replacements finish before earlier pending values. This audit supports
that change conditionally. It adds multiple pending versions and resource
admission obligations; it does not make ordinary overlapping SQL independent or
establish a general performance improvement.

The candidate keeps the complete mutation/effect envelope from
[the envelope audit](ENVELOPE-AUDIT.md). Gates become allocation latches. After
acquiring them in canonical authority order, reserve bounds and publish one exact
position everywhere. Only then release the latches. Each next overlapping
allocation follows previously announced positions, including pending ones, and
relevant read bounds. The old announcement remains until that attempt resolves.

This is an alternative lifetime for the same ordering mechanism, not a fallback
or a second optimistic execution mode. The independent root-owned pipeline
simulation studies its service costs; the present audit studies semantics.

## A concrete extra concurrency case

Initially `x=0`. A has position 10 and waits for a remote input before supplying
the full value `x=7`. B has position 20 and unconditionally replaces the complete
row with `x=9`, returning only its supplied result or completion.

| Event | Retain writer gate | Release after exact publication |
| --- | --- | --- |
| A waits during computation | B cannot allocate x | B allocates position 20 |
| B's complete blind replacement is ready | B remains queued | B commits x=9 |
| Point reader at position 25 | Waits for pending A/B work | Returns x=9 |
| A finally installs x=7 at position 10 | B may now proceed | x@25 stays 9; x@15 becomes 7 |

The serial explanation remains A, B, reader. B's complete later value dominates
A for this row and cut, regardless of whether A later writes, does nothing or
aborts. A's result belongs to A's own position; it need not describe the newest
physical state by the time it returns. The retained-gate control cannot complete
B before A.

Changing B to `x=x+1` preserves preparation overlap but removes independent data
execution. B's read waits for A, then returns old=7/new=8. The audit executes all
four retained/released and blind/RMW combinations. It contains no timing model.

## Multiple pending versions are a real obligation

The row's logical versions must be ordered by position, not installation arrival.
A late install at 10 cannot replace the answer at 20. The deliberately wrong
last-arrival-head interpretation returns 7 where the position-25 answer is 9.
History still needs the older value for reads between the positions.

For a read, walk relevant positions backward from its cut. An unresolved newer
possibility blocks it. A resolved complete value, including a deletion tombstone,
supplies the answer and can shadow older pending values. A resolved no-write or
abort supplies no version: continue to earlier possibilities. In particular,
B@20 doing nothing must expose pending A@10, not the old installed value at 0.
DELETE and unused envelope coverage must never share the same representation.

Resolution and late-message fencing are attempt-specific. Aborting B cannot
erase A's or C's pending slots, rewind the allocator frontier, or allow a late
commit to revive B. The audit checks abort fencing at the decision API, not a
transport or recovery protocol.

Partial multirow installation still requires completeness at the reader's cut.
B can commit `(x=2,y=2)` at 20 and install x before y. A point read of x can return
2; a read needing y waits. Earlier A can install `(x=1,y=1)` afterward without
changing either answer at 25. A range announcement must also precede a later
point allocation for an absent identity inside it. The small range case uses
finite potential slots, not a production symbolic interval manager.

## Blindness includes results, constraints and side effects

A primary-row payload being complete is insufficient. The three deliberately
unsafe controls bypass pending versions and consult only installed state:

| Operation | Earlier pending effect | Wrong result from treating it as blind |
| --- | --- | --- |
| `UPDATE k SET v=9` | DELETE k | Creates k or reports one affected row; at its serial position it must affect zero rows. |
| Replace a with unique value 9 | Another row b will become 9 | Both succeed, violating uniqueness. |
| Replace parent code with x, with ON UPDATE CASCADE | Parent and child change x to y | Comparing with installed x omits the required later child change back to x. |

Position-ordered program replay rejects all three. Correct controls wait for the
necessary predecessors and preserve returned counts, uniqueness and the child
effect. `RETURNING OLD`, triggers and other predecessor-dependent behavior carry
the same obligation. A SQL upsert can qualify as blind if its branches have the
same complete effects and result, with all other constraints resolved; ordinary
UPDATE does not automatically qualify.

Nonunique index maintenance need not always require the predecessor. The audit
keeps immutable index candidates tagged by row/version, then filters them through
the selected logical row version. B's value 9 is queryable before A resolves;
after A installs an older candidate 7, that candidate does not reappear at cut
25. A historical cut at 15 still returns it.

This model also checks the limit: B shadowing x does not resolve A's possible
effects on another row y. A predicate query may still wait for y, including when
the eventual answer is disjoint. Constraint observations on other rows remain
real dependencies. The projection scans the finite row universe to establish
completeness; it does not implement or cost a physical distributed index.

## Capacity must not introduce a metadata/data cycle

Pipelining consumes pending-version space. Backpressure in the wrong place can
invalidate the argument that metadata finalization is independent of data work:

1. A@10 occupies r's pending capacity and will later read s.
2. B holds allocation latches for r and s, and announces provisional bound 1 on s.
3. B waits for r's capacity to be released by A.
4. A reads s at 10 and waits for B's announcement to become exact or abort.

The cycle is `A data -> B metadata -> A data`. B's eventual exact position would
be after 10, but it never reaches that step. Capacity needed for publication must
be secured before creating read-blocking provisional announcements, or the
allocation must fail/abort rather than wait in this state. Capacity waiting
before any such announcement leaves A able to finish. Holding only writer
latches does not itself block A's source read.

The script checks these authored dependency graphs for a cycle; it does not
implement a capacity-limited allocator or prove a complete resource protocol.
Other metadata dependencies, including persistence and verification, must retain
the same independence. Source-history expiration remains the explicit failure
contract described in the previous audit.

## Assessment

Prefer release after exact publication if the design accepts a bounded pending
version ledger. It removes an unnecessary exclusion lifetime while retaining
the same immutable serial-position argument. It has a strict semantic advantage
for complete blind effects and can overlap metadata work for RMW. These benefits
do not require source renewal, victim selection or another execution mode.

The cost is explicit: more live possibilities, per-version resolution and
recovery, retention pressure, correct shadowing and capacity admission. Broad
envelopes still leave broad unresolved possibilities. RMW, predicate and
constraint reads can inherit the same long dependency chains, while increased
in-flight service demand can worsen their latency. This is a reason to bound
admission; it is not evidence that every released gate improves completion time.

Retained gates remain a simpler one-pending-writer implementation with stronger
backpressure. The new choice is justified by the additional legal overlap, not
by claiming fewer states overall or that a finite audit settles recovery.

## Evidence and limits

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/release_after_position_audit.py --output build/orbital-release-audit/audit.json
```

Seventeen authored histories pass their expected outcomes: fourteen positive
histories replay, three deliberately unsafe application histories fail replay.
The capacity dependency-cycle and preflight graph are checked separately.
The runner records its source hash and the reused admission helper's hash;
reproducible JSON belongs in ignored storage.

The model collapses allocation and exact publication into one step; it does not
test provisional-message interleavings. Positive reads use resolved logical
slots; the arrival ledger supplies the wrong-head comparison and index
candidates. It does not independently validate a physical version store.
Programs are small complete interpreters, not a SQL executor. There is no
network, crash recovery, GC, performance measurement or general progress proof.
