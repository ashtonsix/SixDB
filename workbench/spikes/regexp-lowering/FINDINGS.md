# Raw-string lowering findings

The subsequent [factored-filter study](FACTORED_FINDINGS.md) compares simple
AND/OR filters with ordered composition and selected richer constraints.

The first efficacy study is complete, 2026-09-08. Exact lowering and necessary
signatures remove substantial potential RE2 work on both paired workloads.
Keeping alternatives is consequential: increasing the branch budget changes
ua-parser call avoidance by eleven percentage points. Richer constraints,
survivor bounds, and switching expose useful opportunities, but their benefits
vary strongly by query and ordering. No FSST implementation or timing comparison
has been run, and no production policy is selected.

The [scope](experiments.md), [dataset provenance](workloads.md), and
[retained evidence](evidence/raw-20260908/README.md) describe the experiment.
The two runs have identical source/input hashes, differing only in branch
budget and whether sanitizer checks were requested.

## Workloads and correctness

The accident run applies four published regexps to 65,536 descriptions sampled
as 64 contiguous blocks spread across the publisher's CSV. The ua-parser run
applies all 1,270 published rules to 18,213 committed regression strings. There
are 23,392,654 regexp/string pairs per run; every pair is checked against RE2.
All 1,274 patterns compile under the pinned oracle. Every observed exact result,
signature pipeline result, and bounded verification agrees with direct RE2.

The separate correctness driver checks 3,178,704 pairs with 927,400 positives,
including independent LIKE implementations, generated patterns, directed cases,
UTF-8/byte profiles, and small expansion budgets. The eight-branch run also
passes AddressSanitizer and UndefinedBehaviorSanitizer with no diagnostics.
The same code runs the 64-branch corpus comparison. These checks are evidence,
not a proof of arbitrary-input equivalence.

All results concern Boolean search. Oracle RE2 runs on every pair for validation;
reported avoided calls are the residual calls a candidate pipeline would need,
not calls omitted by the instrumented validation process. Capture extraction,
SQL NULL/error integration, compressed execution, and speed are outside this result.

## Exact coverage and signatures

| Workload / branch budget | Patterns | Exact LIKE | Signature | LIKE pass-all fallback | Potential RE2 calls avoided by LIKE | With richer IR |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Accident / 8 or 64 | 4 | 0 | 4 | 0 | 96.654% | 96.654% |
| ua-parser / 8 | 1,270 | 199 | 906 | 165 | 77.284% | 78.029% |
| ua-parser / 64 | 1,270 | 231 | 963 | 76 | 88.504% | 88.907% |

Avoidance includes all calls for exact predicates and rejected values for
non-exact predicates. Each rule has equal scan weight within its workload;
these percentages are not production query-frequency estimates.

The accident signatures admit exactly the 8,772 observed RE2 matches, across
262,144 pairs. They remain signatures because relaxing newline-excluding dot
to LIKE wildcards is not universally exact. The mandatory-literal control
admits 18,338 candidates: retaining ordered structure removes more than half
of that control's residual work.

For ua-parser at budget 64, the mandatory-literal control admits 8,602,417
non-exact candidates, ordered/alternative LIKE admits 2,659,053, and richer IR
admits 2,565,826. The whole corpus has 134,155 true regexp/string matches.
Many filters are highly selective while a few weak ones dominate remaining
work: median per-query rejection among non-exact patterns is 99.973%.
The 76 pass-all fallbacks alone contribute 1,384,188 residual calls.

Of the 1,270 rules, 1,178 have a true match in the fixtures. Restricting the
aggregate to those rules changes LIKE call avoidance from 88.504% to 87.851%.
The result is not solely rejection of unexercised rules. Still, a regression
collection overrepresents unusual cases and supplies neither request nor query
frequencies. All-rule scan pairing differs from ua-parser's ordered first-match
classifier and capture processing.

The budget result argues for preserving branch structure, not selecting 64
universally. Enumerating more alternatives entails more state and filter work.
A factored DAG may retain conditions more economically; neither its construction
nor compressed scan cost has been measured. The class-expansion cap remains four
characters, and Unicode case-folded literals are conservatively weakened.

## More general IR helps particular rules

The richer IR preserves character ranges, restricted single-character runs,
and conservative maximum widths on relaxed gaps. At budget 64 it removes
93,227 more candidates than LIKE, or 3.506% of LIKE's residual calls. That is
0.403 percentage points across all pairs, smaller than the branch-budget effect.

Individual gains can be large. For `device_parsers:455`, a rule containing
uppercase-letter and digit sequences, budget-64 LIKE admits 1,787 candidates;
the richer IR admits 70, exactly the observed true matches. With budget 8 the
same rule admits 12,459 LIKE candidates versus 79 richer candidates. For HTC
rule `device_parsers:171`, budget-64 candidates fall from 13,734 to 2,053, with
64 true matches. Full expressions and counts are in the retained
[examples](evidence/raw-20260908/budget64/examples.csv).

These cases justify keeping the [IR question](richer-ir.md) open beyond LIKE.
They do not establish a compressed strategy, and sample equality does not
certify the richer IR as exact. Its DP matcher is an untimed reference.

## Survivor bounds are query-dependent

Budget-64 ua-parser intervals reduce potential byte coverage from 300,372,312
to 271,176,516 bytes across LIKE survivors: 9.720%. There are 435,171 survivor
pairs with a strict reduction, spread over 545 rules. Verification of every
interval union agrees with whole-value RE2. Multiple regions increase bounded
verification to 2,681,340 calls, versus 2,659,053 LIKE survivors.

Some rules benefit much more. For Linux rule `os_parsers:195`, coverage drops
from 1,309,214 to 91,773 bytes while retaining all 9,152 matches. For Safari
rule `user_agent_parsers:386`, it drops from 1,287,827 to 156,027 bytes.
Conversely, the accident workload saves only 37 of 433,027 survivor bytes:
unbounded gaps and short templated values leave little to remove here.

The eight-branch ua-parser run removes 18.530% of survivor bytes, but from a
much larger survivor population. That does not favor weaker filtering: budget
64 reduces both surviving values and aggregate candidate bytes. Bounds hold
each run's LIKE survivor set fixed and have not yet been composed with richer
IR rejection.

These are decoded-coordinate unions over raw strings. No claim is made about
FSST bytes skipped, endpoint seeking, boundary-symbol overfetch, or CPU savings.
Choosing better witnesses, tightening correlated endpoints, and handling
assertions without full-value fallback remain open.

## Switching trades filter work for later opportunities

For budget-64 ua-parser in source order with 256-row containers, a four-container
warmup and 20% cutoff switches 35 non-exact queries. Relative to always using
useful LIKE filters, it avoids 601,615 filter evaluations and adds 16,893 RE2
calls. Known pass-all filters are already bypassed in every policy, so this
avoidance is additional to the static control.

| Budget-64 LIKE replay, 256-row containers | Filter evaluations | Residual RE2 calls | Rejections missed while bypassed |
| --- | ---: | ---: | ---: |
| Always, any order | 17,539,119 | 2,659,053 | 0 |
| Source, prefix20 | 16,937,504 | 2,675,946 | 16,893 |
| Source, periodic20 | 16,381,229 | 2,892,900 | 233,847 |
| Shuffled, prefix20 | 16,903,126 | 2,680,327 | 21,274 |
| Shuffled, periodic20 | 16,926,749 | 2,682,284 | 23,231 |
| Survivors-first stress, prefix20 | 15,837,408 | 3,212,494 | 553,441 |
| Survivors-first stress, periodic20 | 16,300,104 | 2,812,531 | 153,478 |

Periodic probing recovers opportunities under the deliberately misleading
prefix, but this two-low-container/16-container-reprobe policy is worse in source
order. Container size and thresholds also affect the result; all 216
dataset/arm/order/size/policy aggregates are retained per run. The accident
source-order replay never switches at these cutoffs.

This supports observing scan-local efficacy while exposing why the first few
containers should not dictate an irreversible choice. It does not select a
threshold or show a latency win. A future cost policy needs the new codec's
per-table preparation, bytes and lengths, residual cost, remaining scan work,
and observation/re-probe costs.

This closes the requested raw efficacy slice. Preserving useful branch structure
without multiplying scans is the strongest next representation question. Richer
constraints and bounds deserve query-specific choices. The [FSST work](fsst-follow-up.md)
remains separate for the codec rewrite, along with performance benchmarking and
cost-aware adaptation. Paired production traces with query frequencies remain
a more informative dataset target than treating these sources as universal.
