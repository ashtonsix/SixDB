# Bitset advisor

Can Ikea help Engine choose how to hold a bitset collection without Engine
modelling every codec, and without Ikea pretending to know the workload? This
study prototypes the Ikea side of that seam for collections of 4096-position
groups, then scores its advice against measured complete traces.

## Starting point

- The [whole-bitset study](../bec256-composition/whole-bitsets.md) concluded
  that Ikea should expose estimates and exact effects and leave directory
  choice, sampling and byte/CPU trade-offs to the owner. Its fixed-sampling
  counterexamples (every eighth group unlike the rest) are reused here, and so
  are its RealRoaring windows.
- The [layout analyser](../layout-analyser/findings.md) found that a cheap
  selector can choose badly from a good menu (worst regret 13.6% → 44.9% when
  the palette grew), and that probing complete plans repaired it. So advice is
  scored on complete traces, and a bounded probe is one of the policies.
- [Frame phase 3 §5](../frame-phase-3/FINDINGS.md#5-describing-costs-to-engine)
  found cost tables weighted by a fitting census miss absolute times by a
  median 34–83%, while ranking was right in an easy test. The question here is
  whether a similar table decides *consequential* choices.
- The [Bec4096 study](../bec4096/README.md) showed a fresh block read costs
  2–9× a retained one, so representation and preparation are separate choices
  over the same bytes.
- Codex's review of the first sketch (relayed in the project thread) moved the
  proposal from "return a frontier" to "a bounded decision with consequential
  alternatives and their conditions", asked that preparation be priced apart
  from representation, and proposed regret against the best *fixed* plan.

## What was built

[trace.cpp](trace.cpp) holds six plans, each a stored representation plus a
way of reading it:

| Plan | Stored | Retained |
| --- | --- | --- |
| `plain` | 512 B per group | — |
| `plain+counts` | 512 B per group | 16-bit population per block (32 B/group) |
| `positions` | sorted 16-bit positions, 32-bit offset per group | — |
| `bec256` | sixteen Bec256 bodies per group, direct metadata (68 B/group) | — |
| `bec4096` | one Bec4096 body per group, 8 B/group directory; binds per operation | — |
| `bec4096+retained` | as `bec4096` | one bound reader per group (96 B) |

Each plan answers three demands with consumed results: membership probes at
uniform positions, ranks within one or eight hot groups, and a whole-collection
count of the bitset ANDed with a random mask. Every plan's answers are checked
against independent plain computations before timing. It also times
construction, preparation and the advisor's cheap analysis (per-group
population, live blocks and Bec256 `estimate_bytes`, 20–125 ns per group).

Prices are measured on 22 held-out collections of 128 groups (6 synthetic
shapes, the 2 sampling counterexamples, 14 RealRoaring window families) and on
20 synthetic calibration classes (population × 2 or 16 live blocks, plus empty
and full).

[advise.py](advise.py) is the advisor and the evaluation:

- **Facts from data:** bytes per plan from population and Bec256 estimates.
- **Costs:** per-group operation costs looked up in the calibration table by
  each group's population and live blocks. No evaluation collection is in the
  table.
- **Demand from Engine:** operation counts, hot-group count, and a hard
  resident-memory budget.
- **Package:** a recommendation, the smallest legal plan, the runner-up, the
  reuse at which retained Bec4096 readers repay binding, and whether the
  runner-up is within 1.3× (unresolved).

Policies scored against the best fixed legal plan for each complete trace:

- **advisor:** exact counts.
- **advisor-coarse:** counts rounded to a power of ten, hot-group count unknown.
- **advisor-named:** Engine also names the hot groups.
- **advisor+probe:** when unresolved, and when a probe costs under 5% of the
  predicted trace, build the finalists on 8 groups and run 1,024 of each
  demanded operation. The probe is modeled from the measured prices, not run.
- **preset-smallest:** the smallest stored bytes.
- **preset-bec4096:** retained Bec4096, else Bec4096, else smallest.
- **reuse-rule:** plain words when there are more than 8 accesses per group,
  else smallest.

The trace grid (probes and ranks each 0, 10³, 10⁵ or 10⁷; whole passes 0, 1,
10 or 100; hot 1 or 8) is crossed with three budgets: none, 2× and 1.15× the
smallest actual resident size. Construction is charged once. The grid counts
cases equally; it is not a workload frequency model.

## Findings

Neoverse V2 (c8g.medium) and Zen 5 (c8a.medium), 6,660 cases each.
[V2](evidence/neoverse-v2/advice.txt), [Zen 5](evidence/zen5/advice.txt).

| Policy | Cases over 10% regret, V2 / Zen | Max regret, V2 / Zen |
| --- | ---: | ---: |
| advisor | 149 / 129 | 87% / 183% |
| advisor-coarse | 146 / 129 | 87% / 183% |
| advisor-named | 93 / 45 | 64% / 60% |
| advisor+probe | 78 / 88 | 86% / 183% |
| reuse-rule | 1,326 / 1,156 | 9,096% / 12,782% |
| preset-smallest | 3,067 / 3,140 | 14,787% / 18,539% |
| preset-bec4096 | 3,586 / 3,630 | 7,887% / 7,173% |

No policy exceeded a budget. Byte predictions were exact for plain and
positions and within −6% to +26% for Bec4096.

**A small advisor makes the consequential choice.** It lands within 10% of the
best fixed plan in 98% of cases. Any fixed preset is wrong by orders of
magnitude in about half of them. The plans differ by 10–100× on most demands,
so the cost table's large per-operation errors (−89% to +949%) rarely change
a decision. This agrees with Frame: tables mispredict times but rank
consequential gaps correctly.

**Coarse demand is enough.** Order-of-magnitude counts with no hot-group
information do as well as exact counts. Engine does not need a workload model
to get good advice.

**Which groups are hot matters more than how many operations.** The remaining
failures are heterogeneous collections whose hot groups are unlike the average
group. A dense group in `eighth-dense` or a busy weather window makes the mean
mislead. Naming the hot groups cuts the >10% cases by 38% on V2 and 65% on Zen
5, and the RealRoaring max on Zen from 158% to 17%. Engine knows hot-group
identity from telemetry; that is the one demand detail worth passing.

**Probing helps only when it is cheap.** The bounded probe roughly halves the
>10% cases where it can run. Without the 5%-of-trace gate it lost on small
traces (p90 regret 4.8% locally). It cannot repair a case the advisor wrongly
called resolved.

**Without a memory budget, Bec almost never wins on time.** Unbudgeted, the
best plan is plain words, counted plain words or positions in 2,199 of 2,220
traces on V2 (2,214 on Zen 5). Under the 1.15× budget, Bec4096 is best in 1,443
of them. Bec's value is bytes, so choosing Bec is a memory decision. The
package's useful content is how much time each step down in bytes costs. For
example, on `wikileaks-noquotes` the recommendation is `plain+counts`
(69,632 B), positions take 0.07× the bytes for 1.4× the time, and Bec4096 0.04×
the bytes for 13× ([examples](evidence/neoverse-v2/examples.txt)).

**Retention is a memory question too.** Binding a Bec4096 reader costs about
one fresh access, so retained readers repay their time after about one access
per group everywhere. What varies is memory: a 96-byte reader adds 0.1× to
random groups' bytes but 4.7× on `wikileaks-noquotes` and 11.5× on
`dimension_008`. Fresh Bec4096 costs 60–130 ns per probe, mostly binding.
On sparse natural data, positions beat retained Bec4096 for probes at
moderately more bytes.

## What this suggests for the seam

These are proposals from one problem type, not a contract:

- Ikea's per-module contribution is **exact or estimated bytes from cheap
  facts, plus a per-class cost table**. Byte predictions decided legality
  reliably; cost tables only had to rank large gaps.
- The comparison layer **owns cross-representation recipes**. Pricing positions
  against Bec needed both implementations and the same consumed results, as
  Codex argued. It is problem-specific, here "a collection of 4096-position
  bitsets".
- Engine's side can be small: a memory budget as a hard filter,
  order-of-magnitude operation counts, and hot-group identity when it has it.
- The package should lead with the byte/time steps among legal plans and the
  memory cost of preparation. Presets work as stated-assumption defaults when
  there is no demand summary, not as decisions.

## Limits

- Resident, single-threaded timings on one collection size (128 groups);
  cold reads, concurrency and larger collections are not measured.
- One fixed plan per collection; mixing representations by group, conversion
  between plans mid-trace and mutation are not priced.
- The trace grid is authored and uniformly weighted.
- Bec256 uses direct metadata; packed metadata would shrink it and slow it.
- Positions are plain 16-bit; SeriesPack's 12-bit packing would cut its bytes
  by 25%.
- The plain rank has no word-level tuning beyond a branch-free loop.
- Probes are modeled from measured prices.

## Reproduce

```sh
python3 workbench/tools/worker.py run workbench/spikes/bitset-advisor/run.sh \
  --machine neoverse-v2 --instance-type c8g.medium --arg neon
python3 workbench/spikes/bitset-advisor/advise.py prices.json [--detail | --examples]
```

Use `zen5`, `c8a.medium` and `avx512` for x86. Jobs 20261024T224608Z-127ce85f
(V2) and 20261024T224642Z-1ec65692 (Zen 5); each `evidence/*/job.json` records
the captured source.
