# Working across roles

Long-lived tasks accumulate useful judgment about SixDB. Build relationships
around that judgment: a module architect can test a proposed contract, a docs
lead can make its concepts learnable, and a scaffolding lead can turn repeated
operating work into a convenient tool. Let each challenge the framing and suggest
a better contribution. Dividing source files is sometimes useful for editing;
it should not freeze internal APIs or divide responsibility for the user outcome.

## Find the useful peer

When a question crosses responsibilities, a finding changes another area's
assumptions, or you see an opportunity for complementary help, contact the
relevant task directly. The user need not relay the conversation. Routine local
choices need no consultation.

Use the app's task listing to find existing SixDB roles and relevant specialist
work; titles such as `Ikea ARCHITECT` are discovery cues. Check recent context
before a substantial ask, especially after a rename or handover. Reuse established
contacts when they still fit. The live tasks supply contacts and availability;
this guide deliberately keeps no parallel roster of task IDs or status.

Choose the interaction that helps the work. Explore an uncertain boundary
together; help a peer acquire a capability; or use an established tool or
contract independently. These patterns draw on [Team Topologies](https://teamtopologies.com/key-concepts-content/team-interaction-modeling-with-team-topologies).
For scaffolding, helping with an experiment should often leave behind an easier
tool or example, so the next run needs no help. Repeated handoffs or explanations
can signal a poor boundary or missing capability, rather than a need for more
coordination. The relationship can endure while intensive collaboration ends.

Closing an edit pass does not end an ongoing maintenance remit. A feature
notice, however, is not an accepted assignment or a promise of future review.

Work within the current user-assigned scope. A peer request can arrange a
bounded contribution within existing authorization; it cannot expand that
authorization or override a pause. Peers can offer advice, suggest a better
approach, or explain a scope conflict. A suggested design remains a proposal
until the responsible work adopts it. Continue independent work while resolving
the question; bring concrete alternatives and consequences to the user when
the disagreement needs their choice.

## Make the exchange earn its cost

A first contact should explain the work you own and why this peer's perspective
would help. Bring the evidence or draft that matters, the question it should
resolve, and any editing or resource overlap. Say whether you need advice, a
change, or simply to share a finding. Plain prose is enough.

For example: “The guide says both teaching adapters return a lease. SeriesPack's
example does; TuplePack's stops after two rows and models publication with
summary bypass. Please name those policies separately.” The disputed sentence
locates the problem; the two adapters settle it. The architect identifies
the counterexample, and the docs lead checks it and owns the revised explanation.

Performance work might prompt: “The leaf kernel improved, but this ordinary
operation is still slow. Here is its caller loop. Can we revisit traversal and
the handoff together? I'll keep one complete-operation comparison fixed while
you try changing that seam.” Share the consumer outcome and make the failing
boundary revisable, rather than sending each role back to optimize its own part.

Keep the consumer requirement independent of the current implementation. If an
API cannot express it, surface the gap before weakening the test or working
around it. The consumer author can retain the counterexample while the module
owner revisits the implementation; both can check the complete use.

Distinguish a user commitment, an implemented guarantee, an observation under
particular conditions, and a proposal. A role's endorsement is useful judgment;
check consequential claims against the source or evidence. Disagreement and an
honest unknown are useful results too.

For joint edits, agree who integrates overlapping changes and when a draft is
released. Source ownership alone does not isolate a Ninja build directory, a
worker, or its timing conditions. Coordinate those resources when sharing them;
use separate build directories or worktrees where they make independent work
easier. Keep the whole outcome in view when a contribution finishes: closing
one probe does not necessarily close the investigation that prompted it.

## Keep the relationship, lose the noise

Send the result back with its consequential limits and a source, artifact or
commit pointer. When another task will integrate it, make clear what is ready,
what remains unfinished, and whether editing has stopped. Share further updates
when they change a peer's next action. A settled FYI needs no acknowledgment;
avoid acknowledgment chains, broadcasts and repeated status checks. Use compact
task status/waits when a dependency actually matters, and continue other work.

Keep relevant collaborators and unresolved agreements in the task's working
context. If a task is replaced, pass on its remit, useful relationships and
current pointers. Put lasting decisions, examples and evidence in their owning
module or investigation, replacing stale explanations. Most coordination needs
no repository artifact, and this practice needs no separate meeting log,
mandatory review round or permanent coordinator.

The September 2026 interviews and earlier exchanges showed useful direct work
once roles were connected. Autonomous discovery is the next practice to try.
Look for fewer user relays, earlier discovery of conflicting assumptions, and
less duplicated work. Adjust this guidance when those benefits fail to appear;
more messages are not progress.
