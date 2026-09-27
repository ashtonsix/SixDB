# First formal-model investigation

2026-09-27. The [plan](PLAN.md) received independent review before implementation,
then the models received separate semantic review and deliberately broken
controls. The resulting six models explore complete bounded graphs locally.
They do not freeze every part of Orbital or prove their own imported contracts.
The captured suite contains **80 cases**: 24 exhaustive completions, 33 expected
bad-variant counterexamples and 23 reachability/boundary witnesses. All matched
their expectations, with no incomplete or unexpected outcomes. Six positive
configurations check temporal progress as well as safety. Total child-process
elapsed time was about **135 seconds** across the sequential campaigns.

## What changed our understanding

**Registry recovery must account for old submitted writes.** The first retention
draft let a registration write disappear with its process. That contradicted the
native runtime's physical lifetime rule. After correcting the model, a short
counterexample appeared: submit registration, crash, reload an old registry,
let the old write become durable, then collect using the stale registry. The
collector deletes the base required by the newly durable root. This is a lost
durably registered obligation; its client grant has not yet been delivered.

`RetentionClosureLateRootLoss` catches actual missing reconstruction material,
not merely a stale cache. The tested candidate explicitly drains submitted root
writes before reading the registry. This requires a recovery service contract
which the current simulator does not expose as a generic port. A durable
generation/namespace fence is another possible approach; it is not modelled.
Neither candidate has been promoted into the brief. Cursor updates and releases
remain atomic control transitions in this leaf, so this is not a complete
enumeration of storage failure boundaries.

**Durability receipts need a stated failure interval.** Two historical receipts
can arrive without two copies ever coexisting: write in A, start transfer to B,
lose A, then persist in B and learn both receipts. The normal admission model
admits this history and preserves the data under its one-total-loss assumption.
It does not establish a fresh one-domain-loss allowance starting at admission.
`AdmissionCoverage-historical-boundary` retains this as a witnessed limit of a
stronger claim, with no bug toggle. Eligibility, repair and renewal of protection
need that distinction when the admission/recovery protocol is refined. No
omniscient live-holder check was inserted to make the witness disappear.

**Local release requires the final durable position.** In the reservation leaf,
W announces 5 locally but receives a remote minimum of 17. Releasing locally
before durably fixing 17 lets its successor choose 10. The control detects a
real reversal of conflicting transactions' grant order, not just an incorrect
phase flag. Releasing after the local fix permits a conflicting successor while
W's remote fix remains undelivered. This supports the existing brief rule at
the model's abstract durable-command boundary.

The same leaf witnesses the W → broad waiter B → otherwise independent C convoy.
Skipping B makes the no-overtaking property false; it is not labelled a safety
failure of all possible alternative policies. Reversing acquisition order for
one of two spanning writers produces an actual wait cycle. The positive temporal
checks establish finite-cohort completion under concrete service fairness, not
starvation freedom under an endless stream of younger arrivals.

**Recovery needs ordering evidence as well as values.** Transactions retains a
completed read through a shard reset. Dropping its bound permits a later writer
to choose an earlier position and contradict that actual observation. A separate
control checks the serial result, so the failure is not only a missing-metadata
assertion. The positive cases also witness recovery of the same immutable `c`
after one shard fixes/releases and before another fixes, and a reader seeing one
installed participant while waiting for another.

**Publication checks needed independent result semantics.** Review found that
the initial checked-read model could always abort and still pass. Its corrected
durable decision now binds the received evidence and accepted result. Independent
properties reject always-abort, final-result-only comparison, missing checkers,
wrong invocation/profile, changed read cut and corrupted published result.
The recovery witness registers an old-read root, overwrites the source, crashes
before grant receipt, reconstructs the old bytes after restart, and publishes
the matching read-only result. A separate witness covers decision persistence
before its receipt. Late prior-context reports may cause an abort in this model;
discard-and-continue availability is not checked. Collection is reachable, not
proved eventually complete under the publication fairness assumptions.

## Local sizing

All recorded cases use the shared TLC artifact with SHA-256
`ab4694601923fd5ac06452abbf847c366a5054a3d739552085edd6ed986c29ec`,
Java 21.0.11, a 512 MiB heap and Linux/aarch64 in the local OrbStack VM.
The one-worker growth ladder completed without state constraints or symmetry:

| Dimension | Complete distinct-state counts | Largest elapsed time |
| --- | --- | ---: |
| Backend users: 1 → 2 → 3 | 235 → 3,219 → 46,243 | 1.11 s |
| Required checkers: 1 → 2 → 3 → 4 | 548 → 5,326 → 64,118 → 944,458 | 43.53 s |
| Producer LSNs: 1 → 2 → 3 | 401 → 18,687 → 825,923 | 29.84 s |
| Transactions: basic → one reset with aborts | 626 → 23,981 | 6.58 s including temporal checking |
| Reservations: bridge → two spanning writers | 730 → 1,960 | under 1 s per check |
| Retention: existing roots → dynamic registration and drain | 203 → 1,380 | under 1 s per check |

The largest one-worker cases used about 620 MiB peak RSS, including JVM/native
overhead outside the Java heap. These are model-checking costs on this machine,
not SixDB latency or throughput. Short cases include JVM startup; the timings
are single observations without CPU isolation, not calibrated hardware forecasts.

The identical four-checker graph completed with four workers in **14.12 s**,
versus **43.53 s** with one, approximately 3.08× faster. Distinct/generated counts
matched exactly. Four workers used about 635 MiB peak RSS. This is one measured
scaling point for that case; it does not justify linear scaling to a large server
or a different temporal state space.

Graph growth matters more than the encouraging absolute times. The last checker
step grew about 14.7×, and the last LSN step about 44.2×. Four checkers does not
test additional failure domains; three LSNs does not test another producer or
an authority handoff. There is no basis for claiming an arbitrarily larger
Cartesian product will fit simply because these cases finish.

## Coverage and next sizing decision

The initial suite distinguishes exhaustive completion, the exact intended
counterexample and a reachability witness. The [case catalog](cases.json)
specifies those expectations. In particular, a timeout, unexpected property
failure, deadlock or TLC internal error cannot pass a negative control.
The two temporal controls retain legal infinite behaviours: other work remains
runnable while an omitted recovery/service step prevents the target obligation
from completing. Disabled actions are disabled in their fairness clauses too.

The local evidence supports a maintained quick suite and a separate growth suite
before any bare-metal expenditure. Size future large runs by adding one
consequential interaction, completing adjacent sizes, and budgeting the entire
chosen suite toward the roughly one-hour target. Do not project from a growing
queue or treat that time target as achieved for models not yet written.

Important open boundaries remain: concrete journal/authority replacement and
permanent coordinator loss; general epoch scheduling and fixpoint confluence;
multiple-writer MVCC beyond these fixtures; unbounded-arrival starvation;
distributed retention; physical capacity deadlocks; and a checked refinement
mapping to the native implementation. Handoff's certificate audit is deferred
until it can expose a real protocol choice rather than restate its own imported
safe-prefix premise. The brief and physical brief are unchanged.

## Retained evidence

The [quick suite](evidence/quick-v1/summary.json),
[growth ladder](evidence/growth-v1/summary.json) and
[worker comparison](evidence/scaling-v1/summary.json) retain every authored case
and disposition. Their adjacent archive references recover complete frozen
sources, exact configs/tool metadata, individual receipts, progress and raw
counterexample traces. The local originals are under
`build/orbital-spec/{quick-v1,growth-v1,scaling-v1}`. Summaries' `result` paths are
relative to those recovered bundle roots. Earlier July-jar syntax/smoke runs are
development diagnostics and are not the evidence for these conclusions.
