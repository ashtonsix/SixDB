# Deferred FSST execution and performance questions

These are deferred experiments after the [spike closeout](CONCLUSIONS.md).
Ashton is reworking FSST, so the completed studies stop at raw-string efficacy.
Revisit these possibilities when the codec is ready; this list does not schedule
a continuation or select production semantics.

## First executable slice

Use a spike-local compiled library shared by a correctness driver and a
benchmark driver, following the [existing spike build](../README.md).
Start with constant patterns, one thread, case-sensitive byte semantics,
independently FSST-compressed values, and literal/% compressed matching.
Keep the oracle, lowering, and matching implementation separable. The existing
targets and runner execute raw-string studies; add a distinct compressed driver
when this follow-up begins. The initial byte subset is an experiment profile,
not a decision to drop the UTF-8 requirements exercised by the current oracle.

Use the [factored findings](FACTORED_FINDINGS.md) to compare a single useful
LIKE, optional cheap gates, factored sequences/alternatives, and selected richer
constraints. Preserve relative order, non-overlap, and branch relationships;
explicitly test any retained adjacency or gap limits. The raw earliest-end
shortcut requires arbitrary inter-clause gaps and cannot be assumed to support
tighter relationships. Price shared code-stream work against independent scans.

Implement literals/absolute anchors and unrestricted wildcard chains as exact
candidates, then mandatory-literal and ordered-fragment signatures. Bounded
alternation is the first extension: it tests both useful exact coverage and
the most consequential signature correctness rule. Record every decline and
its reason, distinguishing semantic, expansion-budget, matcher-capability,
and cost-policy decisions.

## Correctness before timing

Compare against RE2 with the same recorded pattern bytes, flags, encoding,
and search/full-match mode. A separate simple decoded LIKE oracle should
consume literal/wildcard tokens without sharing matcher transitions.

- Exact: `E(x) == R(x)` for every subject, including misses.
- Signature: `R(x) => S(x)`; separately require the survivor/RE2 pipeline
  to equal direct RE2. False positives are expected and counted.
- Compressed executor: its result equals decoded LIKE for every tested
  pattern/table/value; require FSST round trips independently.
- Survivor bounds: every true subject retains a verifiable candidate region,
  and verification of all retained regions equals whole-value RE2, including
  context-sensitive cases that decline to full decode.
- SQL wrapper: NULL, NOT, and nested AND/OR preserve the reference result.
  Invalid patterns/options stay errors regardless of a filter's result.

Enumerate short subjects over a deliberately awkward alphabet: letters,
newline, `%`, `_`, escape bytes, NUL, and high bytes. Add generated valid
regexp ASTs with shrinking of counterexamples. Enumerating a bounded domain
is evidence, not a proof for arbitrary inputs: each accepted rule still needs
its language argument. Add explicit positive witnesses so a test cannot pass
mostly by comparing two false answers.

The adversarial corpus should cover:

| Risk | Required cases |
| --- | --- |
| Search/anchor mismatch | Empty values, leading/trailing bytes, absolute versus multiline anchors, final newline, full-match mode |
| Wildcard semantics | Dot with/without `s`, scoped flags, `_` matching one byte versus one code point, literal LIKE metacharacters |
| Optional/alternative content | `foo(bar)?baz`, `foo\|[0-9]+`, empty alternatives, nested alternation, zero/positive minimum repetition |
| Lost order/multiplicity | Reversed fragments, repeated fragments, overlapping occurrences such as `a%a` against `a`, long common prefixes |
| Boundaries and encoding | Word boundaries, case variants, valid multibyte text, the chosen malformed-UTF-8 policy, embedded NUL |
| Budget fallback | Large finite classes, repetition/alternation cross-products, state and time caps, weakening an OR without losing a branch |

Train multiple FSST tables and include hand-built valid fixtures that force
matches inside symbols, across symbols, across literal/% fragment boundaries,
and beside escape pairs. Exercise every intra-symbol offset up to the symbol
length, 1- and 8-byte symbols, byte 255, and adjacent values whose concatenation
would match although neither value does. Use encoder-produced inputs for a
matcher that relies on greedy/unique-prefix properties; arbitrary alternative
encodings are not valid test expectations for that restricted algorithm.

Run the same logical values under different table/encoding choices that meet
each matcher's assumptions. Reusing a matcher after changing a table must be
prevented or correctly rebound. UTF-8 and `_` coverage are separate extensions,
with explicit fallback in the initial compressed subset.

For [survivor bounds](survivor-bounds.md), generate known matches at every
intra-symbol position, preceded by many false witnesses. Check branch unions,
overlapping/disjoint windows, finite and unbounded width, whole-value envelopes,
and occurrence-budget widening. Include word characters/newlines just outside
the proposed slice, real versus artificial text boundaries, and UTF-8 split
points. A passing Boolean signature test does not establish sound region bounds.

## Comparands

| Arm | What it tells us |
| --- | --- |
| Resident decoded values + RE2 | RE2 cost without decode; charge the decoded footprint |
| FSST decode + RE2 for all values | Primary baseline for compressed input |
| FSST decode + exact LIKE | Effect of lowering without compressed execution |
| FSST + exact compressed LIKE | Combined decoding and RE2 avoidance |
| FSST + one mandatory literal + survivor decode/RE2 | Cheapest signature control |
| FSST + ordered LIKE signature + survivor decode/RE2 | Value of retaining order/gaps |
| FSST + bounded Boolean LIKE signature + survivor decode/RE2 | Value and cost of branch conditions |
| FSST + pass-all filter + decode/RE2 | Cost of candidate plumbing when nothing is rejected |
| Signature + full decode + bounded RE2 search | Effect of restricting search without saving decode |
| Same signature + bounded decode/RE2 | Additional savings from decoding only candidate regions and context |

Compare signatures on patterns with the same original regexp and subjects.
Use exact arms only when equivalence holds; compare alternate signatures even
where exact lowering is possible to understand their costs. Include cheap raw
literal/prefix/suffix kernels as applicable, and a compatible RE2-prefilter
adapter if practical. Do not force RE2 into an artificially expensive mode or
claim a weak LIKE implementation measures the limits of compressed matching.

Start with an interpreted matcher to expose construction/state costs, while
retaining the published specialized implementation as a serious comparand.
Only then decide whether JIT preparation is justified. A simple slow lifted
automaton losing a benchmark would not refute the optimized paper candidate.

## Workload dimensions

The first smoke selection can use a literal substring, a dot-all exact chain,
a digit-constrained signature, a two-branch signature, and a no-literal fallback.
Give each both true matches and near misses. A small default should finish
quickly; broader screens and focused confirmations remain explicit choices.

Vary these dimensions independently where possible, rather than starting with
their full Cartesian product:

- True regexp selectivity and signature survival separately: sparse through
  pass-all. Include every required literal in many negatives while violating
  order, distance, character class, anchor, or word boundary. Also include
  negatives missing a cheap initial literal, so easy rejection is represented.
- String length distribution, compression ratio, escape frequency, literal
  length/frequency, fragment count, branch count, and repeated-prefix patterns.
  Correlate long strings with survivors in a separate explicit scenario.
- With the survivor set held fixed, vary candidate-window coverage, number and
  spacing of witnesses, false witnesses before the first true match, context
  requirements, and one envelope versus a bounded union. Include very long
  values with tiny matching regions and nearly full-width candidates.
- Few large versus many small FSST tables; same logical data, different table
  granularity. One-shot cold preparation versus warm reuse across query repeats
  and tables, with cache hit/miss and eviction costs exposed.
- Small batches versus large scans, resident working sets versus larger ones,
  and data already decoded for another consumer. Filtering saves no additional
  decode when a downstream operation unconditionally needs every value.
- Per-row values versus distinct-value dictionaries, including row fan-out and
  result materialization. Repeated rows must not silently make a per-dictionary
  method appear to do equally much predicate work.

Use deterministic synthetic data first, then URL/path, log/message, title,
and structured-identifier columns with reproducible preparation. Calico's
ClickBench and string-codec datasets are useful candidates, not the whole
workload. Keep pattern workloads explicit and representative; patterns derived
only from known positives do not establish production coverage or selectivity.

## Timer and accounting

Report one-shot latency and warm scan latency separately. Time pattern
validation, logical lowering, per-table binding/construction, optional code
generation, filtering, candidate masks/gathers, survivor decode, RE2, and
result consumption. Training/compression may be outside a scan timer but
its time, compressed payload, table bytes, and offsets must be reported.
Include query-induced allocation and scratch growth in the relevant phase.
For bounds, separately charge position reporting, interval construction,
compressed endpoint location, boundary/context overfetch, and repeated RE2
calls. Avoid counting bytes skipped during decode as bytes never scanned.

Use a separate accounting instantiation of the same implementation; avoid
timing software counters as though they were matcher work. Record:

- Exact, signature, and fallback counts; emitted LIKEs, literals, branches,
  states, transition/code bytes, preparation time, and table reuse.
- Input rows and distinct values, compressed bytes read, decoded bytes
  materialized, survivors, RE2 calls, true hits, false positives, and zero
  false negatives. State the denominator for any reported rate.
- Candidate intervals before/after coalescing, their decoded coverage, actual
  materialized bytes including context, compressed bytes traversed to locate
  bounds, occurrence-cap fallbacks, and RE2 calls per survivor.
- Candidate/result bytes, peak live scratch and matcher memory, cache size,
  and cold/hot preparation. Codec payload ratio alone is not total footprint.
- End-to-end ns/value and bytes/second with compressed or decoded byte units
  named explicitly. Do not substitute nominal "decode saved" for latency.

Use the same input digest and result checksum across comparands, keeping data,
pattern, and table-training seeds distinct. Retain raw repetitions and spread;
repeat only interesting crossovers with longer measurements. Exploratory local
ARM/VM results are not Zen5 conclusions; record host/compiler/ISA/affinity and
confirm on the intended hardware before choosing an architecture policy.

Reuse [Workbench's experiment and artifact tools](../../tools/README.md) for
source snapshots, dependency pins, command logs, raw samples, and selected
[compact evidence](../../tools/artifacts.md). Follow the earlier spikes in
separating regenerated tables from human findings. A useful first closeout
would state supported rules and counterexamples, where signatures win or lose,
and which preparation/table/selectivity conditions explain the crossover.
