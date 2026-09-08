# Regexp-lowering closeout

This spike is concluded, 2026-09-08. It investigated exact lowering, necessary
signatures, richer constraints, survivor bounds, adaptation, and factored
filters with explicit relative ordering. The raw-string studies and their
correctness checks are complete. Code, reproducible runners, and verified
evidence remain here for reuse or challenge. The recommendations below are
provisional design direction for SixDB; no production interface, codec
implementation, or execution threshold is selected.

## Provisional recommendations

### 1. Keep exact lowering distinct from signatures

Validate the original regexp and options, then analyze its parsed structure
under an explicit semantic profile. Prefer a small exact path for literals,
prefixes/suffixes, and compatible wildcard chains, with bounded alternatives
where affordable. Exact means equivalence for both matches and misses.
Record the guarantee in the plan so a necessary-condition signature cannot
be mistaken for a confirmed match. A signature miss rejects the value; a
survivor still requires the original regexp unless another exact check proves
the result. Unsupported lowering falls back to RE2.

The 64-branch study lowers 231 of 1,270 ua-parser rules exactly. That supports
an exact path, but does not select a branch budget of 64 or justify promoting
a signature that happened to agree on the sample. Preserve regexp validation,
NULL/error behavior, flags, encoding, and search boundaries through every route.
The production SQL and capture contracts remain to be specified; the spike
only validates Boolean search. See the [first findings](FINDINGS.md).

### 2. Preserve sequence and alternatives in a factored IR

Use literals/simple LIKE fragments as useful leaves, with explicit sequence
and OR structure, rather than making expanded OR-of-LIKE strings the canonical
representation. Retain order, required occurrence counts, branch relationships,
and adjacency/distance information in the analysis. Execution projections may
relax selected constraints, but should say which information they dropped.
Keep expansion/state budgets and weaken safely on exhaustion; never lose a
valid OR alternative.

An unordered AND/OR projection is a useful cheap gate. It does not represent
relative order or prevent overlapping occurrences from satisfying separate
requirements. The ordered comparison removes 128,934 additional ua-parser
candidates across 169 rules. Sequence children must not be sorted or deduplicated
as if they were Boolean conjunctions. Position-dependent cached results need
the incoming position in their identity.

The prototype's earliest-completion shortcut is sound with arbitrary gaps
between clauses. Its positional arm still relaxes adjacency and gap limits;
restoring those constraints may require multiple endpoints or correlated state.
Preserving that information in an eventual IR is a recommendation, not a claim
that the current prototype already executes it. See the
[factored findings](FACTORED_FINDINGS.md).

### 3. Choose useful leaves and stages per query

Keep character ranges and bounded runs available alongside simple filters.
They and ordering solve different problems. One measured model rule drops
from 9,304 unordered candidates to 191 with ordering; another still has 10,734
ordered candidates but only 70 after the original richer filter.

A single LIKE is sufficient for many simple cases. Where factoring helps, an
optional inexpensive gate can precede richer or positional evaluation; avoid
requiring every query to traverse every stage. The factored LIKE gate uses
71.7% fewer Boolean LIKE evaluations than the flat 64-branch baseline on
ua-parser. Putting that gate before the original LIKE also costs substantially
fewer evaluations than the reverse order. On the accident workload, added
stages reject nothing further and only add work.

Do not select the executor with the smallest survivor count by default.
Combining ordered and richer conditions reaches 94.172% potential call
avoidance on ua-parser, but pays for Boolean, positional, and richer execution.
The experiment prices those in separate operation counts, not CPU time. Sharing
compressed scans or transitions remains an implementation question. Selecting
richer leaves inside one factored graph also remains to be implemented; the
study established complementarity with cascades of separate projections.

### 4. Make survivor bounds an optional, separately justified result

Retain the ability to derive candidate regions, especially for long values
with short constrained matches. Require a complete verification opportunity
and preservation of original-subject context, in addition to the Boolean
signature guarantee. Keep relevant witnesses or conservative unions; widen or
fall back when occurrence/state budgets are exhausted. Verify separate regions
without inventing adjacency by concatenating slices.

The raw 64-branch study removes 9.7% of ua-parser survivor byte coverage, with
much larger reductions for some rules, but saves only 37 bytes on the accident
workload. Charge region discovery, endpoint location, context and symbol
overfetch, and extra RE2 calls before enabling partial decode. Unsupported
assertions or context use full-value verification. The current bound extractor
and ordered DAG have separate contracts: an earliest endpoint in a relaxed
filter is not automatically a complete RE2 verification interval. Their
composition is untested. See [survivor bounds](survivor-bounds.md).

### 5. Allow scan-local bypass and recovery, but defer policy constants

Keep direct RE2 available when filtering is unproductive, and allow later
observations to recover from a misleading prefix. Bypass changes the method,
not the result. A future policy should price preparation per codec table,
remaining scan length, bytes and lengths, rejection, residual verification,
and observation costs. Rejection percentage alone is insufficient.

Do not adopt the replay's four-container warmup, rejection cutoffs, or probe
intervals as production defaults. Periodic probing recovers some drift cases
but loses on others. Eager training of Boolean child order saves only 0.263%
of aggregate evaluations after its observation cost and worsens more queries
than it improves. Keep commutative filter evaluation order separate from
relative order inside a string; positional sequences cannot be reordered.
The [switching](FINDINGS.md) and [training](FACTORED_FINDINGS.md) results identify
costs to measure rather than a winning controller.

### 6. Use the new codec to choose the compressed backend

Carry these semantic contracts and raw workloads into the FSST follow-up.
First make per-table binding, shared scan work, symbol-internal positions,
escapes, and the chosen string semantics concrete. Compare direct decode/RE2,
exact compressed matching, simple gates, selected richer constraints, and
optional bounds with complete preparation and execution costs.

The spike does not determine table granularity, dictionary layout, a JIT,
automaton representation, caching policy, or engine/planner interfaces. The
old Calico implementation and published compressed-LIKE matcher are useful
references, with their assumptions checked against the new codec. Neither
constrains what SixDB must carry forward. The RE2 internal-AST dependency and
reference DP evaluators here are spike machinery, not production selections.

## Evidence and useful reasons to return

The [initial study](evidence/raw-20260908/README.md) retains eight- and
64-branch comparisons; the [factored study](evidence/factored-20260908/README.md)
retains the ordering and composition comparison. Each run checks 23,392,654
regexp/string pairs. No detected false negatives, exact disagreements, or
bounded-verification disagreements remain in the relevant runs. Generated,
exhaustive, positional, and sanitizer checks are recorded with measured source.
All three full bundles were uploaded and downloaded with SHA-256 verification.

The paired accident descriptions/queries and ua-parser rules/fixtures are
workload-plausible public inputs, not representative production frequencies.
The former has only four queries; the latter is a regression collection and
the study evaluates rules independently. Candidate counts, raw byte coverage,
and operation counts establish opportunities, not speedups or universal rates.

Return when the new FSST implementation can support meaningful execution and
binding measurements, when a concrete ordered-IR proposal preserves useful
adjacency/distance constraints, or when paired production strings and queries
make frequency and scan order credible. SQL semantics, captures, dynamic
patterns, and context-preserving partial decode are additional integration
questions. The [deferred experiment ideas](fsst-follow-up.md) remain available;
no continuation is scheduled as part of this closeout.
