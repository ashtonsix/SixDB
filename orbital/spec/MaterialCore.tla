---------------------------- MODULE MaterialCore ----------------------------
EXTENDS Contracts

\* A replaceable application fixture. Orbital carries the recipe and tokens;
\* it does not interpret array indices. The independent model observer uses
\* reconstructed bytes, not a Boolean 'material available' supplied by a root.
\* Each immutable token has kind, interpretation and content fields. A patch
\* token names a cell and replacement byte; code is itself a required token.

Needed(recipe) == {recipe.base, recipe.code} \cup Elements(recipe.patches)

Complete(data, recipe) == Needed(recipe) \subseteq DOMAIN data

WellTyped(data, recipe) ==
  /\ Complete(data, recipe)
  /\ data[recipe.base].kind = "base"
  /\ data[recipe.code].kind = "code"
  /\ data[recipe.code].content = "replace-byte-v1"
  /\ \A t \in Needed(recipe) :
       data[t].interpretation = recipe.interpretation
  /\ \A t \in Elements(recipe.patches) :
       /\ data[t].kind = "patch"
       /\ data[t].content.index \in 1..Len(data[recipe.base].content)

RECURSIVE Evaluate(_, _, _)
Evaluate(data, recipe, n) ==
  IF n = 0 THEN data[recipe.base].content
  ELSE LET before == Evaluate(data, recipe, n - 1)
           change == data[recipe.patches[n]].content
       IN [before EXCEPT ![change.index] = change.value]

Reconstruct(data, recipe) ==
  IF WellTyped(data, recipe)
  THEN Evaluate(data, recipe, Len(recipe.patches))
  ELSE None

\* Coverage roots name a set of recipes plus pending semantic obligations.
\* A future result does not become a recipe merely by giving it an identifier.
RecipeClosure(recipes) == UNION {Needed(r) : r \in recipes}

\* Bounded dependency closure for descriptors that reference other descriptors.
\* This rejects a cycle supported only by its own declarations. An independently
\* present base closes a dependency without recursively demanding another root.
RECURSIVE Resolved(_, _, _, _)
Resolved(nodes, edges, bases, n) ==
  IF n = 0 THEN bases
  ELSE LET prior == Resolved(nodes, edges, bases, n - 1)
       IN prior \cup {x \in nodes : edges[x] \subseteq prior /\ edges[x] # {}}

Grounded(nodes, edges, bases) ==
  nodes \subseteq Resolved(nodes, edges, bases, Cardinality(nodes))

=============================================================================
