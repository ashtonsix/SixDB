# Compact per-row filter signatures

**When can compact row signatures answer enough of a Boolean filter to avoid
expensive data access, and how should their precision, planes, and block
rollups adapt to the work that remains?**

This spike concluded on 2026-09-08 with a resident scan study and small
semantic models. Start with the [findings and closeout](FINDINGS.md),
[probe and methodology](probe.md), and
[reproducible evidence](evidence/local-arm-20260908/README.md). Joint block
summaries and expensive text prefilters have useful operating regions;
cheap resident columns usually favor direct evaluation in this study.
No production representation, plane width, rollup size, or planner contract
is selected. The code and evidence preserve the investigation; the remaining
design questions are possible follow-ups, not unfinished work in this spike.

The starting point is conjunctive filtering, including conjunctions embedded
in larger AND/OR expressions. Exact predicate bits, hashed fingerprints,
ordered prefixes, and set signatures are candidates with different contracts.
Neither a universal signature format nor a mandatory signature-first path is
assumed.

SixDB starts clean. Calico's [compact row filters, three-array layout, and
progressive bytepack work](literature.md#calico-starting-points) supply mechanisms,
controls, and counterexamples. Their row layouts, codecs, mask ABI, and planner
boundaries are all open for reconsideration.

## Queries to target

| Query family | Why signatures might pay | Counterexample or competing path |
| --- | --- | --- |
| One expensive predicate: substring, regexp, parsed attribute, costly deterministic expression | Reject before fetching, decoding, parsing, or evaluating a value | A common term leaves a large true-match floor; an exact cached predicate bit or dictionary-level evaluation may be better |
| Several cheap predicates across fields | Combine row-aligned evidence before fetching several columns or scattered rows | Near matches that fail only one atom; cheap resident exact columns; correlated predicates that add little rejection |
| `(A AND B) OR C`, larger CNF fragments, shared subexpressions | A plane may evaluate a useful whole fragment and pass unresolved obligations onward | A broad OR branch can defeat rejection; distributing the whole query into CNF/DNF can explode preparation cost |
| Equality/IN, range intersections, prefix predicates | Hash tags, ordered prefixes, bins, and exact flags offer different refinement paths | Large IN lists saturate tag domains; high-prefix ties and broad ranges make refinement unproductive |
| Sparse candidates from an index or earlier operator | A cheap signature probe may avoid random payload accesses | Loading a cold signature for a known row can cost more than testing its already-needed field |
| Repeated filters on sets or optional attributes | Shared token masks handle variable feature counts without a fixed slot for every attribute | Long sets/text saturate tiny masks; an inverted index or exact bitmap can win |

A single cheap predicate is a required direct-data control. It may still merit
a signature over cold, wide, remote, or compressed data, but its CPU cost alone
is a weak reason to add metadata. “Common query term” also has two meanings:
frequently queried can justify maintenance, while frequently present limits
rejection. Measure these frequencies independently.

## Initial hypotheses

- **Allocate information by avoided downstream work.** A false candidate that
  fetches a long string is more expensive than one checked in a resident byte.
  Distinguish false-positive rate among nonmatches from total survivor rate.
- **Keep widths and placement separate.** Compare one 32-bit plane, two 16-bit
  planes, and four 8-bit planes carrying the same information before varying
  precision. Four byte planes still store four bytes per row. Their potential
  saving is skipping streams/groups and increasing rows per SIMD operation.
- **Progression includes acceptance.** Ordered prefixes can settle inequalities
  before reading all bits; exact facts can settle OR branches. Hashed equality
  survivors generally remain unresolved. Passing a prefilter is not a proof.
- **Rollup quality depends on retained correlation.** OR is a sound rollup for
  positive shared-mask containment, but can quickly saturate and lose the
  same-row witness. Small-domain pattern sets and positional masks deserve
  explicit comparisons at 16, 64, and 256 rows.
- **Mutation changes the choice.** Frozen quantile boundaries can become weak
  without becoming incorrect. Reinterpreting old codes under new boundaries
  can become incorrect. Refresh, visibility, and query reuse must be priced.

## Investigation map

- [Findings](FINDINGS.md): measured operating regions, failed hypotheses,
  construction/replacement costs, and the next questions worth testing.
- [Executable probe](probe.md): implementations, generators, controls,
  timing boundaries, and run selection.
- [Design](design.md): semantics, representations, 8/16/32-bit choices,
  progressive execution, and query-wide planning.
- [Rollups](rollups.md): valid merge rules, correlation loss, small-domain
  alternatives, and block-size trade-offs.
- [Experiments](experiments.md): a small first study, strong baselines,
  counterexamples, correctness, and sustainable maintenance.
- [Reading and prior art](literature.md): inspected Calico work and primary
  references, with their limits.

The first study combines a semantic model, equal-information plane scans,
rollup comparisons, ordered ranges, real/synthetic text, and focused maintenance
measurements. It establishes reasons to preserve direct-data bypass and to
test summaries against expensive downstream work. The experiment menu and
design notes retain broader questions; the local ARM results do not select
a production representation, hierarchy, or planner contract.

Related SixDB work: [regexp lowering](../regexp-lowering/CONCLUSIONS.md) supplies
necessary predicates and exact alternatives; [aggregate maintenance](../aggregate-maintenance/CONCLUSIONS.md)
raises update/publication questions; [trie remapping](../trie-remapping/README.md)
changes physical row grouping; [secondary summaries](../../notebook/secondary-summaries.md)
distinguishes pruning evidence from estimates and approximate answers.
