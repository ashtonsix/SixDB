# An IR beyond LIKE

Ashton's further question: could a representation more general than LIKE still
run directly on compressed strings? LIKE is a useful baseline and output form,
not the boundary of the opportunity. The [raw-string probe](experiments.md)
compares it with an initial richer necessary condition before choosing a
compressed implementation.

Useful candidate operations are literal runs, character sets, bounded runs,
ordered concatenation, alternatives, and absolute boundaries. A compiler can
preserve more of a regexp's structure where it is cheap, then relax a subgraph
when branch/state/preparation budgets are exceeded. There is no need to flatten
everything into a large OR of SQL strings. Shared subgraphs may avoid duplicate
work, but also need explicit state/memory accounting.

The implemented comparison retains character ranges that LIKE turns into `_`,
including newline exclusion, and carries ranges through repetition of one
character token. It also retains conservative maximum byte widths on wildcard
gaps created by bounded or folded subexpressions. For example, the LIKE signature
for `id=[0-9]{4};` admits letters in the four positions; the richer condition
still requires digits. Minimum occurrence counts survive explicit expansion
and required leading tokens; general bounded-repeat interval tracking is not
yet complete. Case-fold expansion and a factored Boolean DAG remain open.

Both representations have the necessary-condition contract `regexp => filter`.
Only the explicitly proven LIKE subset is labeled exact in this implementation;
matching every corpus answer does not promote the richer filter to exactness.
Its raw evaluator is reference dynamic programming, with no timing claim.

The compressed opportunity is broader than LIKE because these are regular
constraints. For a byte-state machine, a table-specific transition can compose
the original transitions across the bytes of each FSST symbol. Range and
wildcard tests can then contribute to the precomputed transition rather than
requiring every value to be materialized. This is a mechanism hypothesis,
not evidence that a large regexp automaton is cheap to build or traverse.

Choose the useful middle ground empirically: tiny literal/% machines, richer
bounded/range machines, or a general regexp automaton over codes. Prices include
state explosion, table-specific construction, code size, cache residency,
Unicode boundary state, escapes, matches within symbols, and the ability to
return conservative survivor regions. A representation good for rejection may
be poor at tracking all viable start/end positions.

The raw study can answer whether extra constraints reject enough additional
survivors to justify investigating them. It cannot decide how to implement
these transitions in the forthcoming FSST codec. Preserve the source regexp,
semantic profile, and fallback route regardless of which IR is chosen.

The [factored-filter follow-up](factored-filters.md) now compares Boolean
composition with positional sequences, explicitly measuring what is lost when
relative ordering is discarded and what it costs to retain it.
