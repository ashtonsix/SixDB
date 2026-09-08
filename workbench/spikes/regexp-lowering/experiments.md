# Raw-string efficacy study

This completed study covers regexp lowering and matching/rejection on raw strings.
Ashton is reworking FSST: compressed matching, decode performance, and timing
comparisons are deferred in [the FSST follow-up](fsst-follow-up.md). Counts
here establish available selectivity and coverage, not a winning executor.
The [closeout](CONCLUSIONS.md) records the resulting provisional recommendations.

The executable [lowering](lower.cpp) and [probe](probe.cpp) examine four
questions on [paired public workloads](workloads.md):

1. Which patterns lower exactly to a bounded OR of LIKEs?
2. For other patterns, how many values do LIKE signatures reject, and how
   much does the richer IR improve rejection over LIKE?
3. How much of each LIKE survivor lies outside conservative match bounds?
4. After observing low rejection in the first few containers, what filtering
   work is avoided, and how many later rejection opportunities are lost by
   switching to direct RE2? Can periodic probes recover after distribution drift?

## Implementation and semantic limits

RE2 at `a44f1bebafdc36437643ba168b839611cb15d37d` (2022-06-01) supplies both
the AST and the oracle. CMake fetches a SHA-256-checked source archive. This
standalone revision keeps the experiment dependency small; it is not a SixDB
dependency choice. Any patterns it rejects are reported, not rewritten into
another dialect. Input preparation uses Python 3.11+ and PyYAML.

The corpus profile is RE2 UTF-8 Boolean search, case-sensitive except for each
ua-parser rule's explicit `i` flag. Inputs are valid UTF-8 encoded from Python
strings. Captures, replacements, and classifier outputs are not returned.
`_` consumes one code point, `%` consumes any sequence including newline, and
literal metacharacters render with `ESCAPE '!'`. A single literal control is
chosen only when it is mandatory in every emitted branch.

The AST analysis keeps literal runs, any-character tokens, anchors at whole
branch boundaries, and at most eight alternatives. Classes of at most four
characters can expand exactly. Larger classes become `_` in LIKE. Finite
repetitions up to sixteen can expand within the alternative budget; positive
unbounded repetition retains a required child, then relaxes the tail. Repeated
single-character fragments retain their required leading characters.

Exceeding a branch/token/depth budget broadens that subtree, while surrounding
literals remain available. Folded literals are conservatively relaxed; this
version does not implement Unicode case-fold expansion. Non-absolute assertions
are dropped only for signatures; unsupported interior anchors likewise prevent
an exact result. The code records its decline/weakening reasons.

The [richer IR](richer-ir.md) also retains character ranges on `_`/repeated
character tokens and finite maximum byte widths on relaxed gaps. It is a
stronger necessary condition, not certified as an exact regexp replacement.
Its reference evaluator is untimed dynamic programming. Literal/%/`_` lowering
and the richer representation share the AST walk, so an additional independent
LIKE implementation checks the execution kernel.

Bounds use each alternative's longest retained literal and conservative maximum
byte widths on either side. Every occurrence contributes an interval, branches
are unioned, and overlapping intervals are coalesced. UTF-8 edges widen to code
point boundaries. More than 128 occurrences widens to the whole value. Assertions
and byte-in-UTF-8 operators decline bounded evaluation. Empty/no-literal branches
also force whole-value coverage. This is a modest implementation of the broader
[bounds proposal](survivor-bounds.md); choosing better witnesses and retaining
their relationships remain open.

## Workloads and pairing

Every published regexp is evaluated against every selected string in its own
dataset. This models independent Boolean scan predicates, with equal query
weight. ua-parser normally processes ordered lists until a rule matches, and
then extracts captures: this study measures neither that control flow nor a
complete user-agent classifier. Broad scan pairing intentionally provides
negatives, but is not a production query-frequency distribution.

The accident sample retains 64 evenly spaced contiguous blocks of 1,024 rows.
Within-block order and repeated descriptions are preserved. The ua-parser
sample combines its three committed regression YAML files in source order,
retaining duplicates. Query counts with and without any true hits are reported
so rejection on irrelevant predicates remains visible.

## Switching replay

Containers here are raw batches of 64, 256, or 1,024 values, independent of any
future storage layout. We evaluate all methods against RE2 once and record one
byte of outcomes per pair: bit 0 RE2, bit 1 LIKE, bit 2 mandatory literal, bit 3
richer IR. The controller then replays those observations for non-exact patterns.
Only observations for containers actually filtered are available to decisions;
counterfactual missed rejections are accounted separately.

| Policy | Rule |
| --- | --- |
| `direct` | Always use RE2; no signature evaluation |
| `always` | Evaluate useful signatures and verify survivors |
| `prefix05/20/50` | Filter the first four containers; permanently bypass if their aggregate rejection rate is below 5%, 20%, or 50% |
| `periodic20` | Same 20% initial decision; later bypass after two consecutive filtered containers below 20%; while bypassed, probe every 16th container and resume filtering at 40% rejection |

Each policy runs with LIKE and richer-IR signatures. Source order is primary;
a seeded row shuffle tests dependence on ordering. Statically known pass-all
filters are bypassed in every policy, including `always`; their avoidance must
not be attributed to learning. A separate `survivors-first`
stress order deliberately places signature survivors ahead of rejects. It is
constructed using the complete signature outcome and is not a natural workload
or input to the controller. It exposes what a misleading prefix can do. The
LIKE and richer-IR stress orders are independently constructed, so compare
policies within an arm there, not a causal cross-arm ordering effect.

Switching is at container boundaries and cannot change a result: bypass means
execute RE2, not accept an unchecked candidate. The exact route is separate and
needs no residual RE2 for Boolean results. The chosen thresholds are sensitivity
probes, not a cost model or learned production policy. A low rejection rate can
still be valuable on expensive, long values; a future policy should price bytes,
filter state, decode, residual cost, and remaining scan length. Switching from
rich IR to cheap LIKE, per-table binding costs, re-probe budgets, hysteresis,
and shared decisions between parallel scan workers also remain open.

## Verification and recorded evidence

[check.cpp](check.cpp) compares the fast LIKE matcher with an independent DP
oracle, then checks `regexp => LIKE`, `regexp => rich => LIKE`, exact equality,
and bounded verification against RE2. It combines directed cases with generated
AST-shaped patterns, exhaustive short subjects, byte/UTF-8 profiles, and two
expansion budgets. Explicit positive witnesses avoid vacuous negative tests.
Invalid patterns remain invalid. Source corpora additionally check every pair
and every bounded survivor. These tests support the rule arguments; they do
not prove every accepted regex/input combination.

No SQL engine is wired up, so NULL propagation, SQL expression error timing,
and capture semantics remain integration requirements rather than tested
features. There is no FSST encode/decode in the executable. Bounded byte counts
are the union of raw candidate intervals, not observed compressed bytes saved;
they exclude codec seeking and boundary-symbol overfetch.

The [runner](run.py) uses Workbench source snapshots, command logs, source/input
hashes, and success/failure receipts. Source must stay unchanged while it runs.
Raw per-query/per-container counters and policy traces live in the full bundle.
The [analyzer](analyze.py) keeps every query's compact counts and aggregates every
policy variant; it regenerates tables from those compact inputs. A retained run
includes its input manifest, source identity, and correctness log.

```sh
python3 workbench/spikes/regexp-lowering/prepare.py build/datasets/regexp-lowering/prepared
python3 workbench/spikes/regexp-lowering/run.py --sanitize
python3 workbench/spikes/regexp-lowering/run.py --branch-budget 64
```

From macOS, prefix each command with `orb -m ubuntu`. Preparation fetches a
1.15 GB source CSV on first use, caches it under ignored `build/datasets/`, and
verifies the publisher's LFS hash. Later runs reuse the prepared input. Runner
step durations are operational logs and must not be compared as benchmarks.
The second run tests expansion-budget sensitivity against the same rows and
patterns. It is an efficacy comparison, not a selected compile-time tradeoff.
