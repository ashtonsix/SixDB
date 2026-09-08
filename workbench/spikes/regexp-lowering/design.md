# Lowering guarantees and candidate mechanisms

This preserves the broader design space, not an Engine interface. The
[completed study](experiments.md) stops at raw-string efficacy and
[considers a richer IR](richer-ir.md); the [closeout](CONCLUSIONS.md) states the
provisional recommendations. FSST implementation and timing are
deferred while Ashton reworks the codec. `R(x)` denotes a
validated regexp predicate with explicit options and matching mode. `L(p, x)`
denotes a whole-value LIKE predicate. Equivalence and implication below concern
non-NULL subjects; the SQL wrapper preserves NULLs and errors separately.

## Establish the semantic profile

The completed corpus studies use valid UTF-8 and RE2 search with the source
patterns' case flags. Correctness checks additionally cover Latin-1/byte mode.
These are experiment configurations, not a selection of SixDB's string semantics.

Each lowering records the profile it was proved against:

- Search versus full-value match. Search normally needs leading and trailing
  `%`; absolute anchors remove them. Multiline anchors are not absolute.
- Byte versus code-point consumption, validity of UTF-8, and the meaning of
  LIKE `_`. One byte and one Unicode character are not interchangeable.
- Dot/newline behavior, including inline flag scopes. RE2's default dot
  excludes newline, while `%` admits it in the proposed binary LIKE profile.
- Case sensitivity, case folding, collation, and normalization. A folded
  regexp literal cannot safely become a case-sensitive LIKE literal.
- Literal `%`, `_`, escape bytes, NUL, and empty values. Use length-delimited
  operands and distinguish literal bytes from wildcard tokens in the IR.

These RE2 distinctions are specified in its
[syntax reference](https://github.com/google/re2/blob/main/doc/syntax.txt).
For example, RE2 `$` means end of text with multiline disabled; do not import
another regexp engine's before-final-newline rule. A known column constraint
such as newline exclusion could justify an additional exact rule, but it must
be guaranteed for all eligible values, not inferred from a sample.

Validate the original pattern and its options before taking an optimization.
An invalid pattern remains an error, even if a cheap predicate would reject
all rows. Exhausting an optimization budget declines or weakens the lowering;
it does not redefine which regexp patterns SixDB accepts. Dynamic patterns
need their own preparation/reuse study; the first probe uses constant patterns.

## Exact lowering

Use the parsed structure with scoped options rather than textual replacement
of punctuation. Candidate internal tokens are `Literal(bytes)`, `AnyOne`, and
`AnyMany`, with absolute start/end information and bounded alternatives.
SQL LIKE strings are a rendering of this representation, with an explicit
escape convention. This avoids inheriting Calico's inability to escape LIKE
metacharacters.

An exact fragment has the same language as its regexp fragment. Concatenating
exact fragments preserves equivalence; combining alternatives with OR does
too. Full-value boundaries and outer search wildcards are attached only after
their context is known. A conjunction of independent substring checks usually
loses order and multiplicity, so it is not generally an exact concatenation.

Candidate rules, subject to the semantic profile:

| Regexp structure | Exact candidate |
| --- | --- |
| Literal or literal-only group | Escaped LIKE literal, with search wildcards as needed |
| Absolute start/end anchors | Remove the corresponding outer wildcard; fully anchored literals can use equality |
| Dot matching every unit that `_` matches | `_` |
| Repetition of that unrestricted dot | `.*` to `%`; `.+` to `_%`; fixed counts to repeated `_` |
| Finite character class or literal alternatives | Bounded OR of exact branches |
| Optional fragment or bounded repetition | Bounded expansion including the empty branch where required |
| Capturing group used only for Boolean existence | Analyze its child; captures themselves are not being returned |

Normalize adjacent literals and repeated `%` tokens. Track expansion count,
total literal bytes, automaton states, and preparation time; avoid exponential
cross-products. An exact result outside those budgets may still have a useful
signature. "Declined" means this lowering did not prove an affordable answer,
not that no equivalent LIKE expression exists.

## Signature extraction

The signature describes a superset of regexp matches. It may be one LIKE or
a positive Boolean expression of LIKE leaves. Keep that guarantee explicit
in the result so an executor cannot confuse a candidate with a confirmed hit.

Two representations are worth comparing:

1. A bounded set of LIKE fragments approximating the language of a matched
   region. Concatenate fragments to retain literal order, then apply the
   surrounding search/anchor context. Relax a restrictive class to `_` only
   when character units agree; otherwise `%` is a safe broader consumption.
2. A bounded AND/OR DAG of predicates over the subject. This can retain useful
   branch conditions without expanding every combination. Keep mandatory
   literals, nullable information, and bounded exact prefix/suffix sets to
   discover literals across concatenation boundaries.

For unanchored subject-level necessary conditions `F(A)` and `F(B)`, the basic
safe rules are:

| Structure | Necessary condition |
| --- | --- |
| Literal `a` | `L('%a%', x)`, with literal escaping |
| Concatenation `AB` | `F(A) AND F(B)`; an ordered fragment may be stronger |
| Alternation `A or B` | `F(A) OR F(B)` |
| `A?`, `A*`, or repetition with minimum zero | `TRUE` for this fragment |
| `A+` or repetition with positive minimum | `F(A)`; additional copies need an occurrence/order proof |
| Assertion or unanalyzed construct | `TRUE` for this fragment |
| Proven empty language | `FALSE` |

Dropping an assertion broadens the language. Children analyzed as required
substrings must not accidentally inherit whole-value anchors. Optional content
must not become mandatory. For `foo(bar)?baz`, dropping `bar` cannot produce
the adjacent literal `foobaz`: the longer valid match lacks that substring.
Keeping `%foo%baz%` is safe; expanding both exact alternatives is another choice.

On budget exhaustion, only weaken a signature: drop an AND requirement,
replace a subexpression with `TRUE`, shorten a mandatory literal, or remove
an order constraint. Never discard an OR branch unless its matches remain
covered. `foo|[0-9]+` has no mandatory literal shared by both branches; a
literal-only extraction becomes `TRUE`, not `%foo%`. A common length condition
could still help in a richer representation.

Single longest-mandatory-literal filtering is the cheap control. Compare it
with ordered chains, factored AND/OR conditions, and bounded exact expansion.
Rank candidates using measured rejection and cost; long literals can be very
common in the actual data. Share repeated leaves and, where practical, scan
several conditions in one pass. Shared automata can also enlarge the state
space, which is part of the comparison.

## Examples and counterexamples

These are proposed mappings, not results from an implemented lowering. They
use case-sensitive search, absolute anchors with multiline disabled, and LIKE
wildcards that consume bytes including newline. `(?s)` enables dot/newline
matching. LIKE operands below show pattern bytes, not SQL string escaping.

| Regexp | Candidate LIKE expression | Guarantee / useful counterexample |
| --- | --- | --- |
| `needle` | `%needle%` | Exact |
| `^needle`, `needle$`, `^needle$` | `needle%`, `%needle`, `needle` respectively | Exact |
| `(?s)^a.*b$` | `a%b` | Exact |
| `^a.*b$` | `a%b` | Signature: `a` + newline + `b` is a false positive |
| `(?s)a.b` | `%a_b%` | Exact in this byte profile |
| `a.b` | `%a_b%` | Signature: `_` also admits newline |
| `^user:[0-9]{3}:ok$` | `user:___:ok` | Signature: accepts `user:abc:ok` |
| `foo(bar)?baz` | `%foobaz% OR %foobarbaz%` | Exact; `%foo%baz%` is a cheaper signature candidate |
| `(?:fatal\|error).*id=[0-9]+` | `%fatal%id=% OR %error%id=%` | Signature: preserves each branch and literal order |
| `\bcat\b` | `%cat%` | Signature: accepts `concatenate` |
| `a+` | `%a%` | Exact for unanchored Boolean search; `^a+$` only has this as a signature |
| `(?s)^a.*a$` | `a%a` | Exact; `a` must fail, since the two occurrences cannot overlap |

For literal `a%b_c`, an explicit `!` escape would render the search LIKE as
`%a!%b!_c%` with `ESCAPE '!'`. Escaping the escape character itself is also
required. If the matcher cannot represent a needed token, decline that route
or safely weaken a signature; do not silently reinterpret the literal.

An unconditional empty alternative in an unanchored regexp matches every
non-NULL subject. `^$` instead matches only the empty value. Nullability of a
fragment must not erase assertions around it.

## Execution over FSST

Keep two transformations separate: regexp to an exact/signature expression,
then each LIKE expression to a matcher specialized for a particular FSST
table. Logical lowering is reusable across tables; a table-specific matcher
must be rebuilt or looked up using the correct table identity and semantics.

Do not search for a single compressed encoding of the literal in the code
stream. A literal can begin or end inside a symbol, cross symbol boundaries,
or occur beside escaped bytes. Adjacent LIKE fragments can consume different
parts of the same symbol. Code byte 255 and its following raw byte must be
handled as an escape pair, not as two dictionary symbols.

A simple reference candidate lifts a byte automaton's transitions across each
decoded symbol at preparation: `T[q, c] = delta*(q, symbol[c])`. Correct state
includes any acceptance reached inside the symbol when that matters, plus
escape handling and end-of-value conditions. Execution then traverses codes
without materializing each decoded value. Price the transition table and its
construction; state count can make this unattractive. Compare this with the
specialized prefix/middle/suffix construction in the
[FSST LIKE paper](https://db.in.tum.de/~pop/fsstlike.pdf), whose assumptions and
implementation are discussed in [the reading note](literature.md).

The first compressed subset can use literal/% patterns. `_`, Unicode character
boundaries, and folding need demonstrated matcher support. A logically exact
LIKE that the compressed matcher cannot handle may use decoded LIKE or RE2;
logical coverage and compressed execution coverage are separate measurements.

For a signature, run the filter on compressed values and decode survivors
once into reusable scratch for RE2. The candidate mask is an execution step:
a SQL `S AND R` rewrite alone does not ensure that RE2 is delayed. Row masks,
dictionary match sets, and gathered batches are alternative placements to
price, with no requirement to retain Calico's dictionary organization.

A further candidate is [survivor bounds](survivor-bounds.md): retain positions
and conservative possible-match intervals, then decode only those intervals
and required context. This needs a separate completeness guarantee; a single
LIKE witness is not automatically the only possible regexp match. Position
collection, seeking within FSST, and repeated RE2 calls have their own costs.

For non-NULL values, negated regexp evaluation can accept immediately when
`S` fails; survivors still require `NOT R`. `NOT S` alone is incomplete and
`S AND NOT R` is wrong. Exact equivalence does permit direct negation. Keep
the NULL mask intact in projection and nested Boolean expressions, not only
WHERE filtering. Extraction, replacement, and capture results are outside
Boolean predicate lowering: confirming existence does not supply those values.

## Costs that decide whether a route is useful

For `N` candidate values, a rough one-query comparison is:

```
direct    = P_R + sum_all(D_i + R_i)
signature = P_R + P_S + sum_all(F_i) + M + sum_survivors(D_i + R_i)
bounded   = P_R + P_S + P_W + sum_all(Fpos_i) + M_W
            + sum_survivors(seek_i + D_intervals_i + R_intervals_i)
exact     = P_validate + P_E + sum_all(E_i) + M_E
```

`P` includes the relevant logical and per-table preparation, `D` value decode,
`R` RE2 execution, `F` signature execution, and `M` candidate/result handling.
The bounded arm adds region preparation `P_W`, a signature with position
reporting `Fpos`, interval handling `M_W`, endpoint seeking, and verification
of intervals including required context. It may trade one RE2 call for several.
Prepared plans amortize some `P` terms; their cold costs remain visible. Price
the same result consumption in every route. The signature wins only when
avoided decode/match work exceeds its preparation, filter, and mask costs.

Survivors may be longer and more expensive than rejected values, so survivor
fraction alone is insufficient. Record rejected decoded bytes and RE2 work,
not just rows. Table churn, cache pressure, already-decoded values, and tiny
batches can change the winner. Include direct RE2's own optimizations in the
baseline; compare additional LIKE work against what RE2 actually does.

A scan can bypass a low-rejection signature after observing the first few
containers and use direct RE2 for the remainder. That is a performance decision
between equivalent pipelines, not a semantic weakening. Source-locality can
make the prefix misleading, so compare one-shot switching with periodic probes
and hysteresis. The raw [controller replay](experiments.md#switching-replay)
measures work traded and missed rejection opportunities; selecting cost-aware
thresholds awaits the new codec. A rich-signature-to-LIKE transition is another
candidate when the extra constraints do little useful work.
