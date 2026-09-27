# Native policy comparison

**Older-conflicts-drain is the provisional maintained default**, conditional on
verification of its binding into the actual transaction models. Ordered,
eligible-first and oldest-live remain explicit experimental comparisons.
The [formal study](README.md) owns their queue rules and progress assumptions.
This comparison supplies actual prepared-quorum commands, distributed positions,
reads, publication, physical resource contention and recovery through messages.

The policy is fixed for the whole run and its replay. This is not an in-place
upgrade of old ordered journal prefixes: different grants can change positions.
Consumers preserve original enqueue order through queued and held states and
settle each agreed command before folding the next. A single forward scan closes
all grants because granting cannot enable a request that was earlier blocked.
Live order participates in replica/recovery comparison. Native responses are
serialized by request identity; the formal queue projection emits grants in
scan order. These are deterministic policy-event correspondences, not identical
wire-sequence claims. Different policies may choose different serial executions;
correctness is checked separately rather than requiring equal application outputs
across policies.

## What the comparison found

[Selected results](native-selected.json) retain all 420 competitors and repeats:
360 main histories, 24 longer streams and 36 actual held-broad controls. There
are zero reported safety violations, simulator errors, event-budget stops or
missed required incidents. All 23,916 offers arrived; 21,141 completed and 2,775
remain unfinished in 60 deliberately short 10 ms prefixes. All longer drainage
controls complete. Unfinished offers remain in the denominator.

The grids include seeds 1/7/19, isolated/shared resources, bridge and equal-width
bridge-cut controls, narrow/broad contention, an overlapping waiter chain,
sparse WAN arrivals, actual broad WAN holders, and consumer destruction or
coordinator write-before-callback restart. All policies retain the same plans
and physical costs within each comparison.

| Selected seed-7 case | Ordered | Drain | Meaning |
| --- | ---: | ---: | --- |
| Original bridge, 48 locals: bridge-scope maximum latency | 70.90 ms | 2.17 ms | Removes waiter-only propagation of the WAN delay. |
| Three-waiter chain: tail maximum | 33.43 ms | 7.65 ms | Breaks the indirect chain. |
| Same chain: independent cohort maximum | 2.94 ms | 7.52 ms | Extra enabled work increases physical/frontend competition. |
| 192-offer/764 ms sparse WAN stream: bridge-scope maximum | 260.43 ms | 0.15 ms | Benefit persists across repeated finite overlap. |
| Same stream: direct-overlap maximum | 252.56 ms | 246.92 ms | Actual held conflicts remain. |
| Ordinary locals, 500 us spacing: maximum | 103.7 us | 103.7 us | No free gain without queue interference. |
| 48-offer hot-key burst: maximum | 6.16 ms | 6.16 ms | A serial hot scope stays serial. |

The younger-WAN case is the important counterexample. X holds x; B waits for
x/y/z; younger Y requests y and a remote scope. Drain lets Y enter at 50.025 ms.
X fixes at 80.190 ms, so B becomes a barrier, but Y stays held until 130.090 ms.
B then fixes at 130.138 ms. Ordered fixes B at 80.238 ms before admitting Y.

| z-only arrival wave | Ordered queue wait | Drain queue wait |
| --- | ---: | ---: |
| Around 55 ms, before X fixes | About 25 ms | 0–0.8 ms |
| Around 95 ms, after X fixes while Y remains held | 0–0.9 ms | 35.0–35.3 ms |

The second wave does not conflict with Y. It waits because B now protects its
whole group. Drain can worsen this later indirect delay; aggregate percentiles
would hide the reversal. The independent cohort remains around 1–2.5 ms.

Oldest-live has another weakness. With an unrelated old WAN holder and 128 narrow
offers, an independent broad request enters at 1.222 ms and is granted at
1.763 ms ordered, 7.185 ms drain and 22.871 ms eligible/head. The unrelated holder
remains live until 80.190 ms. Head disables useful local protection solely because
that unrelated holder is oldest.

An actual WAN holder covering three local scopes behaves identically under all
four policies. At 5/20/80 ms one-way regional delay, overlapping writer maxima
are 10.39/40.39/160.39 ms; disjoint maximum is 0.78 ms. Actual overlapping grants
wait for local fixation, while independent completion precedes it. Three symbolic
scopes establish the causal geometry, not the indexing/enumeration cost of
hundreds of thousands of scopes.

## Operational costs and limits

Consumer CPU still uses the same fixed cost per record. We separately count
conflict tests, visits to ticket records (including historical entries), earlier
waiters and live-order entries. For the unrelated-holder/128-narrow case, primary
conflict probes are 77,253 ordered, 80,684 eligible, 85,223 drain and 80,812 head.
Drain adds 25,008 live-barrier visits across three consumers; every policy scans
about 3.52 million historical ticket entries in the deliberately simple fold.
An implementation needs indexing and measured costs. These results establish
neither a throughput gain nor calibrated production percentile latency.

Queue maxima count candidates before closure; live/holder maxima are per
shard/consumer, then maximum across actors. Work counts include actual replay.
Backend operation refusals are distinct from terminal transaction failure.
Histories and versions are retained, so these finite runs do not establish
sustainable capacity or history reclamation.

The journal path carries full bodies and requires the fixed leader plus a
follower. It has no elections or authority transfer. Coordinator records remain
local durable storage: restart recovers them, permanent media loss does not.
Replicated coordinator recovery is a separate simulator-fidelity gap. Native
pre-position cancellation is absent; the formal queue projection tests it.
The native checked-mismatch abort tests cover post-fix abort instead.

## Reproduction and evidence

[The grid runner](native.py) uses the shared campaign/capture helpers. A current
run captures sources, builds the ordinary maintained regional client, checks it,
and preserves exact arguments, choices, results and provenance:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-reservation-policy/native.py \
  --suite all --output build/experiments/reservation-policy/recheck
```

Use `--suite main`, `long` or `held`, and `--seeds 7` for a bounded subset. An
existing captured source/binary pair can be supplied with `--source` and
`--binary`; source and binary hashes are checked before and after the run.
The runner is programmatic: import `cases()` to compose or vary the grid.
Historical simulator investigation grids explicitly select ordered/eligible
policies rather than inheriting the new default.

[Provenance](native-provenance.json) names the exact reviewed candidate sources
and binaries. The [verified S3 bundle](evidence/native-policy/README.md) retains
all 420 results, frozen sources, exact executables and build provenance; original
runs remain in ignored `build/orbital-design-study/native/`. The Git export adds
only a recovery guide, accounting and identity index beside the selected data.

The selected file retains every case, cohort, censor/failure status,
latency/work/resource counter and broad wait, with younger-WAN z waves separated.
The 384 regional runs retain exact scheduler choices and complete phases. The
separate 36 held-broad controls retain seeds, wrapper/binary identities, results,
local-fix and first-overlap-grant times, but did not record choices or full phases.

The first younger-WAN fixture only tested direct conflicts with Y and missed
the indirect z boundary. Its interrupted v1 capture is labeled superseded;
all selected results use corrected v2. Independent review accepted the policy
operator and the limits stated here. Maintained tests preserve historical ordered
expectations explicitly and check drain's regressions as well as its benefits.
