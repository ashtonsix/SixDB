# Workload and prior-art sources

2026-09-26. Source notes for [the workload exploration](WORKLOADS.md). The local
survey below is bounded; it does not claim to reread every source behind the
earlier 223-entry dissemination catalog. Prior systems supply alternatives and
counterexamples, not inherited SixDB contracts or performance predictions.

## Local material inspected

- Current [project](../../../README.md), [Workbench](../../README.md),
  [Engine](../../../engine/README.md), [Loom](../../../loom/README.md) and
  [Orbital](../../../orbital/README.md) entry guides establish scope.
- [Notebook ideas](../../notebook/ideas.md) and the full
  [Loom objectives/architecture note](../../notebook/loom-objectives-and-architecture.md)
  supply joint-resource, grain, placement and retention questions.
- [Dissemination catalog](../orbital-dissemination/CATALOG.md),
  [recommendation](../orbital-dissemination/RECOMMENDATION.md),
  [workload survey](../orbital-dissemination/SOURCE-SURVEY-WORKLOADS.md) and
  [archive survey](../orbital-dissemination/SOURCE-SURVEY-ARCHIVE.md) locate
  historical ideas and existing evidence. The workload survey was read by
  sections; underlying local implementation/benchmark sources were not rerun.
  [Reads/extensions](../orbital-dissemination/READS-AND-EXTENSIONS.md) was inspected
  through its coverage, replication, reduction and backend-ring sections.
- The complete [worked ELT histories](../orbital-scenarios/ELT-WORKED.md),
  [allocation pipelining](../orbital-scenarios/PIPELINING.md) and
  [extension composition](../orbital-scenarios/reconsideration/COMPOSITION.md)
  supply the current semantic distinctions. The read/write catalog and locality
  headings and their earlier survey mappings were checked; no second complete
  SQL catalog is claimed. Orbital LEAD owns current brief interpretation.
- [Calico reference map](../../notebook/calico.md),
  [Calico Loom guide](../../../../calico/loom/README.md), the full historical
  [join/execution survey](../../../../calico/design/prior-art/engine2-survey/joins-execution.md),
  and the archived [symbolic Boolean note](../../../../consurgent/archive/notes-v0/LOGIC_symetric_binary.md)
  were read directly. ChainVM and other archive ideas use the previous survey's
  inspected inventory. Strong historical adoption/superiority language is not
  accepted as evidence that a mechanism dominates in this composition.

## Primary literature

The selected sources were located/opened on 2026-09-26. Entries identify the
specific idea used; source abstracts/selected sections are sufficient for these
limited imports. No end-to-end evaluation was reproduced. The local examples,
proposed comparisons and SixDB implications in WORKLOADS.md are our analysis.

| Source | Useful import and boundary |
| --- | --- |
| [Graefe, Volcano, 1994](https://ieeexplore.ieee.org/document/273032/) | Exchange isolates parallel communication from ordinary operators; choose-plan delays selected choices. This does not prescribe a universal iterator API or cover dynamic cycles and external effects. Publisher abstract inspected. |
| [DeWitt et al., Practical Skew Handling in Parallel Joins, 1992](https://www.vldb.org/conf/1992/P027.PDF) | Different skew regimes can favor different join algorithms; sampling/preprocessing itself costs work. The historical Gamma results are not a SixDB crossover or a current hardware claim. Abstract/introduction inspected. |
| [Wang, Willsey and Suciu, Free Join, 2023](https://arxiv.org/html/2301.10841v1) | Binary and worst-case-optimal joins need not be separate all-or-nothing choices; lazy indexing and plan choices matter. This supplies a local join design space, not a WAN partitioning/recovery protocol. Abstract/introduction and plan/data-structure discussion inspected. |
| [Fagin, Lotem and Naor, Optimal Aggregation Algorithms for Middleware](https://www.wisdom.weizmann.ac.il/~naor/PAPERS/middle_agg.pdf) | Top-k thresholding depends on monotone score combination and the available sorted/random accesses. Import those conditions, not a claim that arbitrary distributed top-k can stop after k replies. Abstract/access-model formulation inspected. |
| [Murray et al., Naiad, SOSP 2013](https://www.microsoft.com/en-us/research/wp-content/uploads/2013/11/naiad_sosp2013.pdf) | Cyclic computation with structured logical time and notifications distinguishes partial work from closed input/iteration coverage. Sections 2–3.2 inspected here; prior dissemination study considered progress aggregation. Timely epochs are not database commits. The paper targets aggregate-RAM working sets; it does not settle finite-resource/spill policy here. |
| [McSherry et al., Differential Dataflow, CIDR 2013](https://www.cidrdb.org/cidr2013/Papers/CIDR13_Paper111.pdf) | Incremental computation can reuse work across nested iteration and changing input. The abstract/introduction motivates F07/F09; no claim that every small input change yields small work or state follows. |
| [Budiu et al., DBSP, PVLDB 2023](https://www.vldb.org/pvldb/vol16/p1601-budiu.pdf) | A formal incremental formulation covers rich relational and recursive languages. The abstract/introduction's difference/group assumption matters; algebraic expressiveness is not automatic support for arbitrary stateful extension code or irreversible actions. Search-provided paper text inspected; a later full open failed. |
| [Malewicz et al., Pregel, SIGMOD 2010](https://research.google/pubs/pregel-a-system-for-large-scale-graph-processing/), [Google's model description](https://www.research.google/blog/large-scale-graph-computing-at-google/) | Vertex-local work and round-separated messages are a useful simple feedback baseline. Publisher entry and authors' description inspected, not the complete evaluation; no inherited graph scale or performance claims. |
| [Akidau et al., The Dataflow Model, PVLDB 2015](https://research.google.com/pubs/archive/43864.pdf) | Unbounded, out-of-order input requires an explicit choice about windows, output timing and revisions. Abstract inspected; application source completeness is not inferred from network inactivity. |
| [Carbone et al., Lightweight Asynchronous Snapshots for Distributed Dataflows, 2015](https://arxiv.org/abs/1506.08603) | A recovery snapshot can depend on graph structure and in-flight records; their ABS differentiates acyclic from cyclic flows. Abstract inspected. This is a checkpoint alternative for investigation, not proof of database visibility or external-sink exactly-once effects. |
| [Moritz et al., Ray, OSDI 2018](https://www.usenix.org/conference/osdi18/presentation/moritz) | Dynamic task parallelism and stateful actors can serve one application. Publisher abstract inspected; no performance number, lineage guarantee or current Ray API is adopted. |
| [Bernstein et al., Orleans, MSR-TR-2014-41](https://www.microsoft.com/en-us/research/publication/orleans-distributed-virtual-actors-for-programmability-and-scalability/?lang=ja), [project description](https://www.microsoft.com/en-us/research/project/orleans-virtual-actors/) | Stateful entity placement is an alternative to repeatedly reconstructing a stateless middle tier. Abstract and project model inspected. Actor identity/location indirection does not by itself resolve multi-entity invariants or external action ambiguity. |
| [Abadi et al., TensorFlow, OSDI 2016](https://research.google/pubs/tensorflow-a-system-for-large-scale-machine-learning/) | Dataflow placement can span different devices and stateful computations. Publisher model description inspected; motivates a workload dimension only, with no current API or performance recommendation. |

The selection deliberately samples competing execution models: exchanges,
multiway joins, bounded rank refinement, barrier rounds, timestamped feedback,
incremental state, dynamic tasks and stateful services. It is not an exhaustive
literature review, and none requires exposing its particular operators or
progress protocol through Orbital.

## Spark-like application angle

Ashton explicitly raised this scope while the exploration was running. We read
the following **versioned Spark 4.0.2 documentation**, avoiding a moving `latest`
link; this is not a claim that 4.0.2 is the current release. The intent is to
understand the application opportunity and design alternatives, not add Spark
compatibility or select its architecture.

| Official source | Inspected content and import |
| --- | --- |
| [RDD Programming Guide](https://spark.apache.org/docs/4.0.2/rdd-programming-guide.html) | Overview, operations, closure scope, shuffle, persistence and shared variables. Lazy transformations/actions, partition functions, reusable cached data and recovery by recomputation motivate a substantial application surface. Closure copies are not shared mutable process state; shuffle and long-lived intermediates consume memory/disk. |
| [SQL, DataFrames and Datasets](https://spark.apache.org/docs/4.0.2/sql-programming-guide.html) | Full short guide. Structured data/computation exposes more to optimization than opaque functions; this is a useful distinction when combining Engine plans and extension code. |
| [Structured Streaming getting started](https://spark.apache.org/docs/4.0.2/streaming/getting-started.html) | Programming model and fault-tolerance sections. Streaming relates to a changing result with defined output behavior. Replayable source positions and sink behavior participate in the end-to-end guarantee. |
| [Structured Streaming APIs](https://spark.apache.org/docs/4.0.2/streaming/apis-on-dataframes-and-datasets.html) | `foreachBatch`, state-store locality, checkpointing and recovery-change limitations. Custom batch output is at-least-once by default; deduplication requires application/sink participation. State reuse, repeat evaluation and checkpoint compatibility have separate lifetimes and costs. These are selected sections, not an audit of the full API. |
| [Zaharia et al., RDDs, NSDI 2012](https://www.usenix.org/conference/nsdi12/technical-sessions/presentation/zaharia) | Publisher abstract. Coarse immutable transformations and reuse address iterative/interactive jobs; the restricted model is distinct from arbitrary fine-grained mutation of shared state. No benchmark claim is imported. |

[F19](WORKLOADS.md#f19--spark-like-applications-over-reusable-distributed-data)
draws the SixDB implications. Pinning extension/runtime/input identities,
separating cache from durable publication, exposing finite resources and choosing
the transaction boundary are our composition requirements, not claims that Spark
already supplies SixDB's transaction or extension-verification semantics.
