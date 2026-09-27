# Commercial questions

## Current intent and evidence

Ashton clarified on 27 September 2026: **database first; the broader compute
vision remains an option to investigate**. SixDB needs a reason for customers
to adopt a database independently of any eventual market for compute. The
older [Consurgent pitch](../../../consurgent/pitch/SCRIPT.md) is historical
context, not current positioning, financing terms or a delivery commitment.
Consurgent's [identity](../design/identity/README.md) supplies broader intent;
it does not settle the product or business model.

The [project overview](../../README.md) describes a clean rebuild. Ikea has
implemented components; Engine, Loom and Shore remain provisional, and Orbital
has research and designs rather than runtime services. Calico is reference
material. The completed-database benchmarks and operational strength assumed
in the earlier hypothetical valuation discussion are not SixDB results.
Customer demand, replacement scope and willingness to pay remain questions;
no customer evidence was established during this onboarding.

## Commercial LEAD's contribution

Find where SixDB's capabilities could create enough customer value to justify
adoption, then distinguish appealing explanations from evidence. The remit
includes buyer and use-case discovery, positioning, adoption and migration,
pilot proposals, customer economics, packaging, pricing and distribution.
Licensing, competitive differentiation and financing belong here when they
affect those choices. This is a provisional scope, not a parallel product
roadmap or a mandate to turn every technical investigation into a sales case.

Ashton sets company direction and commitments. Commercial develops and
challenges recommendations, prepares discovery work, and carries useful
customer requirements to their technical owner. External customer or investor
contact requires Ashton's instruction. The technical owners retain architecture,
correctness and measurement judgment; Commercial owns the interpretation of
what supported behavior might mean to a buyer.

Work with Orbital on placement, recovery, mixed-workload dependencies and the
limits of operational claims. Work with Scaffolding on evidence navigation and
documentation fit. SQL behavior, scheduling and external integration questions
belong with the relevant Engine, Loom and Shore work as it develops. Consult
the existing tasks directly when useful, following the
[collaboration guidance](../collaboration.md); keep findings with their owner
instead of maintaining a separate technical specification or task roster here.

## Starting hypotheses

**An entry point can be narrow even when the engine is general.** Analytics and
application teams may need distinct explanations, examples and adoption paths
around one coherent product. The [Team Topologies interpretation](team-topologies.md)
also suggests a team owning both needs within one business capability. Test
which first workload it can adopt and operate with less cognitive load and
waiting on other teams. Existing ownership matters; preserving technology
silos indefinitely is not the objective. Consolidation can follow without
being a prerequisite for that team's benefit.

**Connected teams need explicit operating boundaries.** Separate landing pages,
shards or deployments do not by themselves establish independence. The
[reservation-bridge counterexample](../simulator/experiments/README.md) shows
how an overlapping writer can transmit WAN delay to otherwise local work;
relaxing its queue rule introduces a starvation risk. Shared results also
create [retention obligations to slow consumers](dataflow-workloads.md#f18--shared-work-and-demand-dependent-pruning).
These are modeled or conceptual limits to investigate, not predictions of
customer latency. A pipeline may also carry schema, freshness, access and
ownership agreements that must survive removal of its copying machinery.
Expansion should preserve the first team's useful service and ability to
change its own systems.

**Faster work must turn into a benefit someone can capture.** Candidate benefits
include fresher decisions, predictable application latency during analytics,
less paid capacity and less operator work. Compare equivalent completed work,
freshness and recovery promises, including SixDB's price, integration,
migration, temporary duplication and ongoing support. Lower total bytes need
not mean a lower bill: direction, billing boundaries and committed capacity
matter. The [network survey](dissemination/SOURCE-SURVEY-NETWORK.md) retains
useful counterexamples; its historical tariffs are illustrative.

**Placement and recovery may matter without defining a vertical.** Finance and
healthcare are candidate discovery contexts, not established target markets.
Find the concrete constraint and buyer before presenting placement controls
as a regulated-industry fit. Data, metadata, support access and failure domains
can all matter. Placement features alone do not establish compliance.

**Openness and a paid offer must be considered together.** Open source could
help evaluation, inspection, integration and trust. It is not yet a settled
SixDB licensing decision or sufficient evidence of adoption. Distinguish open
source, source availability and evaluation access. Identify what a customer
would pay for and what can actually be delivered around a release; neither
benchmark attention nor code visibility guarantees a commercial footprint.

## Keeping claims useful

Keep the kind of evidence attached to a claim. A native component measurement,
an authored simulator cost, a bounded formal result and a customer's operating
experience answer different questions. [Orbital's brief](../../orbital/BRIEF.md)
contains targets; [CFT measurements](../spikes/cft-commit-latency/RESULTS.md)
and the [simulator](../simulator/README.md) state narrower findings. They do not
combine automatically into database performance, complete correctness or a
measured disaster-recovery envelope.

For discovery, the useful unknowns are concrete: who feels the problem, what
triggers a change, what they can adopt first, which dependencies must keep
working, and which cost or service improvement they can recognize. Retain
observations that change a decision and revise these hypotheses accordingly.
Technical lead duration, competitive response and financing requirements also
need their own evidence; the earlier hypothetical estimates are not company
assumptions.
