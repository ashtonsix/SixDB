# Regexp lowering to LIKE

**How much regexp work can exact lowering, necessary signatures, and survivor
bounds eliminate, and when should a scan switch methods?**

This spike is concluded, 2026-09-08. The [closeout and provisional
recommendations](CONCLUSIONS.md) bring together the raw-string studies of exact
lowering, signatures, bounds, switching, and factored filters with explicit
relative ordering. Code, runners, and verified evidence remain here for reuse
or challenge. Compressed execution and performance benchmarks are
[deferred](fsst-follow-up.md) while Ashton reworks FSST. No production interface,
codec policy, or execution threshold is selected.

The [findings](FINDINGS.md) and [verified evidence](evidence/raw-20260908/README.md)
are available. On the 64-branch comparison, 231 of 1,270 ua-parser rules lower
exactly, and exact/signature LIKE routes avoid 88.5% of potential residual RE2
calls. The four-query accident workload avoids 96.7%. Richer IR, survivor bounds,
and switching add query-dependent opportunities; these counts are not speedups.

The [factored-filter follow-up](FACTORED_FINDINGS.md) is also complete. On the
same ua-parser inputs, factored LIKE gates use 71.7% fewer Boolean LIKE
evaluations; preserving relative order removes another 128,934 candidates.
Richer constraints remain complementary, and positional work needs separate pricing.

The opportunity is to move work onto the compressed representation. An exact
lowering can avoid both value decoding and RE2 matching. A signature can
avoid them for rejected values; survivors still need the original regexp.
Ashton's follow-up adds a third opportunity: derive conservative match-region
bounds so that only part of a survivor needs decoding and RE2 evaluation.
For now, the useful results are which calls and candidate bytes could be
avoided. Pricing preparation, filtering, and decoding is a later question;
rejection rate alone cannot establish a speedup.

| Route | Required guarantee on non-NULL subjects | Execution |
| --- | --- | --- |
| Exact LIKE expression `E` | `E(x) = R(x)` | LIKE decides the regexp predicate |
| LIKE signature `S` | `R(x) => S(x)` | Reject when `S` is false; otherwise decode and evaluate `R` |
| No useful lowering | No selective signature established | Decode and evaluate `R` directly |

An exact LIKE match therefore confirms a regexp match. Equivalence also makes
a LIKE miss conclusive. A sufficient condition alone (`E => R`) could certify
some positive matches, but cannot replace the regexp or reject its misses;
that is a possible later extension, separate from the signature route.

The provisional direction is a small proven exact path and a factored signature
IR that preserves sequence, alternatives, and useful constraint information.
Choose simple gates, richer constraints, positions, and bounds where they pay;
retain direct RE2 and allow recovery from unproductive scan prefixes. LIKE is
one useful execution target. The new codec and production workloads must supply
the costs needed to choose an executor and adaptive policy.

- [Design](design.md): semantic guarantees, candidate lowering rules, examples,
  compressed execution, and decisions left open for SixDB.
- [Survivor bounds](survivor-bounds.md): retain possible match regions from
  signature evaluation, then decode only the required intervals and context.
- [Richer IR](richer-ir.md): retain constraints beyond LIKE without selecting
  the future compressed representation.
- [Factored filters](factored-filters.md): compare simple AND/OR filters with
  ordered composition that carries witness endpoints through alternatives.
- [Workloads](workloads.md): accident descriptions and published queries;
  maintained user-agent rules and collected regression strings.
- [Reading and Calico references](literature.md): what the earlier code does,
  primary sources for compressed LIKE and regexp prefilters, and their limits.
- [Executable study](experiments.md): lowering rules, correctness, raw efficacy,
  switching policies, input preparation, and reproducible runs.
- [Deferred FSST study](fsst-follow-up.md): codec execution and cost questions
  preserved for the forthcoming implementation.

On Linux, run:

```sh
python3 workbench/spikes/regexp-lowering/prepare.py build/datasets/regexp-lowering/prepared
python3 workbench/spikes/regexp-lowering/run.py --sanitize
python3 workbench/spikes/regexp-lowering/run.py --branch-budget 64
python3 workbench/spikes/regexp-lowering/run.py --study factored --sanitize
```

From macOS, prefix each command with `orb -m ubuntu`. Preparation caches a
1.15 GB source CSV; subsequent runs reuse its checked sample. The commands
reproduce the two expansion budgets and the factored comparison.

The [aggregate-maintenance](../aggregate-maintenance/CONCLUSIONS.md) and
[trie-remapping](../trie-remapping/README.md) spikes inform the approach:
isolate a mechanism, include its construction costs, make negative cases
credible, and keep code and evidence beside the question. SixDB starts clean;
Calico's byte semantics, dictionary layout, codec policy, and planner seams
are references to examine, with no presumption of either adoption or rejection.
