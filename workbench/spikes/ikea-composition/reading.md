# Reading for Ikea composition

Initial orientation, 2026-09-08. This is a question-driven reading map, not a
completed literature review. Local source and selected reports were inspected;
external entries below were checked at their official documentation or
publication pages. Full paper and implementation reviews remain part of the
spike. Findings from Calico do not establish SixDB interfaces or target-host
performance.

SixDB was read at the current workspace; Calico HEAD was
`ac83c82b9a0c8d76bea92829e471ebc0d98b1978`. Sibling links follow the existing
Workbench convention. Historical references name an explicit Git revision.

## Local questions and starting points

| Question | Sources inspected | Consequence for this spike |
| --- | --- | --- |
| What is already intended in SixDB? | [Conventions](../../design/conventions.md), [tuning](../../design/tuning.md), [Ikea](../../../ikea/README.md), [Engine](../../../engine/README.md), [Loom](../../../loom/README.md), [Calico map](../../notebook/calico.md) | C++ and deliberate shared compilation are established; the stage ABI remains open. ISA legality and tuning policy already have separate build settings. |
| Which questions outlive BytePack's concrete formats? | BytePack [memo and scope audit](../../../../calico/workbench/prototypes/bytepack/MEMO.md), [README](../../../../calico/workbench/prototypes/bytepack/README.md) | Transform/mutation boundaries, cross-scale composition and useful intermediate-state reuse remain relevant. Its codec geometry and benchmark fixtures are point probes, not Ikea design constraints or a coverage checklist for this spike. |
| What is the primary prior for straight-through CPS? | BytePack [protocol](../../../../calico/workbench/prototypes/bytepack/chain.h), [shared bodies/adapters](../../../../calico/workbench/prototypes/bytepack/bench/chain_kernels.h), [CPS report](../../../../calico/workbench/prototypes/bytepack/evidence/cps.md) | Start from the bounded cursor/table protocol and body reuse described below. The report compares protocol packages, including calling convention and mask transport, and does not test decoded-vector handoff. Ordinary reusable stages are a useful control when interpreting its results. |
| What must escape a mutable body? | BytePack memo's mutation section; xmem [staging implementation](../../../../calico/xmem/include/xmem/xmem.h); QHash [integration status](../../../../calico/qhash/README.md) | Coverage can conservatively include unchanged bytes, but cannot miss changes. QHash's missed recycled-bucket writes illustrate incomplete effect reporting; the general obligation is to cover effects through composition, independently of its specific mutation scheme. |
| What does useful planner evidence contain? | Row-signature [design](../row-filter-signatures/design.md) and [findings](../row-filter-signatures/FINDINGS.md) | Certainty, row coordinates, predicate obligations, edition and whole-group coverage matter. Logical read reduction can lose to direct execution; optional progression and bypass must remain expressible. |
| What do mutation consumers actually need? | Aggregate [design](../aggregate-maintenance/design.md) and [conclusions](../aggregate-maintenance/CONCLUSIONS.md); [secondary summaries](../../notebook/secondary-summaries.md) | Byte coverage is not an aggregate delta. Beforeimages, dirty state, exact versus conservative extrema, visibility, and numeric regrouping matter. Dirtiness indexing is conditional value, not a compulsory hook for every body. |
| Which old module seams are worth challenging? | Calico [Frame](../../../../calico/frame/README.md), [QHash](../../../../calico/qhash/README.md), [Keyset](../../../../calico/keyset/README.md), [kmath](../../../../calico/kmath/README.md), [Engine](../../../../calico/engine/README.md), [tour](../../../../calico/design/TOUR.md) | Layouts, masks, transforms, standalone structures, numeric semantics and Engine binding all provide examples. Calico's fixed geometry and module ownership are historical choices, not defaults to inherit. |
| How does suspension differ from continuation? | Calico Loom [README](../../../../calico/loom/README.md) and [AMAC contract](../../../../calico/loom/spec/AMAC.md) | The ring has explicit live-state lifetime and park/resume behaviour. Hot/streamed paths can avoid parks. Its contracts also distinguish DRAM interleaving from longer-lived residency/I/O work. |

The BytePack CPS report is exploratory M1 Pro / OrbStack VM evidence with
Clang 20.1.8, not a Zen5 conclusion. The inspected SixDB summary studies are
also local ARM VM results. Their most useful input here is the failed
assumptions and comparison structure, rather than headline timings.

Use this wider Calico/SixDB map when a mock-up needs another example or exposes
a seam worth investigating. Follow the relevant caller and implementation
together. The map is not a programme to recover an entire prior architecture.

## Primary CPS prior: BytePack

Ashton's clarification identifies the recent BytePack work as the useful
starting point for straight-through pipelines. The proposed scheme is:

- Carry a cursor through the calling context and advance with `musttail`.
- Bound a pipeline to `1..2^k-1` stage function pointers plus a completion
  continuation (the return address in the pipeline).
- Use 64-byte alignment. Place completion immediately after the last stage;
  for shorter pipelines, duplicate it into the final reserved slot.
- Recover that final slot from the cursor with pointer arithmetic on early
  exit, avoiding a separately carried completion pointer.

The current [chain.h](../../../../calico/workbench/prototypes/bytepack/chain.h)
implements the concrete `k=3` case: eight 8-byte slots in a 64-byte-aligned
table, supporting one through seven stages. Unused slots contain completion.
A running nonterminal stage receives a cursor to the next slot, within the
same table. For that geometry, the early-exit slot is:

```text
completion_slot = (address(cursor) & ~63) + 56
```

This arithmetic depends on the table geometry and cursor invariant. If `k`
varies, table size, alignment and base recovery must remain consistent; the
64-byte mask alone cannot recover the base of a larger table. This is a
calling-protocol candidate, independent of any codec or row grain.

Read [the shared adapters](../../../../calico/workbench/prototypes/bytepack/bench/chain_kernels.h)
for how inline and continuation forms reuse body code, then
[the evidence](../../../../calico/workbench/prototypes/bytepack/evidence/cps.md)
for what was actually exercised. The design questions are the authoring
surface, carried values and bindings, compatible signatures, wrapper/body
boundaries, early completion and plan lifetime. Extend the example only where
one of those questions requires it.

## ChainVM: calling conventions and macro ergonomics

Ashton's assessment is that ChainVM's complex control flow looked promising
in isolation but fell short whenever adoption of those aspects was attempted.
Straight-through pipelines remain promising. For this spike, the useful
ChainVM reading is therefore narrow: calling conventions, flattened arguments,
and the macros that keep signatures and forwarding consistent. Its traversal,
worklists, ring machinery and general control-flow model are not a reading
track for Ikea composition.

The last pre-deletion tree is
`030e39ae1973aecb0d2a2d19fa61336148b777c4` (`929baa9b^`). The relevant parts of
`chainvm/chainvm.h` include convention selection (`preserve_none` / `regcall`)
and field-list macros generating declarations, flattened parameters and
forwarding arguments. Inspect these for reusable authoring ideas, without
adopting its broad register context or whole VM. Recover the header with:

```sh
git -C ../calico show 030e39ae1973aecb0d2a2d19fa61336148b777c4:chainvm/chainvm.h
```

The command assumes the SixDB repository root. The initial orientation also
inspected parts of the old specification and Loom extraction survey; that
broader reading is not the proposed continuation. Loom integration has its
own current sources in the table above.

## External reading, tied to decisions

### PyTorch: composition and transformation surfaces

Added following Ashton's suggestion, 2026-09-08. The following is a targeted
documentation reading, not an implementation audit or a historical comparison
of PyTorch and TensorFlow adoption. The rolling documentation should be pinned
before reproducing an API or examining implementation details.

- The [original PyTorch paper](https://arxiv.org/abs/1912.01703) describes
  imperative authoring, ordinary Python composition and user control as design
  goals alongside performance. For Ikea, inspect how extending ordinary code
  can remain accessible before introducing more declarative machinery.
- The [dispatcher tutorial](https://docs.pytorch.org/tutorials/advanced/dispatcher.html)
  separates operator schemas from implementations and illustrates layering
  autograd/backend handling through dispatch and redispatch. The page is
  explicitly deprecated; use it as a design explanation, and use current
  custom-operator guides for the authoring API. The useful Ikea question is
  how wrappers compose services while keeping native bodies local. PyTorch's
  dispatch frequency, keys and tensor ABI are not proposed Ikea defaults.
- The [custom-operator overview](https://docs.pytorch.org/tutorials/advanced/custom_ops_landing_page.html)
  recommends ordinary functions for compositions of existing operators;
  registration makes otherwise external kernels participate in subsystems.
  Its [Python authoring guide](https://docs.pytorch.org/tutorials/advanced/python_custom_ops.html)
  requires explicit schema/mutation/aliasing contracts and describes fake
  kernels that propagate output metadata without reading data. These suggest
  separating ordinary composition, opaque implementations and metadata-level
  reasoning. Ikea would also need data-dependent analysis/probes; metadata
  alone does not establish compression suitability or selectivity.
- The [FX fusion example](https://docs.pytorch.org/tutorials/intermediate/fx_conv_bn_fuser.html)
  traces operations through nested modules into a transformable graph. The
  [ATen graph-transformation guide](https://docs.pytorch.org/docs/main/user_guide/torch_compiler/torch.compiler_transformations.html)
  provides node/subgraph rewrites and capability-based partitioning. These
  are useful examples of a callable surface coexisting with access to its
  internal composition. Graph editing facilities do not prove an arbitrary
  replacement semantically equivalent.
- The [export tutorial](https://docs.pytorch.org/tutorials/intermediate/torch_export_tutorial.html)
  shows controllable decompositions and functionalisation, including mutation
  represented in graph outputs. This motivates testing whether an Ikea
  operation can offer an inspectable composition alongside specialised native
  implementations, and how effect information survives rewriting. It does
  not settle byte-span reporting, MVCC or publication in SixDB.

**Proposed lesson to test:** keep a convenient callable contract while letting
analysis inspect supported compositions, substitute parts, and bind specialised
implementations. An opaque extension is useful but creates a limit on what an
optimiser can rewrite unless it also supplies a decomposition or rewrite rules.
Do not require an author to describe the same semantics independently at every
layer. These sources do not establish the proposed Engine model of persistent
equivalence graphs, continual improvement or adaptation during a single query;
that remains its own design question. They also provide no evidence about
Ikea's native register handoff or BytePack-style CPS calling convention.

### Other targeted sources

- **Highway:** [official quick reference](https://google.github.io/highway/en/master/quick_reference.html),
  particularly dispatch, namespaces and headers. It separates target
  implementations and permits custom target selection; it also recommends
  keeping vector arguments within inlined functions because ABIs differ.
  Investigate its organisation, dispatch isolation and target testing. The
  proposed inference for Ikea is to borrow these mechanisms selectively while
  retaining native bodies and explicitly investigating native handoff ABIs.
  A pointer-based outer dispatch boundary does not decide the representation
  between every pair of inner stages. Pin a source revision before detailed
  implementation comparison; the linked documentation is rolling.
- **Wagner et al., Incremental Fusion (ICDE 2024):**
  [author institution publication and manuscript](https://ir.cwi.nl/pub/34359).
  Its shared building blocks for fused code generation and vectorized
  interpretation make it directly relevant to reusing algorithmic definitions
  across execution strategies. Read its primitive boundaries and generated
  interfaces. This is not evidence for a CPS ABI, or a reason to require JIT
  compilation in Ikea.
- **Koçberber, Falsafi and Grot, Asynchronous Memory Access Chaining:**
  [author institution publication](https://www.research.ed.ac.uk/en/publications/asynchronous-memory-access-chaining/),
  [PVLDB paper](https://www.vldb.org/pvldb/vol9/p252-kocberber.pdf).
  Read when the Loom seam needs an example of independently progressing
  irregular accesses and retained context. Scheduling suspension has different
  lifetime needs from a hot continuation hop.
- **Lang et al., Data Blocks:**
  [author institution publication and manuscript](https://ir.cwi.nl/pub/24382).
  A compressed-storage design serving both point transactions and analytical
  scans, combining vectorization and compilation. Read for point/scan
  interfaces and compression boundaries. Its use of “Data Block” does not
  establish this spike's term “block”.
- **Clang's calling and tail-call rules:**
  [attribute reference](https://clang.llvm.org/docs/AttributeReference.html#musttail).
  Use as the starting language/compiler contract, then verify against SixDB's
  pinned Clang 21.1.8. Rolling documentation and historical ChainVM macros do
  not establish that a proposed signature, lifetime or native-vector handoff
  works on that compiler and target.

Broaden the literature when a concrete sketch exposes a missing idea:
compressed-domain execution, transform composition, partial evaluation,
behavioural subtyping, and adaptive predicate evidence are likely directions.
For each useful source, retain what it contributes, what assumptions Ikea
does not share, and a mock-up change or counterexample it motivates.
