# Sociological questions for SixDB

Commercial interpretation, 28 September 2026. These are hypotheses about
adoption, discovery and shared authority, not customer findings or
architectural commitments. The [commercial note](commercial.md) owns the
broader remit and database-first direction.

## Flow, discovery and authority

[Team Topologies](https://teamtopologies.com/key-concepts) focuses attention on
the flow of useful changes, teams' cognitive load and their interactions.
For SixDB, the question is what a team can deliver and operate with less
waiting, learning and coordination. Database speed and systems removed are
inputs to that question.

In [The Republic of Science](https://www.polanyisociety.org/mp-repsc.htm),
Michael Polanyi describes independent inquiry coordinated through published
results, mutual adjustment and overlapping expert judgment. Shared standards
support criticism and originality. Applied here: can participants discover,
inspect and build on each other's work without every useful initiative being
selected centrally? Some connections create opportunities rather than delays.

Elinor Ostrom's [Beyond Markets and States](https://web.pdx.edu/~nwallace/EHP/OstromPolyGov.pdf)
examines interacting centres of decision making and institutions suited to
local conditions. Her resource studies identify recurring roles for user
participation in rule changes, accountable monitoring, conflict resolution
and governance at several scales. Applied here: who can use shared resources,
change the rules, bear the costs and resolve competing claims?

These lenses overlap without being interchangeable. Technical judgment does
not by itself confer authority to assign obligations; agreement on a policy
does not establish an empirical claim. Centres of authority may overlap and
operate at different scales, rather than partitioning neatly into teams or
an organisational tree. None of these lenses supplies a universal recipe.

## A coherent workload within one team's ownership

Analytics and applications entry points could meet buyers where they are, but
these technical categories do not necessarily identify team boundaries. A
team responsible for a business capability might need both; an analytics
product team might own a distinct customer outcome. A useful adoption path
respects current ownership while allowing avoidable handoffs to disappear.
Preserving technology silos indefinitely is not the objective.

A candidate first use case is a product team adding live reporting to an
operational application it already owns. Avoiding another storage system and
recurring work in another team's queue could increase its scope of independent
delivery. Conversely, requiring it to learn transactions, streaming, analytics
and distributed scheduling could increase its burden. The engine's breadth
needs a small, useful surface, sensible defaults and clear responsibilities.

| Initial adopter | Candidate value | What the offer needs to establish |
| --- | --- | --- |
| A product team with authority over a bounded workload | Deliver a live-data feature with less integration and coordination | Useful developer experience, a manageable migration and clear operating responsibilities |
| An internal platform team serving several product teams | Offer database capabilities that consumer teams can use with less assistance | Provisioning, access, observability, resource and cost attribution, recovery and appropriate support |

These are buying situations to investigate. The second could offer greater
expansion potential while requiring more operational maturity. User, operator,
budget holder and approver may differ in either route. A platform team can be
a customer and distribution partner; its value need not depend on maintaining
many engines.

The [thinnest viable platform](https://github.com/TeamTopologies/Thinnest-Viable-Platform-examples)
principle suggests supplying the interfaces and tools needed to help consumer
teams. SixDB could be a component of an internal platform without requiring
its replacement or the broader Consurgent compute vision.

## Preserve agreements and room for discovery

A pipeline can carry ownership agreements as well as bytes: schema evolution,
validation, freshness, access, lineage, retention and support expectations.
Removing copying or orchestration leaves those obligations to handle.
Unrestricted access to another team's mutable internal tables could make
changes harder even when queries become faster.

For example, an orders team could retain its operational schema while a
reporting team consumes a documented, versioned interface. Whether this uses
views, events, derived data or another mechanism is a technical and customer
question. The [Team API concept](https://teamtopologies.com/resources) also
makes responsibilities and interaction expectations explicit; a SQL interface
alone cannot settle who handles an incident or a breaking change.

Participants also need room to try interpretations that nobody has yet agreed
are useful. Gross order value, recognised revenue and an experimental metric
can use related records without being interchangeable. A shared database need
not force one group to own every interpretation. The organisation still
decides which definition applies to a particular decision.

Independent experiments become more useful when others can inspect their
inputs, assumptions, versions and results. Permitted discovery, provenance,
reproducible evaluation and publication of stable interfaces are candidate
capabilities to test against customer needs. Access obligations constrain what
may be made visible.

[Orbital's extension and replay direction](../../orbital/BRIEF.md) is relevant
to execution evidence. Matching results or deterministic replay cannot
establish that an input is true, a metric is useful or an inference is sound.
Software can support scrutiny; domain judgment remains with people.

## Shared resources and decision rights

Data and knowledge can be reused without consuming the original. Compute,
bandwidth, retained storage and operator attention have limited capacity;
access can often be restricted. A database is therefore not automatically a
common-pool resource in Ostrom's technical sense. Identify the actual resource
and institutional problem. Disclosure can also impose costs unrelated to
resource depletion.

Permission to query does not establish participation in the rules governing
queries. Ask who can set or challenge an allocation, change an interface and
accept obligations to consumers. Local discretion can coexist with company-wide
security requirements and agreed ways to resolve conflicts. Distributed
execution alone establishes nothing about that distribution of authority.

Consider a hypothetical organisation sharing an engine:

| Participant | Decision it might retain | Shared obligation to make explicit |
| --- | --- | --- |
| Orders team | Operational schema and application changes | Preserve consumer interfaces or arrange migration |
| Finance team | Approval of recognised-revenue definitions | Identify input versions, assumptions and correction procedures |
| Analytics team | Experimental models and metrics | Respect access, resource budgets and publication expectations |
| Platform team | Operation and allocation of shared capacity | Explain interference, cost attribution and recovery responsibilities |

The actual boundaries need agreement. A long-lived analytical reader might
retain versions that the orders team would otherwise release: a lifetime
mechanism also creates a question about who accepts and pays for the obligation.
Existing [retention questions](dataflow-workloads.md#f18--shared-work-and-demand-dependent-pruning)
and [modeled transaction dependencies](../simulator/experiments/README.md)
show why separate schemas or team labels cannot establish independence.
Promises about interference, recovery scope or independent upgrades would
need appropriate technical evidence.

Resistance to consolidation may protect legitimate responsibilities and local
knowledge. Discover which decision rights or assurances adoption would remove,
and whether those losses are necessary. Separate landing pages cannot resolve
that concern by themselves.

## From joint learning to routine use

The three [interaction modes](https://teamtopologies.com/news-blogs-newsletters/2025/2/21/team-topologies-interaction-modes-breaking-through-common-misconceptions)
distinguish temporary joint learning, help that builds another team's
capability, and routine service consumption. Design-partner work can involve
collaboration; onboarding can involve facilitation; routine use should become
possible through clear interfaces and support. This is not a mandatory sequence.

A pilot may demonstrate technical usefulness while hiding substantial founder
labour. Identify what becomes repeatable, remains a paid operating service or
still needs bespoke intervention. Recurring managed operations are a service
responsibility, not temporary enablement. Customers should not need the
vendor's database-internals expertise to use the product.

## Commercial consequences to investigate

One positioning hypothesis is to help teams share live data and useful work
while retaining clear responsibility for their own changes and decisions.
Start with a bounded workload whose adopter can capture the benefit. Broader
sharing should expose its costs and obligations rather than require acceptance
of an unexplained global optimisation objective.

Logical ownership, resource sharing and physical deployment are separate
choices. Performance headroom might make dedicated resources, separate
deployments or retained copies economical. A common engine can also supply
common skills and tooling across physically separate systems. Product and
deployment consolidation need not happen together.

Credible customer control could include inspectable interfaces, portable data
and definitions, independently maintained extensions and workable ways to
change providers or separate workloads. OSS, practical portability and
participation in project governance are distinct. An open repository can still
leave one vendor controlling essential services or compatibility decisions.
Source access also does not remove the work of installation, upgrades,
recovery and diagnosis.

The business hypothesis is that customers will pay for reliable operation,
integration and support that make cooperation workable. Preserving customer
options may aid adoption while reducing switching barriers. Test commercial
features against work they remove or make safe, rather than assuming value
from an enterprise feature list. These arguments do not select a licence or
imply a foundation or generic governance platform.

The broader Consurgent vision remains optional. Independent participants can
coordinate through contracts, shared services and negotiated rules as well as
prices. A liquid compute market is one possible mechanism; the current intent
remains database first.

## Learning from customers and from each other

Trace a recent real change: where did it wait, which teams participated, who
owned data and incidents, and what would SixDB let the adopter stop doing?
For a disagreement over sharing, identify the parties, resource, contested
decision, existing rule and consequences of changing it. A pilot can compare
delivery time, recurring coordination, operational effort and equivalent-service
cost alongside workload performance. Speed alone does not demonstrate the
proposed organisational simplification.

Our [collaboration guidance](../collaboration.md) already supports direct
consultation, revisable boundaries and findings carried to their owners.
Commercial contributes customer evidence and constraints; technical work
contributes mechanisms and counterexamples. Neither substitutes for the other.
Publish enough context for peers to challenge an inference, while leaving
exploratory questions room to develop before a sales use is apparent.

A company still chooses what to fund and support. These lenses do not imply
that every investigation can continue indefinitely or that professional
consensus settles customer value.
