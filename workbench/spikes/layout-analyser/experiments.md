# Test the connected account

Reframed 2026-09-14. The next useful result is an end-to-end comparison in which
information, representation, operation and reuse choices participate in the same
decision. Extending every axis of the first benchmark pass independently would
not establish the [proposed direction](design.md).

The [first findings](findings.md) and [retained evidence](evidence/README.md)
remain supporting controls. They establish boundary sensitivities and some bad
selection assumptions. They have not demonstrated a general analyser, independent
container learning or historical adaptation.

## The central comparison: does choosing storage first lose useful programs?

Use the [connected record case](case-study.md) as the main experiment. Start with
its exact binary-string and fixed-field semantics; add richer predicates and
sparse exceptions when their particular interaction is being tested. No generic
schema system or learned cost model is required.

The first finite universe contains I8, I7, relocated-X, separate-prefix and a
padded-I8 control. For each, admit no extra filter or the same compact string tag.
Use actual lengths, tails, descriptors and allocations. Keep tag information
matched when attributing a benefit to placement. Include known-row projection,
negative and successful string lookup, full reconstruction and partial replacement.
The 63B-to-64B ninth-prefix-byte fixture is a separate control family.

For each representation, enumerate a small explicit set of valid programs:
filter use/bypass where applicable, staged versus eager acquisition, and supported
packet/consumer alternatives. Resolve real Ikea child formats and operation maps.
Reject infeasible combinations with a reason. The reference is exhaustive only
over this declared finite universe; an unimplemented alternative is not a loss.

Compare selection policies under the same objective and evaluation budget:

| Policy | Decision restriction being tested |
| --- | --- |
| Fixed baseline | Always use the starting representation and its ordinary program |
| Density first | Choose smallest admitted occupied representation, then its best program |
| Storage first | Fix evidence/planes using projection/co-access summaries, then optimize placement and programs |
| Joint finite reference | Compare complete admitted representation/program combinations |
| Bounded joint search | Retain candidates for omitted work, dependency depth, footprint or maintenance, then price a shortlist |

Supply read-heavy, reconstruction-heavy and update-heavy scenarios with stated
space limits and binding reuse. Keep query IDs, values, mutations and results
identical across candidates. Price complete consumers, including exact refinement,
shared state, preserving effects and the stated owner boundary. Record how often
each next information source and physical group is needed, and where addresses
become known. Separate those counts from runtime and measured traffic.

A useful positive result is a repeatable case where a storage-first restriction
excludes a materially better complete candidate, with the responsible dependency
identified. A null result also matters: if a small fixed family and ordinary
programs match the joint reference, a more general search has not earned its cost.
Report near-ties and omitted candidates, not only selected winners.

## Add mechanisms only to discriminate a live explanation

Several source mechanisms can be introduced into that same case. Each has a
specific comparison rather than a separate broad benchmark matrix.

- **Conditional evidence:** introduce a Boolean alternative with exact evidence
  on one branch and necessary tags on another. Compare joint versus marginal
  summary encodings, then independently vary their plane placement. Verify every rejection
  and certification against exact truth. At equal row outcomes, contrast clustered
  and scattered unresolved rows. This tests whether the decision needs Boolean
  obligations and group occupancy beyond co-access/selectivity summaries.
- **Common values and exceptions:** add sparse patches to selected fixed fields.
  Compare a dense moved byte, a sparse plane, and shared versus separate patch/tail
  payload where implemented. Use joint patch patterns, not only independent rates.
  Include the actual rebuilding/repair work. This tests whether saved common-path
  bytes repay address discovery and maintenance.
- **Execution without migration:** use the current A8/B4/C4 grouped TuplePack
  example as a correctness and binding control, then price relevant programs in
  the record consumer. Keep the bytes identical. This tests whether operation
  adaptation is useful after storage is fixed, without presuming grouping wins.
- **Readiness and hardware:** compare a directly computed extension address with
  a locator discovered from the core. Use serial and a small admitted independent
  request set, a hot control, and a qualified larger working set. Retain phase
  and footprint. This tests whether a spatial result transfers across a changed
  dependency path; it need not identify a prefetch mechanism.

Use the nested bucket/directory case as a transfer test after the record model can
express its choices. It should require child representation, capacity, evidence,
address computation and retained state, rather than a special bucket score.
Compare N/fingerprint choices with actual occupancy and overflow, key-only versus
value paths, and fresh versus retained directory traversal. Same-entry metadata
updates cannot stand in for length-changing repair. A successful record-only
model that cannot express these dependencies is still incomplete.

## Can a small family transfer to a new region?

Freeze an admitted candidate universe and its complete programs before evaluating
selection. Use independent immutable containers in two known key regions, a held-out
region and a later changed workload window. Generate or capture new values and
queries per container; shuffled rows or different mixes from one retained capture
do not supply independent historical evidence.

Compare one fixed layout, a collection-wide choice, a small palette of concrete
layouts, and a palette of structural families with bounded local parameter fitting.
Use equal local evaluation budgets, initially small budgets such as two/four
family attempts. Keep the exhaustive finite local reference for evaluation only.
Train and tune on disjoint containers from those used to report regret.

The discriminating cases have similar pooled means but different local usefulness:
string discrimination positions, clustered unresolved groups, joint patches and
projection/update mix. Candidate family identity can transfer; parameters and
rankings may need local fitting. Include unseen-region fallback and stale advice.
Acquisition time, inspected rows, local fitting/measurement cost, fallback rate,
footprint and held-out regret all count. Report comparison uncertainty when the
reference's measured plans are close.

This distinguishes two claims that the first offline study could not: whether
a diverse shortlist helps within a known capture, and whether reusable hypotheses
help select for new data. If cheap local statistics fail, the result may support
bounded local measurement or a robust fixed family rather than a learned selector.

## Can advice improve while old images stay put?

Reuse those containers and change the later workload. Compare keep, rebind,
new-data-only and migrate. Retain at least two image representations and an old
reader. Changing advice must not change how either image decodes. Include mixed
layout dispatch and charge any legal grouping of work.

For migrations, price implemented conversion and repair; put unsupplied owner
publication/durability/interference costs into explicit scenarios. Account for
source/destination overlap and new binding, and treat existing preparation as
sunk. Show which horizons favor which action. Do not label scenario arithmetic
as measured online migration.

The result should demonstrate a useful path even when migration is unattractive,
or show that the extra palette/binding/dispatch complexity does not repay itself.
That is a stronger test of adaptive design than predicting a new best layout
while silently assuming every historical byte can be rewritten.

## What makes this evidence useful

Retain candidate definitions, dependency/repair explanations, exact outputs,
rejections and objective inputs alongside complete comparison rows. Tie costs
to source, executable, hardware, placement and consumer identity. Use the
[existing measurement tools](../../tools/README.md) and
[retention conventions](../../tools/artifacts.md); no new benchmark framework or
cloud fleet is needed to assemble the finite problem.

The desired research result is an explained choice and its limits: which
information could be deferred, which program exploited it, what local property
changed the verdict, how much analysis was reused, and whether adapting required
new bytes. Those observations would sharpen the vision even if they still do not
justify specifying an Engine module.
