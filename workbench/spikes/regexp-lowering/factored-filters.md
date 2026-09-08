# Factored filters and relative ordering

Follow-up to the [first raw study](FINDINGS.md), requested 2026-09-08. Compare
several simple filters with a richer condition, while keeping relative order
explicit. This remains raw-string efficacy and operation accounting; FSST and
timing are deferred. No representation has been selected for SixDB.

The run is complete; [findings](FACTORED_FINDINGS.md) and
[verified evidence](evidence/factored-20260908/README.md) are available.

## Comparisons

All arms use the existing 64-branch exact route unchanged. For non-exact
patterns, compare:

- The original 64-branch LIKE signature and its richer range/width refinement.
- A Boolean DAG of literal containment filters.
- A Boolean DAG of short LIKE fragments, using literals, `_`, and `%`.
- Ordered composition of those simple fragments, carrying candidate positions
  through sequences and alternatives.
- Cascades with the original LIKE or richer signature, to recover complementary
  constraints. The unordered cascade is checked in both evaluation orders.

The compiler walks the same RE2 AST. Concatenation becomes AND for the Boolean
arms; alternation remains OR, and optional subexpressions become TRUE. A
required repetition retains a required occurrence. Identical atoms and Boolean
subexpressions share nodes and results within a value. The LIKE arm packs
adjacent linear subtrees into one atom, retaining local order and `_` counts.
It splits at alternatives instead of distributing concatenation across them.

The linear projection relaxes character classes to `_`, folded literals
to `%`, assertions to empty conditions, and uncertain tails to `%`. Unlike the
first compiler, it does not enumerate small classes or optional alternatives.
Finite required repetitions retain at most sixteen copies, then relax the tail.
Limits are 256 tokens per local fragment, 4,096 allocated DAG nodes, 20,000 AST
visits, and depth 128; exceeding a limit weakens a condition. Reachable size and
budget relaxations are reported. These are different conservative projections,
so neither is presumed to dominate the original lowering.

## Relative order is a separate requirement

`(foo OR bar) AND (baz OR qux)` preserves alternatives but accepts `qux foo`.
The ordered arm instead uses `SEQ(OR(foo,bar), OR(baz,qux))`. Sequence order and
duplicate child occurrences are retained during interning. Unlike AND, a
sequence is neither sorted nor deduplicated. A later nonempty witness starts
at or after the previous witness ends; overlapping characters cannot satisfy
two required occurrences. Alternatives remain whole alternatives, so fragments
from different branches cannot be mixed to invent a match.

Each clause permits an arbitrary gap before its witness. Consequently, its
earliest possible completion dominates every later completion: any suffix
feasible after a later endpoint is also feasible after the earlier endpoint.
An OR returns the minimum endpoint of its viable branches; a sequence threads
the endpoint through its children. This avoids enumerating all combinations or
keeping every possible start/end pair in this particular profile. It must find
the earliest **end**, not simply the first-starting alternative. Memoization
uses both node ID and incoming position, so a result cannot incorrectly reuse
an occurrence from an earlier part of the value.

The proof depends on arbitrary gaps between clauses. This arm preserves order
and non-overlap but weakens adjacency and inter-clause minimum/maximum distances.
Required complex repetitions retain up to sixteen ordered copies. It is
a necessary condition, not an exact replacement. Restoring adjacency or gap
limits may require additional endpoints or correlated state; the earliest-end
shortcut must not be reused without establishing its contract. Absolute/line
anchors and word boundaries remain with RE2.

The positional reference evaluator computes reachable byte endpoints for one
simple LIKE at a time, with `_` stepping over UTF-8 code points. It uses a DP
buffer proportional to the raw value length. The corpus arm first runs the
unordered LIKE DAG and computes positions only for its survivors. Both stages
are charged. This is not evidence for a cheap compressed implementation.

## Work accounting and early observations

The flat LIKE and unordered arms use the same raw LIKE kernel. Count actual
atom evaluations after short-circuiting and memoization, and subject bytes
offered to those calls. These bytes are not measured bytes read: an individual
matcher may finish early or revisit positions. Boolean node visits and cache
hits are recorded too. Exact-route work is common and excluded from these
residual-work counters. Known pass-all filters cost no atom evaluation.

Position searches are a separate operation class. Record their DP position
visits, peak endpoint-buffer bytes, and memo-entry counts, without equating them
to a simple Boolean LIKE call. Richer-IR evaluations are also reported
separately. No sum of these counts is a CPU cost model.

One unordered arm observes every reachable leaf on the first four 256-row
containers. It orders AND children by estimated rejection probability divided
by reachable leaf count, and OR children by acceptance probability divided by
that count. All eager warmup evaluations are charged. Later rows cannot affect
the decision; tail and total costs are reported separately. Only commutative
Boolean children can be reordered. Positional sequences are never reordered.
Source-order replay tests this mechanism, not a production learning policy.

## Correctness and reproducibility

The [checker](factor_check.cpp) compares short-circuit and eager DAG evaluation,
the independent DP LIKE oracle, positional DP versus independent prefix
enumeration, and earliest completion versus retaining every feasible endpoint
through complete sequences. It covers UTF-8 and byte profiles, generated shapes, exhaustive
short strings, tiny node budgets, and explicit positive witnesses. Directed
cases cover reversed groups, overlapping occurrences, repeated alternatives,
branch correlation, and a first-starting alternative whose endpoint is too late.
Every corpus result is checked against direct RE2, using the same strings and
patterns as the [first study](workloads.md). All methods still evaluate Boolean
scan queries with equal query weights, not ua-parser's first-match classifier.

Run on Linux, from the repository root:

```sh
python3 workbench/spikes/regexp-lowering/run.py --study factored --sanitize
```

From macOS, prefix with `orb -m ubuntu`. Prepared inputs from the first study
are reused and hash-checked. The runner retains source, executables, logs,
per-query counters, full plans, and raw outcomes. Outcome bits are RE2, baseline
LIKE, baseline richer IR, literal DAG, LIKE DAG, and ordered DAG respectively;
exact-route pairs only set the first two bits because residual arms are skipped.
`factor_analyze.py` regenerates aggregate tables from `factor_summary.csv`.
