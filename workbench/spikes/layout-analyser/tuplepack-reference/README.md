# A small executable layout analyser reference

This subprobe enumerates **8 two-byte and 2,808 three-byte layouts** for labelled
codes A:1, B:7, C:3, D:5. It exports structural features, accepts operation costs,
and compares layout/recipe choices under stated workload and reuse assumptions.
It is a bounded experimental analyser, not a production architecture or a CPU
cost model. Physical byte order, interior gaps and unused bytes remain distinct.

The [layout-analyser investigation](../README.md) owns the continuing design.

The standard-library Python implementation requires Python 3.10+. Run commands
from the repository root on Linux; prefix them with `orb -m ubuntu` from macOS.
Outputs below belong in ignored `build/`; no dataset download or CMake target is
needed. The [reading note](../tuplepack-search.md) owns the motivating
questions and literature.

## Export layouts and choose a measured subset

```sh
python3 workbench/spikes/layout-analyser/tuplepack-reference/analyse.py enumerate \
  --output build/layout-analyser/tuplepack-reference/all.json
python3 workbench/spikes/layout-analyser/tuplepack-reference/analyse.py select \
  --output build/layout-analyser/tuplepack-reference/subset.json
python3 workbench/spikes/layout-analyser/tuplepack-reference/analyse.py enumerate --format tsv \
  --subset build/layout-analyser/tuplepack-reference/subset.json \
  --output build/layout-analyser/tuplepack-reference/candidates.tsv
```

Omit `--subset` to export the full universe in either format. TSV has a header:

```text
id  unit_bytes  A_offset  A_shift  B_offset  B_shift  C_offset  C_shift  D_offset  D_shift
```

Separators are tabs. IDs encode byte offset and low bit in A,B,C,D order, e.g.
`b2-A0s0-B0s1-C1s0-D1s3`. Offsets and shifts are zero-based; widths are fixed.
JSON additionally exports code widths, operation maps and per-operation features.
Selected masks, selected byte spans, preservation masks, ordered offsets and
interior-code counts describe **logical requirements**. They are not actual
instruction counts, native access spans, cache misses or measured footprint.
For example, a word-load reader can access more bytes than selected-byte spans.
The maximum number of selected codes sharing a byte is a structural quantity;
it is not a lower bound on machine instructions or writer rounds.

`select` retains all eight two-byte layouts and, by default, 20 three-byte
layouts. `--three-byte-count 16` through `24` controls the small spread. The exact
method is farthest-first selection among the three-byte candidates, seeded by
their smallest ID, with smallest-ID ties. Its vector contains occupied-byte
count; each code's offset, shift and interior status; and each operation's
selected-byte count, byte envelope, preservation-byte count (zero for reads),
backward steps and offset travel. Distance is L1 after scaling each nonconstant
dimension by its range over all three-byte candidates. Integer common-denominator
scaling makes selection deterministic. These equally weighted dimensions are an
authored sampling choice, not an inferred performance metric. No measured cost
enters selection, and this spread does not bound regret over omitted layouts.

## Operation and measurement contract

The provisional `byte8-preserve-v1` contract uses an eight-byte ordered carrier:

| ID | Direction | Ordered code IDs |
| --- | --- | --- |
| R_AC | read | A, C |
| R_0 | read | A, C, B, D |
| R_1 | read | B, A, D, C |
| W_A | write | A |
| W_AC | write | A, C |
| W_AB | write | A, B |
| W_all | write | A, C, B, D |

Exported maps use numeric IDs A=0, B=1, C=2, D=3 and append 255 holes to length
eight. Reads return zero in holes. Writers take admitted-width replacement
values in map order, ignore hole inputs, and preserve all unselected bits,
including padding. Checked writers reject invalid selected values before any
mutation. Any effects/result and common benchmark sink must be the same across
recipes in a cost context. The analyser does not execute these kernels.

Cost input is JSON, as illustrated by [costs.synthetic.json](examples/costs.synthetic.json):

```json
{
  "schema_version": 1,
  "kind": "measured",
  "context": {
    "id": "unique-capture-and-operation-contract",
    "contract_id": "byte8-preserve-v1",
    "provenance": "path/to/capture-receipt-and-repetitions",
    "statistic": "median CPU ns per complete operation after item normalization"
  },
  "measurements": [
    {
      "candidate_id": "b2-A0s0-B0s1-C1s0-D1s3",
      "operation_id": "R_AC",
      "recipe_id": "selected-byte-access",
      "run_ns": 2.1,
      "prepare_ns": 30
    }
  ]
}
```

The numbers in this schema illustration are invented. All included executable
examples declare `kind: synthetic`; no CPU timing or migration was performed for
this subprobe. The tool preserves that label in its output. Setting `measured`
is an upstream evidence assertion, not a fact the analyser can independently
verify.

Use one context per machine/compiler/ISA, access trace, placement/stride, common
consumer, validation/effect contract and cost summary. Extra context fields are
retained verbatim. `run_ns` is complete-operation cost with preparation outside
timing; `prepare_ns` constructs one independent operation binding. Shared
schema/code preparation is not inferred or charged once automatically. Actual
issued spans and recipe/text bytes can remain in the source capture. Summarize
repetitions upstream; repeated `(candidate, operation, recipe)` rows are errors.
Unsupported recipes are absent alternatives. Every active operation must have
at least one cost for every declared candidate. Missing costs, nonfinite or
negative numbers, unknown identities and contract mismatches are rejected.

## Rank workloads, preparation and structural heuristics

```sh
python3 workbench/spikes/layout-analyser/tuplepack-reference/analyse.py rank \
  --costs workbench/spikes/layout-analyser/tuplepack-reference/examples/costs.synthetic.json \
  --workload workbench/spikes/layout-analyser/tuplepack-reference/examples/balanced.json \
  --subset workbench/spikes/layout-analyser/tuplepack-reference/examples/subset.json \
  --invocations 1000 --output build/layout-analyser/tuplepack-reference/example-rank.json
```

For the retained TuplePack measurements, [export costs offline](../../tuple-layout/viability.md#reproduce-and-inspect)
and use the captured [28-ID subset](../../tuple-layout/runtime/point-subset.json)
with the exported cost JSON. Without `--subset`, ranking requires all 2,816 candidates.
Reports retain the exact candidate IDs and their hash. `--top` controls how many
ranked plans to emit; the default is ten. Selection and regret still consider
every declared candidate.

Workload JSON has `schema_version: 1`, an `id`, and a `weights` object mapping
operation IDs to nonnegative numbers. Positive weights are normalized; missing
or zero-weight operations are inactive and incur no preparation. The
[read-heavy](examples/read-heavy.json), [balanced](examples/balanced.json) and
[write-heavy](examples/write-heavy.json) examples use 5%, 50% and 90% writes.
They are synthetic workload assumptions, not estimates of database traffic.

For total invocation horizon N and normalized operation weights w, the cold
selection minimizes, separately for each layout L:

```text
sum over active operations o:
    min over supplied recipes r (prepare_ns[L,o,r] + N*w[o]*run_ns[L,o,r])
```

There is **one fresh binding per active operation**, retained throughout the
horizon. N*w[o] is an expected invocation count and may be fractional. The
report gives each selected recipe, preparation charge, expected execution cost
and amortized preparation per invocation. A different horizon can change both
recipe and layout. Decimal inputs are represented as rational numbers during
ranking and break-even calculation; JSON results use ordinary numbers.

Four intentionally simple selectors use structural keys: density first minimizes
unit bytes; edge first minimizes weighted interior-code count; outer order first
minimizes weighted backward steps then offset travel; co-access first minimizes
weighted selected-byte count. The latter three use unit size as the final
structural tie-break. All use smallest candidate ID after structural ties, with
no cost peeking. Their chosen layout gets the same measured recipe selection as
the exhaustive reference. Regret is total cost minus the best declared cost;
relative regret is null when the optimum is zero. Reports also show structural
tie counts and the regret range across those ties. Exact ties in supplied
summaries do not establish statistical indistinguishability.

## Migration versus keeping a resident plan

Add `--migration workbench/spikes/layout-analyser/tuplepack-reference/examples/migration.synthetic.json`
to the ranking command. The
[example](examples/migration.synthetic.json) declares the current candidate,
exact resident recipe for each active operation, and total migration cost for
every other declared candidate. It must match the cost table's context and
evidence kind. `scope` describes the entire object/segment being migrated;
`provenance` points to its measurement. Migration cost includes the stated
copying/conversion/ownership work, but excludes new operation preparation,
which the analyser charges separately. No per-row migration cost is extrapolated.

Existing preparation is sunk. Staying costs N times the resident weighted
execution rate. Each target costs its migration charge plus its cold optimum
above. The report gives savings at N and the first **strictly better integer
horizon**, allowing target recipe choice to change with horizon. Equal total
cost retains the resident plan. No finite win is reported when the best target
execution rate cannot improve on the resident rate. This compares destination
layout changes; improving the resident binding in place is a separate choice,
not automatically included. It assumes stable post-migration costs and does not
model interference during migration or predict future reuse.

## Checks and what remains open

```sh
python3 workbench/spikes/layout-analyser/tuplepack-reference/check.py
```

All 12 checks passed on the Linux workspace on 2026-09-12. The checks
independently count byte assignments using labelled subset partitions,
permutations and stars-and-bars gap placement. Together with unique, legal output
layouts, that verifies completeness of the backtracking enumerator. Independent
bit sets check all 19,712 candidate/operation feature combinations; adversarial
old-byte patterns check preservation. A Cartesian recipe oracle checks ranking
over the full 2,816-candidate universe with synthetic costs. Hand-calculated and
direct-search cases check preparation-driven changes, migration with a changing
recipe, ties, impossible break-even and rejected input. TSV round trips and the
deterministic 28-layout selector are checked separately.

This demonstrates that a small initial analyser can enumerate, ingest evidence,
select recipes and expose regret without a production search framework. It does
not establish an accurate cost predictor. The captured
[V2 mixed-plan probe](../../tuple-layout/viability.md), with
[retained evidence](../../tuple-layout/evidence/mixed-v2/provenance.json), now supplies a concrete
counterexample to using additive isolated costs as the final chooser. Both
prediction and observation compared the same **56 plans: 28 layouts × two
uniform recipe families**, using actual trace weights. At 50% writes, the plan
selected from isolated costs ran at 5.102 ns/invocation versus the measured best
3.087; at 90% writes, 6.131 versus 3.718. Both are about 65% regret within this
subset. The 5%-write mixture had only 0.3% regret. This comparison does not test
the analyser's arbitrary per-operation hybrid recipe selection; the failure
already occurs with a single recipe family used throughout each plan.

**Use isolated costs to propose candidates; choose finalists using representative
mixed-plan probes.** Retain candidate diversity when doing so. The result
falsifies confidence in fixed isolated coefficients as a final ranking rule,
not the enumeration or cost-table arithmetic. Conditional byte traversal and
shared indirect-target histories are possible explanations, without established
PMU attribution. Preparation/migration break-even remains conditional on costs
that hold in the intended execution context. A measured subset optimum is still
bounded to that subset and its supplied recipes; it validates no pruning of the
remaining universe. No confidence intervals or statistical winner selection are
inferred from one summary per cost.

Candidate diversity matters: HYRISE's primary-partition pruning relies on additive
costs and order invariance in its own model. Ordered code maps, shared loads and
preservation here can violate those assumptions. Equal structural features or
co-access sets therefore do not justify eliminating a layout. The selector
samples; it does not prune with a correctness claim. Retain the exhaustive
reference for small cases and report subset regret as subset regret. See the
[literature and its limits](../tuplepack-search.md#useful-prior-mechanisms-with-their-limits)
for the owning discussion.
