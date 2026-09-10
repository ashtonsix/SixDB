# Ikea composition and authoring ergonomics

Opened 2026-09-08 from Ashton's design brief. This page retains the investigation
charter and review approach. Start with the [consolidated working design](synthesis-1.md)
for the current authoring model, provisional seams, evidence and open choices.
The [second sketches](sketches-2.md) and [first sketches](sketches.md) retain
the earlier alternatives; they are not competing current baselines.
[Reading](reading.md) records the orientation and questions for investigation.

**Implementation direction, 2026-09-10:** Ashton closed the
[heterogeneous exercise](../ikea-heterogeneous/closing.md), judging the basic
data-structure probes sufficient for Ikea's approach to take concrete form.
Carry shared operation authoring, explicit native bodies, independently admitted
and bound inputs, and curated compilation regions into implementation. The
closing assessment owns the demonstrated obligations and measured tradeoffs;
unresolved integration mechanisms need not delay the basic parts.

**Latest investigation, 2026-09-10:** [operation granularity and curated
composition](operation-granularity.md) compares compilation boundaries, unequal
native grains, multiple sources and block obligations across the integer,
bitset, row-filter, aggregate and Xmem case studies. It investigates Ashton's
`A × B + C × D` direction while leaving the concrete interface open.
Its [heterogeneous follow-up](operation-granularity.md#what-the-heterogeneous-probe-now-adds)
now combines cursor authoring with selected region cuts, and records measured
retained-state costs and a parent-locality counterexample. General cursor and
substitution interfaces remain open.

**Earlier direction, 2026-09-09.** Recordable composition functions, optional
named components and explicit implementation/rewrite knowledge form the working
authoring model. Carrier allocation, deep child/dependency representation,
progressive observations and integration boundaries remain open. Ashton selected
the [packed-integer probe](synthesis-1.md#next-probe-packed-integers), with
PFoR-like reconstruction and bitset metadata as intended customers and further
progressive-filtering/nested-substitution work in view. The existing bitset
probe supplies evidence, not a mandatory framework for that work.

**Further evidence, 2026-09-09:** the [integer composition exercise](../ikea-integers/composition/README.md)
uses one decoded value in two consumers and varies parent placement separately
from the tail format. Its bounded result extends the evidence available after
the first probe; the study records its limits and subsequent measurements.

**Review remit, corrected 2026-09-09.** This task advises on the composition
principles and authoring designs agreed with Ashton, and actively uses concrete
block/kernel work to challenge them. General correctness reviews, code cleanup,
or implementation approval requested by another task are outside this remit;
decline or reorient those requests unless Ashton asks for them. Performance and
ABI evidence belong here when they test the costs and viability of a composition
seam. The [first probe review](probe-review.md) records how the collaboration
failed and the concrete design questions carried into the restart.

**How can an engineer write and optimise one kernel with a small amount of
local context, while that kernel participates in varied layouts, transforms,
execution strategies, and database integration?**

Ikea will hold the vast majority of SixDB's data containers, codecs, and
kernels. Engine is its primary direct customer and owns composition schemas
and planning. Loom owns partitions, buffer acquisition, micro-task scheduling,
and hardware-prefetch modelling. The investigation must also accommodate
blocks reused in standalone data structures.

Concrete codecs, block sizes, and container schemes in the brief illustrate
composition questions. They are replaceable point probes in the design space;
they do not determine Ikea's architecture, preferred representations, or the
spike's coverage. Choose an example because it exposes a particular boundary,
and vary it to check whether the proposed interface generalises.

Ashton's clarification after the first sketches distinguishes rich schema and
plan analysis from a small execution surface. Engine may replace deeply nested
data components and rewire pipelines while keeping the container's contract
stable. Its proposed incremental compilation model retains plans and
equivalence graphs long-term, improving them through accumulated evidence and,
for long queries, periodic probes during execution. Complex analysis is not
part of every call. A bound container call should perform its work and the
integrity duties assigned to it, leaving explicit obligations with its caller.
The [sketch discussion](sketches.md#clarification-analysis-and-execution-surfaces)
develops the consequences without selecting a new interface.

Segments in the same collection can have different physical representations.
This can adapt to data/access patterns or persist through lazy migration; cold
segments may never be prioritised for a newer format. Engine must distinguish
each segment's actual composition from a preferred replacement and from the
execution plan used to access it. Mixed representations are a normal operating
condition, not a temporary exception requiring convergence.

## Starting commitments and hypotheses

The brief establishes:

- Physical composition and operation composition are many-to-many. Reusing
  a block does not fix its enclosing container, physical arrangement, or
  execution grain. The same logical work may need different implementations
  in different contexts.
- ISA and microarchitectural differences remain explicit in inner bodies.
  Maintaining a small number of specialised bodies per function is welcome.
- Manual fusion serves the most critical compositions; inlining prioritises
  source maintenance; CPS serves the large majority where build time and
  binary size matter. These are implementations of the same logical work.
  Inline and continuation forms must reuse significant algorithmic code.
- Generic/erased interfaces must support sound substitution without imposing
  avoidable memory round trips on hot intermediate values.
- Kernels consume prefilter masks and exploit them to omit work. Progressive
  filtering stages must be available to Engine for adaptive planning.
- Physical mutation coverage, summary maintenance, Loom integration, format
  description, and planner introspection are part of the design exercise.
- Engine needs deep inspection and substitution during schema/plan work,
  alongside cheap repeated execution. Rich composition interfaces and a small
  container call surface must be considered together.
- Segments in one collection may retain different representations indefinitely.
  Planning, binding and substitutions must use the applicable actual composition;
  a collection-wide preferred format does not describe every segment's bytes.

Five organising principles to challenge in the mock-ups:

1. **Name logical work independently of its execution mechanism.** Identify
   the contract first; bind a specialised implementation and calling protocol.
2. **Separate physical layout, logical meaning, and execution grain.** A
   storage block, mask domain, SIMD group, and suspension unit need not coincide.
3. **Keep the kernel author's obligations local and explicit.** A body needs
   its operands, validity rules, allowed effects, and result contract. Narrow
   adapters/drivers supply surrounding services; mutable bodies still declare
   the effects their callers must account for.
4. **Make boundary costs and lifetime changes visible.** Erasure, packing,
   materialisation, pinning, and suspension have owners and costs. A register
   value that crosses an immediate call differs from state surviving a park.
5. **Expose evidence with its meaning and coverage.** Certainty, granularity,
   row coordinates, predicate identity, and validity cannot disappear when
   evidence crosses an interface. Estimates and sound pruning proofs differ.

These are working priors, not an adopted architecture. In particular, a
body/shell split is a hypothesis to exercise, not permission to hide arbitrary
complexity in one universal context object.

## Organise around composition questions

Start with the relationships and authoring obligations an interface must
express. Use small worked examples to make alternative answers inspectable.
When comparing alternatives, hold the useful work and observable contract
constant; when challenging generality, change the example independently of
the interface. Read prior art against the question that prompted the sketch.

| Design question | What a mock-up should expose |
| --- | --- |
| How does a physical primitive participate in several compositions? | Which properties belong to the block, its placement/view, and its enclosing contract; how to add an implementation for another context without changing the primitive's identity. |
| How does algorithmic composition preserve useful state? | Inputs, intermediate representations, transform dependencies, masks and omitted work; who owns traversal and reuse across successive operations. |
| What makes implementations substitutable? | Semantic identity, applicability, generic/erased binding, native carriers and explicit conversions; shared body code across manual fusion, inlining and straight-through CPS. |
| How much surrounding machinery must an author understand? | The body and the code needed to use it; where buffer acquisition, effect reporting, summary obligations and scheduling enter, and how an author finds or extends those pieces. |
| What can Engine discover and decide? | Available operations, progressive evidence and remaining obligations, legal compositions, format descriptions, and the separation between schema, binding and execution. |
| Where does the proposed vocabulary stop working? | Row/column layouts, fixed/variable-width data, and record storage versus standalone data structures; accidental assumptions about coordinates, ownership or granularity. |

Possible probes include producing an intermediate and consuming it twice,
substituting a context-specific implementation, inserting a transform with an
auxiliary input, or reporting a mutation to an external consumer. Give each
sketch just enough physical detail to answer its question. A filter followed
by a consumer can expose mask and state handoff without choosing a particular
codec; a mutation can expose effect ownership without choosing an update
scheme. Add awkward cases when they challenge an assumption in the sketch.

## Competing mock-ups and review rounds

**Opening sketches: competing answers to boundary questions.** Produce several
small alternatives, including the author-facing code. Possible responsibility
arrangements to contrast include:

- **Container-led:** a bound container exposes operations and optional
  refinement steps; its implementation owns layout traversal and kernel
  selection. Test whether consumers can compose and retain useful state.
- **View/function-led:** containers yield validated views; free functions
  operate on them; a driver owns traversal, effects, and composition. Test
  whether useful locality demands too much layout knowledge from the caller.
- **Recipe/descriptor-led:** Ikea describes available operations, ports, and
  constraints; Engine assembles and binds recipes to reusable implementations.
  Test whether reflection and authoring duplicate semantic declarations or
  pull planner policy into Ikea.

These are sources of useful contrasts, not three whole architectures that
must compete through a fixed scenario matrix. A pair of local sketches may
settle one responsibility while leaving others open. Preserve the three
execution techniques in the design space: container methods do not imply
virtual dispatch, free functions do not imply inlining, and descriptors do
not imply CPS.

Each sketch shows the call site, body, relevant adapter/driver, and the
declarations an author must supply. Include the success path and one awkward
path. Name the current owner of intermediate values and every conversion or
allocation at a seam. Pseudocode can omit bit tricks; it cannot omit the
control flow or lifetime mechanism on which composition depends.

**Review 1: attack boundaries and authoring burden.** Review from kernel
author, container author, Engine caller, Loom/MVCC integrator, and compiler
perspectives. Objections should supply a concrete failing call, legal
substitution that breaks, hidden dependency, or edit that spreads too widely.
Record the strongest defence before revising or dropping a candidate. Review
the simple alternatives with the same care as the most elaborate candidate.

**Further sketches: vary the assumptions that matter.** Revise broken seams,
then try surviving proposals with a different physical composition, kind of
data, or customer. The changed example should reveal whether the interface
depended on an incidental property of the first. Let those results determine
the next mock-ups and challenge the emerging vocabulary and information
architecture. Retain a useful minority design where its operating region is
different; a universal winner is not required.

**Review 2: trace substitution and costs.** For a representative useful
pipeline, spell out manual fusion, inlining, and CPS, identifying the shared
body code. Change the target and then the layout context independently.
Trace live values across immediate handoffs. Where a sketch needs Loom
integration, examine suspension separately. Keep ordinary indirect reusable
calls as an explanatory control for the CPS protocol.
Look at masks alone and at a decoded native value consumed immediately by
the next stage; the former cannot establish the latter's cost.

Use BytePack's recent straight-through CPS work as the primary protocol prior:
a cursor carried through the calling context, bounded function-pointer tables,
`musttail` hops, and an aligned completion slot recoverable for early exit.
The [reading note](reading.md#primary-cps-prior-bytepack) preserves the scheme.
Keep Loom suspension as its own integration question. ChainVM's complex control
flow fell short in attempted adoption despite promising isolated results;
mine its calling conventions and macros selectively.

Small compiler probes can start as soon as a claim depends on ABI behaviour.
Use SixDB's pinned compiler, separate TUs and genuinely indirect calls.
Inspect generated code for helper outlining, argument spills, tail-call
legality, and store/reload handoffs. Probe x86 first for the primary target,
then another supported ISA to expose accidental assumptions. Cross-compilation
answers code-generation questions; runtime claims need execution on the target.
Measure code/build growth over reachable recipes if sharing is disputed.
Do not turn this into a full codec or benchmark campaign.

**Review 3: perform the how-to guides.** Try adding a block, a kernel variant,
a transform, and a container composition using the draft guides. Also replace
an inline recipe with CPS and add a manual fusion without changing its logical
contract. Examine what an engineer must learn, declarations duplicated,
unrelated files edited, diagnostics for a bad binding, and new test obligations.
Revise the guide and interface together.

Comparisons remain qualitative: explain local reasoning, discoverability,
semantic clarity, maintenance cost, and plausible runtime/code-size tradeoffs.
Code snippets and observed failures are evidence; one weighted score is not a
substitute. These are proposed rounds, not required review ceremonies, and no
reviews are claimed complete by this charter.

## Working vocabulary and information architecture

Use the brief's data terms provisionally, without turning them into a mandatory
ownership hierarchy:

| Term | Working meaning |
| --- | --- |
| Block | Composable primitive with a defined physical layout; often tileable or hierarchically composable. It can occur in several contexts. |
| Plane | Repeating pattern of blocks, uniform or ragged. Logical repetition and physical contiguity are separate properties. |
| Container | Contract exposing operations and getters/setters, with multiple possible physical realisations. An object-like interface need not require inheritance. |
| Segment | Engine data-structure node, typically covering `2^16` key positions, bundling the containers constituting records at that depth. |
| Partition | Loom-owned storage unit containing contiguous bytes from multiple blocks. Relationships to blocks, planes, and segments must be described explicitly. |
| Record / tuple / struct | Related logical/physical groupings whose distinctions need row/column and variable-width examples before names are fixed. |

For code, try **operation** for requested work; **kernel** for a named
executable semantic contract; **body/implementation** for the specialised
algorithm; **stage** for an occurrence of work in a composition;
**recipe/pipeline** for composed work; **binding** for validated operands and
implementation choice; **adapter** for a specific conversion/protocol seam;
and **driver** for traversal, service integration, and execution lifecycle.
A stage, a function call, a progressive observation point, and a scheduler
suspension are separate choices. The mock-ups should establish which of these
terms earn their place.

The eventual common Ikea map should let readers find things by **physical
format**, **operation**, and **authoring task**. Candidate groupings are block
formats/views, codecs/transforms, container contracts/realisations, kernel
families and target implementations, composition/binding, and integration
adapters. Put specialised kernels beside the family whose contracts they
implement, with cross-links for other layout contexts. Try an actual proposed
file/namespace map against each guide before adopting it. Keep the competing
sketches in this spike; production directory moves can follow later evidence.

## Seams the sketches must make concrete

| Seam | Provisional contract questions / obligations |
| --- | --- |
| Layout ↔ view ↔ body | Format identity and version, bit width, bounds/alignment, readable padding, aliasing, byte order, lane/row mapping, tails, and lifetime. A grouped fast path cannot silently strengthen an erased interface's preconditions. |
| Operation ↔ implementation | Domains, exact/approximate semantics, NULL/NaN/overflow/determinism, mask behaviour, effects, progress and failures. Logical identity survives a change in fusion strategy; implementation applicability and costs are additional facts. |
| Typed ↔ erased handoff | Separate cold descriptors/bindings from hot carried values. Compare narrow ABI families and explicit bridges with materialised views. Selection validates representation, target and calling convention; incompatible function-pointer casts do not provide substitution. No universal register context is presumed. |
| Kernel ↔ transform/context | Required auxiliary containers and editions, input/output representation, ordering/equality properties, restart/random-access support, mask mapping and dependency closure. A predictor may need preceding or otherwise inactive values. Unknown properties forbid a shortcut, while an exact reconstruction path remains available. |
| Evidence ↔ Engine | Predicate identity, certainly-true/possibly-true or equivalent evidence, logical coordinates, covered grain, edition/visibility, unresolved obligations, and continuation statistics. A coarse positive bit need not certify every row. Absence of knowledge differs from SQL NULL. |
| Mutation ↔ MVCC/summary consumers | Reports cover every modified byte, including relocation, patch metadata and secondary writes. Distinguish exact differences from conservative touched spans; define precision explicitly. Logical deltas/invalidations are a separate output. State ownership, capacity failure, completion, and publication order; post-write reporting must still precede the relevant publication boundary. |
| Driver ↔ Loom | Buffer requirements and leases, ownership through cancellation/completion, refusal, prefetch intent/access shape, and safe resume state. Immediate CPS transfer need not queue work or spill everything. A park must preserve all necessary live state; prefetch is a hint, not evidence of readiness. |
| Formats/capabilities ↔ Engine schema/plans | Ikea describes its physical formats, operations and legal compositions; Engine owns composition schemas and planning choices. Separate persistent format/schema identity from process-local bound pointers and native carriers. Specify enough version/parameter/dependency information to reconstruct a composition. |

For each adopted provisional seam, write a small signature, its laws, the
owner on each side, one caller, and one refused case. Avoid an all-purpose
`Context&` whose correctness depends on undocumented fields.

The substitution exercise must preserve observable results, permitted
accesses/effects, ordering and error/progress semantics. Different resource
requirements must be admitted or adapted explicitly. Semantic substitutability
does not guarantee an identical ABI or cost. A richer evidence result can
refine an earlier one only under matching predicates, coordinates and validity.
Proof of impossibility, proof of truth, and an estimate of likelihood are
different contracts even if each is stored in one bit or word.

## Intended outputs and useful stopping point

Keep the final synthesis small: a short set of organising principles; one
common terminology and file/namespace map; provisional seam contracts with
worked calls; selected competing sketches and the objections that changed
them; and four or five guides covering:

- creating a block/format and making it usable in another context;
- adding a kernel implementation for an existing operation;
- adding a transform, including a cross-container dependency;
- creating a container composition and exposing it to Engine;
- composing a pipeline, changing its execution strategy, and integrating
  mutation or suspension where needed.

The spike has a useful design result when those authoring tasks can be walked
through without inventing missing interfaces, the important substitutions
survive the counterexamples, and ABI-dependent claims have bounded compiler
evidence or explicit unresolved alternatives. This does not require settling
every codec, a universal stage ABI, final persisted schemas, Loom policy, or
production implementations. Remaining questions should name the concrete
example or measurement that could distinguish the alternatives.

**Current discussion:** use the [working design](synthesis-1.md) and its
[packed-integer connection](synthesis-1.md#next-probe-packed-integers) to guide
composition review. Keep the [carrier/inline-region alternatives](value-reuse-sketches.md)
open as the concrete customer demands become clearer. PyTorch's composition and transformation interfaces
inform [the targeted reading](reading.md#pytorch-composition-and-transformation-surfaces).
Proceed through discussion-sized sketches; this charter is not authorization
to carry out the entire spike unguided.
