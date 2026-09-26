# Progress, changing results and recovery

2026-09-26. The useful initial scope is **finite accepted transitions within a
possibly long-lived flow**, with application-defined input coverage, recursive
termination and output meaning. Retain reusable state where it pays, allow
recomputation where it does not, and keep computation progress separate from
publication authority and retention lifetimes. This is a spike recommendation,
not a new Orbital API or an adopted general dataflow runtime.

Engine, or the binding for a non-database application, owns operators, plans,
snapshot meaning, algebra and accepted outcomes. Loom owns scheduling and finite
resource budgets. Orbital supplies application-opaque durable objects, delivery
and the existing authority boundaries. A loop counter, watermark or empty queue
does not grant a transaction permission to publish. Existing requirements in the
[brief](../../../orbital/BRIEF.md),
[extension composition](../orbital-scenarios/reconsideration/COMPOSITION.md),
[worked ELT histories](../orbital-scenarios/ELT-WORKED.md),
[position pipelining](../orbital-scenarios/PIPELINING.md) and
[delivery study](../orbital-dissemination/DELIVERY.md) remain the composition
context; historical algorithms are alternatives to evaluate.

## Several orders serve different purposes

| Order or evidence | Its question | What it does not establish |
| --- | --- | --- |
| Transaction position `c` | Which application-visible versions and predecessors does this transaction observe? | How far a recursive algorithm has run |
| Witness shard epoch | Which input is admitted and which fold/pending transitions follow agreed history? | Completion of every transaction mentioned in it |
| Source cursor and captured cut | Which source changes are included? | A consistent application snapshot merely by taking unrelated cursor maxima |
| Dataflow time, e.g. `(input batch, iteration)` | Which changes and recursive round can this work affect? | Transaction commit order, real time or external-effect completion |
| Event-time window and source watermark | Under the declared source/late-data contract, which event-time interval can close? | That clocks, silence or a timeout prove absence of future corrections |
| Delivery/attempt identity | Which bytes, fragment and execution attempt are being tracked? | Which signed contributions belong in the result or which execution checks can be omitted |

Iteration can read newly discovered sources at the parent's fixed position when
the current transaction contract permits that discovery. It cannot add output
authorities outside the complete declared effect envelope. Internal handlers
must not wait for the parent to publish before they can finish its computation.
The initially selected checked-native lane remains local to all logical source
and effect authorities; a recursive call does not bypass that restriction.

For a fixed input cut, set reachability can be complete when all pending
derivations and messages are exhausted. A worker that retains the right to emit
another derivation still blocks completion even if every network queue is empty.
Dynamic fanout needs an ownership handover: the parent must remain represented
until its children have registered their obligations. Counting only currently
known child results can otherwise close a computation before its last child
exists. A crashed worker is missing progress evidence, not evidence of no work.

Timely's documented approach tracks capabilities and messages, propagating their
implications through graph path summaries; the frontier contains minimal times
under a partial order. This is a useful precedent for application-level progress,
not a durability or transaction protocol.
([Timely progress tracking](https://timelydataflow.github.io/timely-dataflow/chapter_5/chapter_5_2.html))

The [probe](progress_probe.py) uses exact central accounting for one loop, with
atomic transfer between a worker token and a message token. Its queues-empty
case correctly remains incomplete. Frontier `{(0,2), (1,0)}` is incomplete at
`(1,1)` because `(1,0)` can still affect it. Keeping only the lexicographically
smallest pair `(0,2)` and comparing it componentwise falsely reports completion.
A scalar minimum outer input batch is conservative for whole-batch completion;
it cannot express all useful inner-loop parallelism. Iteration 3 may finish while
iteration 4 keeps that outer batch open.

The prototype does not implement Timely's distributed protocol. Its initial
source inventory closes before progress is observed, and its token transfers are
atomic. Real duplicated/reordered control updates, crashed token owners, graph
reconfiguration and restoration need durable, authenticated progress evidence
consistent with the chosen transport. In particular, application's duplicate-safe
payloads do not make arbitrary progress-accounting updates duplicate-safe.

## Corrections and negative results change the output contract

Consider orders joined with payments, and an anti-join reporting unpaid orders.
The same pattern covers CDC enrichment, inventory reconciliation, alerts and
extension-hosted workflows. No payment received *yet* is not a permanent proof
that no payment exists. Choose the actual product semantics:

| Contract | Useful result | Accepted cost or limit |
| --- | --- | --- |
| Result at a captured complete input cut | Exact join and absence at that cut | Sources/cursors must define complete relevant coverage; freshness can lag |
| Provisional changing result | Early output with later positive/negative changes | Every downstream consumer must understand corrections; an irreversible alert or payment cannot be retracted merely by sending `−1` |
| Final bounded event-time window | Close only under the application's watermark and late-data policy | Retain window state; a late event must follow the declared reject, correction or reopening semantics |
| Finite rebuild and replacement | Compute a fresh version, then publish its defined atomic replacement | Extra scan/work and retained old version; useful when deltas are large or maintenance state is expensive |

A source watermark can be an estimate rather than a hard promise. Its meaning
must be supplied by the connector/application; this study does not infer a hard
bound from event timestamps or idle source partitions. Absence can close for a
narrow key/range once its relevant inputs are complete; it need not wait for
unrelated partitions. That improvement requires a sound coverage mapping.

The executable join preserves bag multiplicity and signed changes. For old bags
`L,R` and a finite simultaneous change `dL,dR`, it computes:

`dJoin = dL ⋈ R + L ⋈ dR + dL ⋈ dR`

Omitting the last term loses a pair inserted on both sides in the same cut;
applying that term twice double-counts it. Processing one side first is another
valid formulation if the second side uses the updated first relation. Operator
contracts must state set/bag semantics and valid inverse/update rules; a network
relay cannot decide these from the fact that records have numeric weights.

The retained six-cut history inserts both sides, adds an unmatched order,
accepts a later payment with the same old event time, deletes a last matching
payment, adds a second equal-valued order, and deletes both sides together. The
later correction retracts the old unmatched result; deleting the last payment
creates an unmatched result. Equal-valued orders remain two legitimate bag
members. Duplicate transmission of the *same logical contribution* is instead
suppressed by its stable lane/sequence identity. Physical attempt number and
tuple value are not substitutes for that identity.

Source seals may arrive before their packets. The two-lane example has sequence
2 and its final seal but lacks sequence 1, so it stays incomplete until repair.
Every declared lane must close, including an empty lane. An unknown lane or a
packet beyond a sealed cut is rejected in this fixture. New corrections enter a
new input cut; adding members while work runs would require an application-owned
coverage transition, not treating the old recipient list as complete.

Closed input, complete output and authority remain separate. The probe's four
publication cases allow the prepared result only when both required checks and
acceptance authority are supplied. For checked execution, matching partial
results or a completed dataflow frontier do not replace the agreed set of full
interaction/outcome checks. Speculative deltas can flow privately without making
their intermediate anti-join results authoritative.

## Recursion needs deletion semantics and a recomputation alternative

Incremental and iterative work are independent dimensions. Differential dataflow
uses partially ordered versions and retained differences to reuse work across
both dimensions, including nested loops. This is a candidate for demanding
applications, with retained indexed history and operator semantics to price;
the present probe does not implement it.
([Differential dataflow, CIDR 2013](https://www.microsoft.com/en-us/research/wp-content/uploads/2013/01/differentialdataflow.pdf))

For a smaller initial comparison, the prototype maintains single-source **set**
reachability with insertions and deletions. It overdeletes potentially invalid
reachable nodes, then rederives nodes still supported by the updated graph.
This follows the DRed idea of removing a superset and recovering surviving
derivations, specialized here to one positive recursive rule and one root.
The original work also explicitly observes that recomputation can be cheaper
than incremental maintenance.
([Maintaining views incrementally](https://sigmodrecord.org/?download_id=9412&smd_process_download=1))

The negative control is `0→1→2→1`. Deleting `0→1` leaves `1` supported by `2`
and `2` supported by `1` under naive local support counts, so both incorrectly
remain reachable. The least fixed point is `{0}`. The overdelete/rederive
candidate obtains that result. Counting incoming supports without tracking the
rooted derivation semantics does not implement recursive deletion.

Logical traversal results from [retained evidence](evidence/progress.json):

| Change | Incremental edge visits | Full recomputation edge visits | Overdeleted / rederived nodes |
| --- | ---: | ---: | ---: |
| Delete the last edge of a 200-edge chain | 1 | 199 | 1 / 0 |
| Add one edge to the end of that chain | 1 | 201 | 0 / 0 |
| Remove the only bridge into a 200-node cyclic component | 401 | 0 | 200 / 0 |
| Delete one redundant entry path to a long shared suffix | 593 | 200 | 198 / 198 |
| Add a bridge into a disconnected two-node cycle | 3 | 4 | 0 / 0 |

These are edge-entry visits, not elapsed performance. Both alternatives start
with indexed graph state; the actual Python prototype rebuilds indexes and
copies sets, which these counters deliberately exclude. A retained incremental
implementation needs incoming as well as outgoing adjacency in this comparison:
twice the edge entries of the forward-only full traversal, before metadata,
reachability sets or history. Index updates are reported separately. The zero
full-traversal row still has setup/index-update costs and reads the root.

The local-delta benefit is real under those accounting rules, but not universal.
An adaptive choice should compare the expected affected region and retained
indexes with a fresh traversal; it should not assume that one changed edge means
small work. Switching algorithms is safe only if they compute the same declared
result against the same cut. No heuristic threshold or runtime policy is selected
by these seven small comparisons.

Overdelete is private intermediate state. In the redundant-path example it
temporarily removes nodes that belong in the final result. Crash or cancellation
there cannot expose that temporary set as an accepted answer. Restart can
recompute from retained inputs or resume a checkpoint that preserves the exact
phase, worklist, graph version and output accounting. The probe demonstrates
discard-and-recompute; it does not implement phase checkpoint recovery.

Finite input does not imply finite recursion. On the same cycle, bag recursion
that counts walks (`UNION ALL`) still produces one new walk after eight rounds;
the bounded probe labels it incomplete. Set reachability stops with three nodes.
Likewise, shortest paths with negative cycles, recursively generated new values,
general recursive negation, or numerical iteration require their own termination
or approximation contract. `p = not p` has no Boolean fixed point. A round or
resource limit may cause a declared failure, continuation or explicitly bounded
approximate result; it cannot silently mean exact convergence. Floating-point
reductions and convergence tests also need deterministic observable behavior
under the existing execution contract.

Do not place an indefinitely running stream or iterative application in one
transaction that must verify all future work before publishing. A persistent
incremental view can accept finite state/offset transitions. An iterative
transaction can keep private tentative state and later resume at its assigned
position. An application that wants intermediate approximations can publish
separate finite outcomes with that meaning. These choices have different
visibility and dependency costs even if they use the same transport graph.

## Recover input, state and outputs as one coherent cut

The join fixture's accepted transition contains source-cut identity, both base
relations, derived state, the outcome identity and stable output-record IDs.
Source/code identity is pinned. Four interruption points—before preparation,
after preparation, after acceptance, and after external delivery before its
receipt—each recover one accepted internal transition. The last case obtains
one external effect only under the explicitly supplied sink's atomic
deduplication contract.

Two executable split-checkpoint controls show why offsets alone are insufficient:

- Emit the result, crash before its input cut is recorded, then replay: the sink
  observes the contribution twice.
- Record the input cut, crash before state/output is recorded, then trust that
  cut after restart: the sink observes zero contributions.

The positive fixture uses one atomic in-memory accepted transition as an
assumption boundary. It is not a crash-consistent disk algorithm or a distributed
checkpoint protocol. A production composition must bind the same logical cut
across state, inputs, in-flight work, interpretation artifacts and externally
owed output. Existing durable objects and transaction decisions are candidates
for selected materialization boundaries; a new universal checkpoint framework
is not required by this study.

| Recovery choice | Advantage | Cost to evaluate |
| --- | --- | --- |
| Recompute private work from retained inputs | Little per-operator recovery machinery | Longer replay, repeated UDF work, longer source/code/snapshot retention |
| Materialize selected durable stage results | Bound replay and share expensive results | Writes, retained bytes, result identity and a valid publication cut |
| Capture operator state plus channel/worklist state | Resume fine-grained long-running work | More complex consistent-cut, upgrade and recovery rules; unimplemented here |
| Split the product into finite separately accepted outcomes | Bound an individual outcome's work/lifetime | Application accepts those visibility boundaries; not equivalent to one atomic answer |

Output delivery must retain operation/result identity across replay and
repartitioning. A replayed fragment cannot be added to a reduction twice. A
downstream consumer that cannot undo already consumed deltas either needs a
final accepted version or a stronger sink contract. A committed external intent
means that an action remains owed; it is not an external receipt. The two local
histories “sink acted, reply lost” and “send never arrived” have the same recorded
intent and no receipt. Blind retry produces respectively two and one effects in
the negative control. See the [delivery study's external-action analysis](../orbital-dissemination/DELIVERY.md#retry-cannot-complete-every-external-action-safely).

## Completion does not settle every lifetime

An input frontier can retire a pending derivation while its result remains
useful to future inputs. The join probe accepts an order at input cut 1 and a
matching payment at cut 2. Retaining the join arrangement produces one joined
row; discarding it merely because cut 1 completed loses that row. Safe eviction
needs a declared bound on future use, a replacement representation or a charged
reconstruction path. Windowed joins, unbounded joins and historical snapshot
readers have different retention rules.

The cancellation example allocates 5,504 modeled bytes across input, result,
committed external intent and an independent old snapshot. It tracks their
separate owners:

| Event | Still retained |
| --- | ---: |
| All owners active; a one-byte new offer is refused | 5,504 bytes |
| Client cancels result interest; worker still uses its buffers | 5,504 bytes |
| Worker reaches its actual stopping point | 5,248 bytes |
| Accepted checkpoint covers the input's replay obligation | 4,224 bytes |
| Independent old-snapshot reader finishes | 128 bytes |
| Committed external intent receives its qualifying receipt | 0 bytes |

Cancellation is neither an instantaneous buffer free nor retroactive revocation
of a committed external intent. A local timeout also cannot erase announced
transaction effects or another reader's history. The governing transaction or
subscription may explicitly resolve an unfinished obligation; the worker must
still stop using its physical resources before those allocations are reused.
Resource reservation, execution stopping, accepted outcome resolution and history
reclamation therefore need separately checkable events even if implementations
coalesce them in common cases.

Plan for bounded working state, replay history, output backlog, version pins and
progress metadata. Refusing new work before it creates unretainable obligations
is preferable to claiming success after eviction. A slow sink or reader can keep
old data pinned after input progress advances. A checkpoint can replace a replay
path only if the checkpoint and all interpretation dependencies are durably
available; it does not automatically cover old readers, required audits or sink
deduplication history. Removing dedup history needs a surviving rejection floor,
fenced identity or another valid retry-lifetime rule.

## Executable scope and remaining uncertainty

Run from the repository root with the pinned Linux environment:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/check_progress.py
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/progress_probe.py \
  --output workbench/spikes/orbital-dataflow/evidence/progress.json
```

The **29 focused checks** include 256 exhaustive small signed-join transitions,
1,000 weighted random join transitions, 500 successive full-relation comparisons,
24 packet/seal permutations, 384 exhaustive small graph changes, 2,000 batched
graph changes with self-loops, and 1,134 frontier point comparisons. Join oracles
expand base bags and enumerate pairs; recursive oracles repeatedly scan the full
edge relation to a least fixed point. They do not reuse the incremental formulas
or worklist implementation. The retained JSON includes all authored positive and
negative histories, exact changes/counters, Python version and both executable
source hashes; routine successful random histories are reproducible from seeds
and are not individually retained.

The probe's `epoch` names a flow input version, **not** a witness shard epoch or
transaction position. It supplies source seals, a closed lane map, required-check
completion, durable atomic acceptance and sink deduplication where selected. It
has no network/disk timing, distributed progress or consensus, arbitrary dynamic
graph protocol, actual external service, general SQL/Datalog engine, secure
extension execution, or integrated operator memory/spill allocator. Its small
retention ledger is a separate finite-lifetime counterexample; the relation and
outbox maps are bounded by each authored workload, not by an implemented
production admission controller. No SLO or performance forecast follows from
these logical counts.

The supported small composition is therefore finite application outcomes over
explicit input cuts; declared progress/coverage for feedback and fanout; signed
incremental state only where its algebra and economics are justified; a full
recomputation path; private partial results; selected durable materialization;
and independently governed cancellation, delivery and retention lifetimes.
Distributed checkpoint construction, broad differential execution, online plan
replacement and calibrated latency remain design-in-detail work.
