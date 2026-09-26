# Exchanges, joins and skew

2026-09-26. Focused executable research for the broader dataflow spike. The
small useful composition is an application-selected exchange and operator
portfolio over bounded channels, with separate choices for partitioning, work
grain and physical placement. Neither a universal map/reduce boundary nor one
always-preferred join strategy is supported by these examples. Engine or an
extension author owns the algebra, row identity, cut and result promise; Loom
owns scheduling and resource assignment; Orbital transports opaque work/data
and supplies its selected lifetime and delivery facilities.

[exchange_probe.py](exchange_probe.py) implements exact bag joins, a count-only
alternative, algebra counterexamples and a finite queue model.
[Evidence](evidence/exchanges.json) retains all 26 selected join comparisons,
nine queue comparisons and semantic examples. The integers below are logical
counts and synthetic byte estimates. They are neither measured performance nor
a prediction of a production system's latency.

## What the focused probes change

**Choose the representation of the required result before optimizing its
movement.** The two-sided hot-key fixture generates 65,670 row pairs. A 2 × 2
partition grid fits the supplied build-memory limit where the other tested row
join plans fail. If the requested result is `COUNT(*)`, per-key multiplicities
compute exactly the same count without enumerating a pair: 1,304 modeled wire
bytes, compared with 45,696 input bytes plus 1,576,256 bytes to gather the grid's
row-pair output. Replacing the row output with a count is legal only for that
requested result; an extension requiring a callback for every pair must still
receive those pairs. This is a concrete message-enhancement/generalized
aggregation opportunity, with an application proof rather than a transport
heuristic deciding whether it is valid.

**Broadcast is a conditional alternative, not “send the smaller row count.”**
For a 1,024-row probe and 32-row build, broadcast moves 6,144 input bytes versus
67,584 for repartition. With a 512-row probe and 256 *wide* build rows,
broadcast moves 3,145,728 bytes versus 1,081,344 for repartition. Exact key
filtering then repartitioning moves 164,800 bytes, but performs extra work to
construct and apply the filter. In the small-build case the same filtering adds
bytes and work. These premises are visible in the retained fixtures; none is a
claim about typical distributions.

**A blocked destination needs both independent service and admission isolation.**
Per-destination queues avoid head-of-line blocking, but a common buffer can
still be monopolized by a blocked receiver. A fixed per-destination cap fixes
that example and loses on a smoother example by refusing work that would have
fit and eventually completed. This supports elastic reservations or bounded
borrowing as a candidate, not selecting the fixed cap from this toy trace.

## Exact join study

Each row has a stable *occurrence* identity, key, home worker and declared width.
Equal keys are not deduplicated: three left occurrences and five right
occurrences produce fifteen pairs. SQL equality's NULL behavior is explicit.
Every completed result is compared with an independent nested-loop Cartesian
product oracle. The planner is given all inputs and the heavy key; heavy-key
detection and delayed/incorrect observations are not implemented.

| Plan | Actual algorithm in the probe | Principal extra obligation or cost |
| --- | --- | --- |
| Repartition | Send each side to `key % workers`; build right, probe left | A hot key stays indivisible; both sides may move |
| Broadcast | Keep each left occurrence at its home; copy all right rows to every worker | Repeated build state and CPU; complete build required at every eligible worker |
| Exact semijoin + repartition | Gather left key sets, distribute the exact union, remove nonmatching right rows, then repartition | Summary construction, global dependency and filter work; no benefit when few rows are removed |
| Colocated | Run only when both sides already have compatible key placement | Compatibility includes complete coverage and equality interpretation, not merely matching worker names |
| One-sided hot-key salting | Assign each hot left occurrence to one worker; replicate every hot right occurrence to all workers | Entire hot build side remains resident at each worker |
| 2 × 2 hot-key grid | Left occurrences choose a grid row and replicate over columns; right choose a column and replicate over rows | Both sides replicate, but each worker sees half the hot build rows; each pair meets exactly once |

Input-wire bytes count remote copies only, with source locality charged per
copy. Each right row uses its declared width plus a synthetic 32-byte hash-table
entry. The exact-key summary uses 8 bytes per key; summary construction and
right-side membership tests count as work. Hash-table memory alone has a hard
admission limit. This is deliberately a **build-state viability** test, not a
whole-process memory claim: summary sets, source input, serialization, runtime
metadata and streamed result storage are outside that cap. The executor streams
logical pairs into a validation counter outside modeled memory. The owning
resource study covers retained output and spill; this probe does not establish
that an accepted build plan can safely materialize an unbounded result.

The viability check knows the eventual partition sizes and rejects before any
network work. A rejected plan therefore reports all input rows as not admitted,
zero actual wire/CPU/output, and separately retains its planned copies, wire
and required build memory. This is an oracle cost comparison, not a realistic
admission predictor or online recovery strategy. All comparisons include the
oracle's expected output count and bag hash, so a refusal is not confused with
an empty successful join.

| Fixture / selected plan | Input wire bytes | Peak build bytes | Largest join-worker work units | Outcome |
| --- | ---: | ---: | ---: | --- |
| Small build / repartition | 67,584 | 768 | 520 | 1,024 pairs |
| Small build / broadcast | 6,144 | 3,072 | 544 | Same pairs |
| Small build / semijoin | 68,544 | 768 | 520 | Same pairs; total work 3,136 versus repartition's 2,080 |
| Wide build / repartition | 1,081,344 | 264,192 | 320 | 512 pairs |
| Wide build / broadcast | 3,145,728 | 1,056,768 | 512 | Same pairs |
| Wide build / semijoin | 164,800 | 33,024 | 264 | Same pairs; total work 1,824 versus repartition's 1,280 |
| Selective, 64 KiB cap / repartition | 0 actual | 294,912 required | 0 actual | All 4,352 input rows not admitted |
| Selective, 64 KiB cap / semijoin | 20,960 | 1,152 | 132 | 256 pairs; 4,080 right rows rejected by exact key evidence |
| One-sided hot / repartition | 51,200 | 768 | 1,864 | 1,024 pairs |
| One-sided hot / salt | 65,728 | 864 | 523 | Same pairs; more input and gather traffic |
| Two-sided hot, 20 KiB cap / repartition | 0 actual | 25,920 required | 0 actual | Not admitted |
| Two-sided hot, 20 KiB cap / salt | 0 actual | 26,208 required | 0 actual | Not admitted |
| Two-sided hot, 20 KiB cap / grid | 45,696 | 13,920 | 16,711 | 65,670 pairs |

One work unit is one build row, probe row or emitted pair. The largest-worker
column covers the join stage only, excluding semijoin setup. This arithmetic
cannot price cache effects, differing hash costs, filter serialization,
parallel overlap or critical-path time. The output gather is separately charged
at 32 bytes per remote pair. For example, salting the one-sided hotspot changes
gather bytes from 3,072 to 24,672: balancing join CPU can move the bottleneck to
output. A downstream partitioned consumer could avoid this gather, but that is
a different composition and placement choice.

The count-only alternative locally counts keys, exchanges `(key, count)`
partials, multiplies left/right multiplicities per key, and reduces four scalar
counts. In the two-sided hotspot it uses 640 local count updates, 136 partial
merges and 32 key products. Its validity is checked against the same 65,670-pair
oracle. Python's arbitrary-precision integer arithmetic is the exact contract
here; a fixed-width implementation needs an overflow contract. No general
factorized representation or aggregate-rewrite system is implemented.

## Queue and dynamic-fanout study

The queue model supplies 80 parent offers. A parent produces either one child
or four children as a function of its input identity. It is admitted atomically
only if space exists for all children, preventing an accepted parent from
silently losing one discovered destination. A zero-child input completes with
explicit accounting. Offers outside the finite observation interval are not
counted as arrivals. This finite model does not infer a dynamic graph's global
completion or allow new publication authorities.

Every child occupies 64 payload bytes until delivery. Total capacity is 1,024
bytes. One destination can accept one message per tick; the shared NIC can
deliver two. Destination 0 cannot deliver before tick 60. A FIFO stalls on its
first ineligible destination. Independent queues round-robin over eligible
destinations. The capped alternative allows at most 256 queued bytes per
destination. Admission precedes service within each tick.

| Trace / policy | Complete parent offers by tick 96 | Healthy-only parents completed | Not admitted | Before receiver recovers |
| --- | ---: | ---: | ---: | ---: |
| Smooth / FIFO | 32 / 80 | 24 / 60 | 48 | 0 |
| Smooth / destination queues, shared pool | 80 / 80 | 60 / 60 | 0 | 45 |
| Smooth / destination queues, fixed cap | 68 / 80 | 60 / 60 | 12 | 45 |
| Early blocked burst / FIFO | 35 / 80 | 19 / 64 | 45 | 0 |
| Early blocked burst / destination queues, shared pool | 35 / 80 | 19 / 64 | 45 | 0 |
| Early blocked burst / destination queues, fixed cap | 68 / 80 | 64 / 64 | 12 | 44 |

The early burst fills the shared pool with sixteen blocked children before any
healthy input arrives. Separating queues cannot recover memory already owned
by those children. Under the capped policy only four are admitted, leaving room
for all healthy inputs. Under the smooth trace the shared pool suffices and the
hard cap refuses twelve inputs unnecessarily. A still-blocked trace at tick 80
retains accepted unfinished parents separately; the capped policy completes
60 of 80, refuses 16 and retains 4. A p99 over its successful parents would be
one tick, while all-offered p99 is unavailable. Both are retained to expose
survivorship bias.

Refusal is an admission result, not a dropped accepted message. The caller still
owes work if the application has previously promised completion. There is no
unbounded upstream queue hidden in the comparison: the probe stops at that
explicit refusal boundary and does not claim eventual completion for those
inputs. A complete application needs bounded retention, backpressure/spill,
retry authority or a user-visible failure outcome. The model assumes independent
offers can reach admission; a blocked upstream producer unable to inspect later
independent inputs can recreate head-of-line blocking earlier in the pipeline.

The model counts payload occupancy, delivered wire bytes and eligibility
checks, not real CPU time, queue-object memory, control packets or link headers.
There is no packet loss, congestion controller, persistence or cross-host RTT.
The result is a counterexample about finite service/admission structure. It does
not select an Orbital queue implementation or a cap of one quarter of memory.

## Legal transformations remain application facts

| Temptation | Checked counterexample or condition | Required distinction |
| --- | --- | --- |
| Salt both join sides independently | Four occurrences per side yield only four pairs instead of sixteen | Replicate one side, use a complete grid, or otherwise prove every pair has one owner |
| Apply a negative runtime filter by dropping probe rows | A left join drops its unmatched key and NULL row: four rows become two; anti-join's two rows become zero | Preserve an absence-output branch for these operators; filter only with a complete compatible build cut |
| Treat `NOT EXISTS` as `NOT IN` | A NULL on the build side yields zero true `NOT IN` outputs here, but two `NOT EXISTS` outputs | SQL three-valued semantics is part of Engine's operator contract |
| Keep each partition's top group | Partitions `{A:6,B:5}` and `{C:6,B:5}` each discard B, although B's global sum 10 is the winner | Local row top-k under one total order is mergeable; top-k of incomplete group aggregates is not |
| Use bounded local top-k for `WITH TIES` | Scores `[10,10]` and `[10]`: local top-1 yields two rows instead of all three | Carry all relevant ties or a correct continuation protocol; tie size need not be bounded by k |
| Regroup floating sums arbitrarily | `(1e16 + 1) - 1e16 = 0`, while `(1e16 - 1e16) + 1 = 1` | Choose exact/reproducible arithmetic, fixed ordering or explicitly relaxed results |
| Retry a reduction contribution as a new event | Sum 4 + 7 becomes 18 after replaying 7 | Transport identity/coverage and application idempotence or contribution multiplicity are separate |
| Repeated pairwise semijoins solve every join | Triangle relations in the retained example survive every pairwise reduction yet have zero final rows | Cyclic residue still requires actual multiway/binary join reasoning |

The checks also exercise 100 random bags across all five moving join algorithms,
plus exact count results, and 100 random row sets with four choices of k. Stable
tie breaking uses score descending and occurrence ascending. Result properties
are checked independently of scheduling and placement.

## Scope beyond these small implementations

The following are real alternatives to retain in Engine/application exploration,
not requirements for Orbital to understand their semantics.

| Alternative | Useful condition | Counterexample / reason to defer a default |
| --- | --- | --- |
| Range exchange and ordered merge | Global ordered output, range joins or existing sorted runs | Skewed equal keys and boundary overlap still require complete ownership; sampling can miss a hotspot |
| Lookup/index join, batched remote probes | Tiny selected outer side and useful resident index | Random packet/IO work, repeated lookups and a high-match key can dominate; batch and deduplicate lookup *work* without losing bag multiplicity |
| Approximate runtime filter | Cheap, compact negative evidence before a costly edge | False positives still need exact checks; delayed filters may save little; an old filter missing a new match is incorrect at the new cut |
| Pipelined / symmetric hash join | Both inputs stream and early matching output is useful | State lives until the opposite side's complete boundary; outer/anti absence cannot be finalized merely because nothing has arrived yet |
| Sort-merge, partitioned spill, reread/recompute | Build exceeds memory; sorted reuse or bounded spill is cheaper than replication | Extra I/O/passes, scratch capacity and release rules must be charged; a second blocking resource is not an unlimited escape hatch |
| Multijoin semijoin reduction and lazy/factorized joins | Rejection or a compact representation avoids large intermediates | Cyclic joins, duplicate-sensitive aggregates and consumers demanding expansion defeat a universal semijoin/factorization rule |
| Two-dimensional or more general replicated partitioning | Both sides have hot keys and pair generation is inherently large | Communication and execution-state replication can grow rapidly; the downstream result may still dominate |
| Partial aggregation / groupjoin | Required aggregate has a declared merge law and multiplicity can be carried | Arbitrary UDFs, per-event outputs, order-sensitive results and deletion witness repair require more state/work |
| Adaptive partition split/coalesce or plan replacement | Observed fanout/size differs materially from estimates; remaining work is large | Existing state, emitted pairs and old routes must retain exactly one contribution owner; changing the modulo divisor mid-stream duplicates or misses work |
| Shared exchanges/filter artifacts across queries | Compatible cuts, identities and expensive reusable work | A slow/cancelled subscriber pins state; an estimate reusable for cost need not be valid proof for a later snapshot |

For adaptation, the minimum useful prototype boundary is a declared set of
unprocessed work with stable identities, plus a complete treatment of active
and already-emitted contributions. Materializing an exchange can make this
boundary easier to inspect and replay, at the cost of I/O and latency. Continuing
an old routing generation while admitting a new one requires per-input ownership
or a proven handoff. A new partition count alone supplies neither. This probe
uses fixed assignments and complete source inputs, so it makes no claim that an
online transition has been implemented.

An HTAP query needs input coverage at its selected read cut, including legally
discovered additional sources. A filter, build table and probe input drawn from
incompatible cuts can lose a match even when transport is perfect. A private
analytics result, an updating transaction's validation and publication of an ELT
generation impose different boundaries. Iteration time, read cut, shard epoch
and delivery progress are separate concepts; none is derived from the queue's
last delivery. New input discovery within a valid read contract is distinct
from adding output publication authorities after an already-complete envelope.

For extension-hosted non-DB work, the same exchange shapes describe route
classification, recommendation candidate expansion, image/video fanout and
matching subscribers to events. The extension must declare whether duplicate
callbacks, reordering, early output or compressed/factorized results are legal.
An arbitrary callback is not automatically a mergeable reduction. Side effects
need their own publication/idempotence boundary rather than being hidden in a
“join completion” message. These are scope examples, not separately measured
workloads.

## Sources and relation to prior work

The source survey starts with the current
[notebook](../../notebook/ideas.md), dissemination
[workload survey](../orbital-dissemination/SOURCE-SURVEY-WORKLOADS.md),
[read catalog](../orbital-scenarios/reconsideration/READS.md) and
[aggregate semantics](../aggregate-maintenance/design.md). They motivate
complete cut-qualified coverage, exact versus necessary evidence, shared
artifacts, duplicate sensitivity and output/retention accounting.

The historical Calico
[join/execution ledger](../../../../calico/design/prior-art/engine2-survey/joins-execution.md)
and [planning ledger](../../../../calico/design/prior-art/engine2-survey/planning.md)
were read in full. They contain semijoin schedules, factorized/lazy expansion,
groupjoin, cyclic joins, adaptive evaluation and morsel scheduling. Their
“superseded,” “solved” and “adopt” verdicts are Calico's recorded claims, not
inherited SixDB facts. In particular, this spike's cyclic and output-growth
examples provide no basis for discarding general join planning.

Primary external references checked 2026-09-26:

- [Graefe, Volcano (1994)](https://people.eecs.berkeley.edu/~prabal/teaching/resources/eecs582/graefe94volcano.pdf):
  exchange operators separate parallel coordination from individual operators.
  This supports studying the separation, not importing Volcano's iterator ABI
  or putting relational meaning in Orbital.
- [Apache Spark's performance guide](https://spark.apache.org/docs/latest/sql-performance-tuning):
  concrete examples of broadcast, compatible storage partitioning, runtime plan
  changes, partition coalescing and skew splitting/replication. Its thresholds
  and runtime implementation are not used in this probe.
- [Afrati, Stasinopoulos, Ullman, Vassilakopoulos, SharesSkew](https://arxiv.org/abs/1512.03921):
  communication-aware replicated partitioning for heavy hitters and joins.
  Our exact 2 × 2 example is a minimal independently checked construction,
  not a reproduction of that paper's optimizer or performance results.

## Reproduction and limits

From the repository root, using the pinned Linux environment:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/exchange_probe.py --output workbench/spikes/orbital-dataflow/evidence/exchanges.json
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/check_exchange.py
```

Evidence records Python 3.13.7 and the generator SHA-256. All fifteen focused
checks pass, including exact result comparisons, malformed colocation rejection,
memory refusal, atomic fanout admission, empty/future offers, 300 randomized
queue-policy conservation cases and byte-for-byte logical evidence reproduction
(excluding interpreter-version provenance). No random timing or wall-clock
benchmark is reported. The code is standard-library Python and intentionally
does not implement a query planner, durable execution, a spill engine, dynamic
membership, real transport or a high-fidelity simulator.
