# Design questions

These are candidate contracts and analytical expectations for the
[spike](README.md), not selected SixDB interfaces or measured thresholds.

## What information does a signature contain?

Separate a **feature** (a stored fact about a row), its **encoding**, a
**plane** (an independently addressable stream of row codes), and a **stage**
(work performed on a candidate set). A plane may describe several fields;
one field may span several planes. A progressive stage may read existing
encoded data rather than a separately maintained index.

| Representation | Sound evidence | Important limitation |
| --- | --- | --- |
| Exact predicate bits, including NULL/presence and low-cardinality flags | Decide their defined predicates | Workload-specific materialization consumes build/update budget; semantic identity must include expression parameters |
| Dedicated `b`-bit hash tag per field | Unequal tags reject equality; IN probes a set of tags | Equal tags do not prove equality; hash bits give no value ordering |
| Leading bits of an order-preserving value code | Different prefixes reject equality; intervals wholly inside/outside a range can be accepted/rejected | Prefix ties need refinement; physical first bytes are not necessarily most significant or order-preserving |
| Fixed or trained ordered bins | A bin denotes a conservative value interval; boundary bins need refinement | Query thresholds and bin boundaries differ; duplicate quantiles need a defined partition |
| Shared OR of field-salted token masks | Missing any required query bit rejects a positive conjunction | Overlapping fields/tokens cause collisions; deletion cannot simply clear a contribution |
| Tuple tag for a chosen field combination | Reject equality on that full tuple | Not generally usable for arbitrary subsets or ranges; every participating field update changes it |

For fixed scalar schemas, retain dedicated tags as a first candidate based on
Calico's conditional result. For sparse attributes, sets, and text features,
retain shared masks as a separate candidate. Compare usefulness per maintained
byte, including padding, dictionaries, boundaries, versioning, and scratch.
Do not describe all these representations with one Bloom false-positive formula.

For a positive shared-mask conjunction, let `S(row)` OR all encoded row
features and `Q` OR the required features. The test is `(S & Q) == Q`.
An OR of alternatives instead needs alternative tests or a Boolean program:
requiring the union of alternatives would incorrectly require all of them.
Likewise, a range encoded as several possible bins requires **any** covering
bin, not containment of the union of all their signatures.

## Predicate evidence and Boolean composition

For a predicate `P`, maintain lower and upper sets `L_P` and `U_P` over the
incoming visible row set: `L_P` is certainly true, `U_P` is possibly true, and
`L_P ⊆ true(P) ⊆ U_P`. Reject outside `U_P`; refine `U_P \ L_P`.
An exact stage has equal bounds. A necessary-only signature usually has an
empty lower bound. An unsupported predicate starts with empty/full bounds.

For positive AND/OR expressions, intersect/union the respective lower and
upper sets. This preserves soundness and allows exact facts to remove work.
Carry a factored expression and unresolved predicate/branch identities so that
an exact residual is evaluated only where it remains relevant.

For `(A AND B) OR C`, candidate masks combine as `(U_A & U_B) | U_C`.
A row rejected by `A` must still be considered for `C`. Only a **certain** `C`
match can certify the whole expression and skip `A/B` residuals. A possible
`C` match cannot. The equivalent CNF is `(A OR C) AND (B OR C)`; a plane
covering `A,C` could answer the first clause in one pass. This is useful even
if the other clause needs a different plane or the base data.

Conversely, a plane covering only `A,B` cannot reject this query if `C` is
unknown: substituting possible-true for `C` makes its upper bound full.
Predicates inside a branch are not automatically mandatory query filters.
Do not lose this distinction when pushing fragments into a data plane.

The bounds above concern SQL **TRUE**, not necessarily SQL FALSE. Under
three-valued logic, `NOT P` is not the complement of `true(P)` when NULL is
possible. Either normalize nullable predicates with explicit truth/false/NULL
evidence or require a sound dedicated negative predicate path. Never negate
a necessary-only mask and call it an exact `NOT`, `!=`, or anti-join filter.
Even exact atom truth masks need NULL handling before complementation.

Also specify collation/case folding, string versus byte semantics, signed and
floating ordering (including NaN and signed zero), decimal conversion,
dictionary identity, and whether missing differs from NULL. A hash must agree
with the chosen equality relation. For arrays/objects, distinguish predicates
on the same element from independent existential predicates. Volatile or
error-producing expressions need explicit rules before moving evaluation.

## Progressive features beyond a first-byte equality check

- **Ordered prefixes:** interpret a prefix as a value interval. For `x > t`,
  an interval wholly above `t` certifies, one wholly at/below `t` rejects,
  and the boundary interval remains unresolved. Test signed/float normalized
  codes and cell-local transforms separately from raw unsigned values.
- **q25/median/q75:** three exact threshold bits have at most four ordered
  states and can be encoded in two bits with shared boundaries. Compare that
  compact bin code with three directly usable predicate bits. Tied quantiles
  reduce information; equal population is not automatically best for the
  actual query thresholds or costs.
- **Hierarchical bins or dictionary codes:** refine only boundary bins or
  ambiguous dictionary classes. A dictionary can evaluate an expensive query
  once per distinct value, followed by code membership; charge dictionary
  preparation, updates, and scope. Unordered IDs cannot answer ranges unaided.
- **Text:** compare an exact bit for a frequently reused term with reusable
  token/n-gram signatures, length/prefix/character-class facts, and necessary
  literals from [regexp lowering](../regexp-lowering/design.md). Whole-string
  equality hashes do not reject arbitrary substring queries. Token membership
  and substring containment have different semantics; short strings/patterns,
  Unicode normalization, and absent extractable grams need explicit fallbacks.
- **Expensive computed predicates:** coarse spatial/time bounds, parsed type
  or path presence, and conservative summaries of deterministic expressions.
  The value is avoiding a decode/function call, subject to its exact semantics.

Adding hash bits reduces collisions but never turns a finite fingerprint into
proof of equality. Adding ordered value bits can eventually decide the
predicate exactly. A projection or same-field aggregate may need all those
bits anyway; reuse decoded data/prefix state instead of charging the direct
path for repeated work while giving signatures free reuse.

## How wide, and how many planes?

Keep four knobs independent: total bits per row, bits per feature, physical
plane element width, and rows per execution/rollup group. An 8-bit element
might be eight exact flags, two four-bit tags, or one ordered prefix. Width
alone says little about rejection strength.

First hold 32 bits of row information fixed: `1×32`, `2×16`, `4×8`, plus a
bit-sliced control and a column grouping control. If every plane is read,
splitting does not reduce stored payload bytes. For a SIMD register of `V`
bits, byte lanes offer `V/8` rows per operation versus `V/16` and `V/32`, but
more streams/instructions and mask combination can consume that advantage.
Mask extraction, byte comparison support, widening, and gather costs depend
on the actual ISA and compiler; lane counts are not throughput measurements.

| Choice to test | Conditions that could favor it | Conditions that could defeat it |
| --- | --- | --- |
| 8-bit planes | A small feature subset answers common queries; early planes empty many later groups; hot/cold features separate well; byte-domain lookup evaluates a rich fragment | Most groups still visit all planes; several weak masks must be combined; scattered candidate probes touch many streams |
| 16-bit planes | A byte leaves costly ambiguity, another byte removes it; two related byte features are usually requested together; fewer handoffs/loads pay | An 8-bit stage would already settle most groups; extra bits are correlated or only rarely queried |
| 32-bit planes | Many co-used features resolve a complex fragment together; most rows would reach all narrower planes; random probes or update publication benefit from locality | High unconditional scan traffic; few relevant bits per query; expensive residuals still survive a saturated shared mask |

For an ideal independent hash, a nonmatching equality survives `b` bits with
probability `2^-b`. For a nonmatching conjunction, its survival depends on the
sum of tag bits of the **failed** predicates, not all queried predicates.
One true atom contributes no rejection. Repeated values, correlated fields,
shared hashes, overlapping tokens, and fixed query collisions invalidate
casual multiplication; measure conditional survivor rates on actual candidates.

Charge an additional plane against the residual work it avoids:

```
extra plane reads + evaluation + masks/handoffs
    < residual cost before refinement - residual cost after refinement
```

The two residual costs include grouping, locality, and output demand. An
IID diagnostic is that a group of `g` rows is visited with probability
`1 - (1 - p)^g` when each row independently reaches the next stage with
probability `p`. At `p = 1/256`, this is about 6%, 22%, and 63% for groups
of 16, 64, and 256. Few survivors can still touch many groups. Clustering
changes this, and reaching a SIMD group does not equal a DRAM transfer.

Measure plane-major, row-major, and small tiled layouts. Separate skipping
execution from skipping cache lines, compressed pages, and I/O requests.
Existing value planes can be a zero-extra-payload signature source, but their
placement may prevent reading the useful prefix densely. Duplicating a prefix
into a scan stream trades maintenance/storage for that locality.

## Query-wide planning and handoff

A data plane could advertise represented facts, semantic/version identity,
supported Boolean operations, and the evidence it returns. For an 8-bit code,
consider compiling a bounded fragment into a 256-entry decision table or an
allowed-code bitset; compare that with AND/mask/compare kernels. A 16-bit table
has 65,536 entries and a full 32-bit table is impractical, motivating algebra,
factorization, or selected specialization. SIMD evaluation still needs pricing.

Plan **fragments and residual work**, not just an ordered list of independent
predicates. Reuse shared atoms, identify clauses entirely covered by one plane,
and choose between deeper refinement of one predicate and a first look at
another. For conjunctions, rejection is valuable; for disjunctions, certification
can be valuable. Avoid unrestricted CNF/DNF expansion: bound compilation and
retain an expression DAG with an exact fallback.

Useful handoff state includes row coordinates/edition, incoming mask, certain
and possible truth, unresolved obligations, and reusable prefix/decoded state.
Measure the cost of that state before selecting a masks-only or richer API.
Do not require Calico's 256-row mask, CPS protocol, or cell size.

The cost model needs observed conditional survivors, nonempty groups, residual
bytes/calls, and locality as well as preparation cost and plan reuse. Compare
fixed ordering with bounded online sampling and region-local bypass; charge
sampling, switching, and poor initial choices. A no-filter plan remains a
normal alternative. Shared scans and batches of related queries may amortize
one plane read but require separate per-query truth state.

The same evidence can prune work after an index lookup or before a join probe,
but runtime join filters, outer joins, anti joins, and NULL-sensitive IN/NOT IN
need their own legal placement. Signatures cannot invent a base-row identity
or cross an operator where the predicate is not valid.

## Mutation, drift, and physical design

Freeze bin boundaries/normalization/hash parameters within an encoding edition.
Data drifting away from old quantiles hurts selectivity, while a correctly
maintained code under those fixed boundaries remains sound. Changing boundaries
requires rebuilding, compatible translation, or explicit mixed-edition query
handling. A new row outside the training range must have a representable
overflow interval or bypass, never be silently omitted.

Define visibility before concurrency: a negative test must cover every version
visible to its reader. Latest-only signatures cannot reject older snapshots.
Candidates include edition-aligned immutable planes, versioned signatures, or
conservative old/new evidence plus a dirty-row bypass. For hash tags, old OR
new is not an equality tag. Publishing a single atomic word is insufficient
when its data, other planes, or rollups can come from another edition.

Replacing a dedicated tag updates one slot; a shared OR mask needs a rebuild,
retained contributions/counters, or a conservative stale superset. Inserts must
be reflected in every pruning level before they can become visible there.
Deleted contributions can remain for rejection-only supersets at a precision
cost; acceptance certificates and old readers impose additional rules.

Compare permanent features, workload-selected materialization, on-demand
construction, and reuse of existing data planes. Include query-parameter
rotation, feature eviction, retraining, temporary doubled storage, WAL/replicated
bytes when available, and recovery/rebuild cost. Remapping/splits change block
membership and mask coordinates even if row values are unchanged. The linked
[rollup note](rollups.md) develops the resulting merge/repair questions.
