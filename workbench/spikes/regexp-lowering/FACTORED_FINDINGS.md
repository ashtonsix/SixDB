# Factoring, relative order, and constraint retention

The follow-up is complete, 2026-09-08. Multiple simple filters are promising,
but factoring and retaining constraints are complementary choices. On the
ua-parser fixtures, factoring preserves useful alternatives with substantially
fewer Boolean LIKE evaluations. Carrying relative order through those
alternatives removes another 128,934 candidates. Character ranges and tighter
constraints remain decisive for other rules. The accident workload gains
nothing from the additional composition stages.

The [method and contracts](factored-filters.md) explain the implementation;
[retained evidence](evidence/factored-20260908/README.md) supports these results.
No FSST implementation, timing comparison, or production executor is selected.

## Comparison held constant

This run uses the same prepared inputs as the [first study](FINDINGS.md):
four published accident queries over 65,536 descriptions, and 1,270 ua-parser
rules over 18,213 regression strings. All arms share the original 64-branch
exact route, including its 231 exact ua-parser rules. The remaining 1,039 rules
are the signature comparison. Baseline exactness, hits, LIKE survivors, and
richer-IR survivors agree with the earlier run for every query.

All 23,392,654 corpus pairs are checked against direct RE2. No false negatives,
exact mismatches, or reordering disagreements were detected. The factored
checker covers 1,828,560 Boolean and 914,280 ordered cases; a further 66,120
comparisons retain every feasible endpoint to check the earliest-completion
shortcut independently. Both it and the original 3,178,704-pair checker pass
ASan/UBSan without diagnostics. These checks support the semantic arguments;
they do not establish arbitrary-input equivalence.

These remain equally weighted independent Boolean scan queries. ua-parser's
regression frequencies, first-match control flow, and capture extraction are
not a production query distribution. Avoidance counts include all calls on
exact routes and rejected values on signature routes, not a measured speedup.

## Factoring keeps useful alternatives at lower simple-filter work

| ua-parser route | Potential residual RE2 calls | All calls avoided | Boolean LIKE evaluations |
| --- | ---: | ---: | ---: |
| Original LIKE, budget 64 | 2,659,053 | 88.504% | 117,142,458 |
| Original richer IR | 2,565,826 | 88.907% | 117,142,458, then richer evaluation |
| Literal AND/OR DAG | 1,654,385 | 92.848% | 35,426,403 |
| Local LIKE AND/OR DAG | 1,631,817 | 92.945% | 33,200,289 |
| Ordered DAG, with unordered gate | 1,502,883 | 93.503% | 33,200,289, then positional evaluation |
| Ordered DAG, then original richer IR | 1,347,980 | 94.172% | 37,239,899, plus positions and richer evaluation |

The unordered LIKE DAG uses 71.658% fewer simple-filter evaluations than the
flat baseline, using the same raw LIKE kernel. Subject bytes offered to these
calls fall from 12.195 GB to 3.706 GB. These are full subject lengths per call,
not measured bytes read; token count, early exit, revisits, and Boolean control
also affect cost.

Across non-exact rules, the flat plans contain 7,505 branches and 53,200 tokens;
the unordered DAGs contain 3,413 distinct LIKE leaves, 14,887 tokens, and 4,995
reachable nodes. Literal payload falls from 103,511 to 23,412 bytes. Known
pass-all filters fall from 76 to 23. No DAG budget limit is reached in the
corpus; the largest reachable DAG has 359 nodes.

The result is not a controlled swap of storage layout alone. The factored
compiler keeps alternatives without distributing their products, but weakens
some class, repetition, adjacency, and boundary constraints differently. Its
representation counts make that tradeoff visible; they are not memory-residency
or codec-binding measurements.

## Relative order changes real query outcomes

The unordered DAG only establishes that its required fragments exist. It can
admit a later fragment before an earlier fragment, or reuse overlapping
occurrences across conjunctions. The positional arm preserves sequence order,
branch correlation, non-overlap, and bounded required-copy counts. Its memo key
includes the incoming position; sequence children are never reordered or
deduplicated.

This removes 128,934 candidates, 7.901% of the unordered DAG's survivors,
across 169 rules. Most of the effect is in device rules: 126,244 candidates.
The other groups contribute 855 OS and 1,835 user-agent candidates.

For `device_parsers:335`, a model-alternative rule followed by a build/WebKit
suffix, the original LIKE admits 9,520 values, unordered factoring admits
9,304, and ordered composition admits 191. There are four true matches.
This is a substantial benefit from retaining the relationships among fragments.
The retained [examples](evidence/factored-20260908/examples.csv) include full
regexps and `AND`, `OR`, and `SEQ` plans for inspection.

Order does not recover every discarded constraint. For `device_parsers:372`,
the original LIKE admits 139 values and its richer refinement admits exactly
the 57 observed matches. Unordered factoring admits 9,151 and ordering reduces
that to 711. For the uppercase/digit rule `device_parsers:455`, ordered factoring
still admits 10,734 values while the original richer IR admits 70, exactly the
observed matches. Ordering cannot substitute for character constraints.

Ordered factoring has fewer candidates than original LIKE for 93 non-exact
rules, but more for 163. The rest tie. Their intersection retains useful
information from both: 1,407,416 candidates with original LIKE, or 1,347,980
with the richer IR. Equal observed matches never certify a signature as exact.

## Positional work must be priced separately

The ordered route pays for its 33,200,289 Boolean gate evaluations and then
4,220,879 position searches. The untimed positional DP visits 1,164,874,319
token/position combinations. The peak endpoint buffer is 986 bytes and peak
memo occupancy is 292 entries on these gated survivors; these are component
counters, not total allocator or executable memory use.

The reference retains only the earliest possible completion of each clause.
That is sufficient because inter-clause gaps are unrestricted: completing
earlier cannot remove a later continuation. It still permits adjacency and
distance violations. Restoring inter-clause distance limits or adjacency may
require more endpoints and correlation state. A compressed implementation must
also handle positions inside symbols. None of those costs is settled here.

The best rejection count is therefore not automatically the preferred route.
An unordered DAG followed by the original richer signature already gets to
1,424,309 candidates without positional evaluation. Adding positions removes
another 76,329, while introducing their separate work. This is a concrete
tradeoff for the future backend, rather than a reason to evaluate positions
unconditionally.

## Evaluation order also affects work

Filtering first with the unordered DAG and then with original LIKE requires
39,101,584 Boolean LIKE evaluations. Reversing the two requires 128,083,444.
Both admit the same 1,484,280 candidates. This is evaluation order between
necessary predicates, distinct from relative order inside the string.

Learning the unordered DAG's child order from the first four 256-row containers
has little aggregate benefit after paying for observations. Eager warmup adds
1,772,227 leaf evaluations; the tail saves 1,859,448, leaving a net saving of
87,221, or 0.263%. There are 96 improving queries and 362 worsening queries.
Device rules lose 566,146 evaluations overall while OS and user-agent rules
recover the aggregate. A source-prefix policy is not selected on this evidence.
Positional sequences are excluded from this reordering mechanism.

## Where this leaves the IR question

Keep alternatives factored and make sequence order explicit. Simple predicates
are useful leaves and cheap gates; selected richer leaves can retain constraints
that ordering cannot recover. A plain unordered AND/OR signature is useful as
a prefilter, but is an inadequate representation of all the useful information.

Do not add stages universally. On the accident workload, local LIKE already
admits exactly the 8,772 observed matches. Positions and subsequent filters
remove no more candidates; they only add work. Plain literal conjunction is
weaker, admitting 8,953.

The strongest follow-up question is how to bind a factored ordered graph, with
selectively richer leaves, to the forthcoming codec without multiplying scans
or carrying excessive positional state. Sharing symbol transitions, preserving
adjacency/distance where useful, and deciding when a cheap gate pays belong in
the [deferred FSST work](fsst-follow-up.md). This raw comparison is complete.
