# SixDB through Team Topologies

Commercial interpretation, 27 September 2026. These are adoption hypotheses,
not customer findings or requirements imposed on the architecture.

[Team Topologies](https://teamtopologies.com/key-concepts) focuses attention on
the flow of useful changes, teams' cognitive load and their interactions.
Applied to SixDB, the question becomes: what can a team deliver and operate
with less waiting, learning and coordination after adopting it? Database speed
and the number of systems removed are inputs to that question.

## A coherent workload within one team's ownership

The proposed analytics and applications entry points could meet buyers where
they are. However, those technical categories do not necessarily identify
team boundaries. A team responsible for a business capability might need both
transactions and analytics; an analytics product team might own a distinct
customer outcome. Preserving today's technology silos indefinitely is not the
objective. A useful adoption path respects current ownership while allowing
avoidable handoffs to disappear.

A candidate first use case is a product team adding live reporting to an
operational application it already owns. If it can do this without establishing
another storage system and recurring work in another team's queue, HTAP could
increase its scope of independent delivery. This is a discovery candidate,
not a conclusion that such teams will prefer SixDB to their existing options.

The converse matters: replacing a familiar database with a system that requires
understanding transactions, streaming, analytics and distributed scheduling
could increase the team's burden. The engine's breadth needs to be accessible
through a small, useful surface with sensible defaults and clear operating
responsibilities.

## Preserve contracts when removing machinery

A pipeline can carry ownership agreements as well as bytes: schema evolution,
validation, freshness, access, lineage, retention and support expectations.
SixDB might remove copying or orchestration without removing those obligations.
Unrestricted querying of another team's mutable internal tables could make
changes harder even when every query becomes faster.

For example, an orders team could retain ownership of its operational schema
while a reporting team consumes a documented, versioned interface with agreed
freshness and access. Whether that interface should use views, events, derived
data or another mechanism is a technical and customer question. Sharing an
engine does not decide it. Team Topologies' [Team API concept](https://teamtopologies.com/resources)
also makes responsibilities and interaction expectations explicit; a SQL
interface alone cannot settle who handles an incident or a breaking change.

Logical ownership, resource sharing and physical deployment are separate
choices. Existing [modeled dependencies](../simulator/experiments/README.md)
and [retention questions](dataflow-workloads.md#f18--shared-work-and-demand-dependent-pruning)
show why team labels or separate schemas cannot establish operational
independence. Customer promises about workload interference, recovery scope
or independent upgrades would need appropriate technical support.

## Two routes to adoption

| Initial adopter | Candidate value | What the offer needs to establish |
| --- | --- | --- |
| A product team with authority over a bounded workload | Deliver a live-data feature with less integration and coordination | Useful developer experience, a manageable migration and clear responsibility for operating it |
| An internal platform team serving several product teams | Offer database capabilities that consumer teams can use with less assistance | Provisioning, access, observability, resource and cost attribution, recovery and a support model appropriate to those consumers |

These are different buying situations to investigate. The second route could
have greater expansion potential while requiring more operational maturity.
User, operator, budget holder and approver may be different people in either
route. A platform team can be a customer and distribution partner; its value
does not depend on maintaining many different engines.

The [thinnest viable platform](https://github.com/TeamTopologies/Thinnest-Viable-Platform-examples)
principle suggests supplying only the interfaces and tools needed to help
consumer teams. SixDB could be a component of that internal platform without
requiring the customer to replace the whole platform. This remains compatible
with the database-first direction and does not require the broader Consurgent
compute vision.

## Interactions over time

The three [interaction modes](https://teamtopologies.com/news-blogs-newsletters/2025/2/21/team-topologies-interaction-modes-breaking-through-common-misconceptions)
distinguish temporary joint learning, help that builds another team's
capability, and routine service consumption. For SixDB, early design-partner
work can involve close collaboration; onboarding can involve facilitation;
routine use should become possible through clear interfaces and support.
These are useful distinctions, not a mandatory sequence for every customer.

A pilot may establish technical usefulness while hiding substantial founder
labor. Record which work becomes repeatable, remains a paid operating service,
or still requires bespoke intervention. Recurring managed operations are a
service responsibility, not temporary enablement. Customers should not need
to acquire the vendor's database-internals expertise to use the product.

## Commercial consequences worth testing

Performance headroom might fund stronger boundaries: dedicated resources,
separate deployments or retained copies could still be economical. Maximizing
sharing is not automatically the customer's best outcome. Conversely, a
single engine may offer common skills and tooling even when physical systems
remain separate. Product consolidation and deployment consolidation need not
happen together.

OSS can help inspection and evaluation, but this lens does not determine a
license. Source access and low operational burden are separate properties.
A self-hosted product still needs usable installation, upgrades, recovery and
diagnostics; a managed offering needs a credible service boundary. The value
of commercial features should be tested against work they remove or make safe
for the consuming teams, rather than presumed from an enterprise feature list.

Discovery should follow a recent real change: where did it wait, which teams
participated, who owned the data and incidents, and what would SixDB actually
let the first team stop doing? A pilot can compare time to deliver a useful
change, recurring coordination, operational effort and equivalent-service
cost alongside workload performance. A faster database that leaves these
unchanged may still be valuable, but it has not demonstrated the proposed
organizational simplification.
