# Orbital material worth revisiting

This is a consultable list of questions, useful examples and evidence from older
drafts, SixDB studies and Calico. Inclusion does not select a mechanism or add a
requirement to the [brief](BRIEF.md). Networking in particular still awaits
Ashton's further input. Engine owns application semantics; Orbital must be usable
by applications with entirely different data structures.

The [draft map](stale-drafts/README.md) distinguishes the older proposals and the
different historical meanings of “BRIEF2”. The [Calico map](../workbench/notebook/calico.md)
provides broader navigation. Calico and Consurgent links below assume sibling
checkouts; the [pitch script](../../consurgent/pitch/SCRIPT.md) explains the
intended adoption model. Check implementation headers and measurement reports
alongside design prose: some attractive descriptions were never implemented.

## Durable objects, memory and local execution

- **Make durable memory usable by ordinary programs.** The
  [early narrative](stale-drafts/BRIEF.md), in its memory and closing design-lessons
  paragraphs, starts with durable objects and projects them into memory. Mine
  stable pointers, lazy materialisation, copy avoidance and page-remapped growth.
  Compare [xmem DESIGN §3 and §12](../../calico/xmem/DESIGN.md) with the five paths
  in [fault.h](../../calico/xmem/include/xmem/fault.h). Calico's page claims and
  single-writer rules are not SixDB contracts; remote faults and facade wiring
  were incomplete.

- **Choose object granularity for more than locking.** The early narrative's
  recovery/conversion discussion asks whether an object can be restored,
  converted and verified without unrelated state. Revisit that alongside the
  current object-owned conflict scopes. Physical pages, logical objects and
  atomic publication units need not coincide.

- **Bound resident memory without accidentally pinning a shard.**
  [ServingImage](../../calico/xmem/include/xmem/serving_image.h),
  [pins.h](../../calico/xmem/include/xmem/pins.h) and
  [Loom residency admission](../../calico/loom/spec/IO.md) distinguish discardable
  serving images, readiness, session pins and exceptional range/version pins.
  Mine reclamation and source-incarnation checks. Local residency holds do not
  establish semantic retention or distributed retirement.

- **Keep durable versions independent of resident addresses.**
  [Ikea integration, “A sealed local change”](../workbench/spikes/ikea-composition/semantics-and-integration.md)
  distinguishes physical coverage, logical effects, derived-state validity and
  visibility. Its lifecycle is illustrative, not six compulsory API calls or a
  reason to reinstate the old validation protocol.

- **Share one transaction across local workers.**
  [omachine CONTRACT, opened-handle sharing](../../calico/omachine/CONTRACT.md)
  and [Xmem::attach](../../calico/xmem/include/xmem/xmem.h) explore several workers
  contributing to one outcome without copying whole working sets. Existing
  partitions are disjoint, page-bounded and local to one machine; shared mutation
  and capability carriage across processes or VMs remain separate questions.

- **Account for work until it actually releases resources.**
  [Loom's concrete situations](../workbench/notebook/loom-objectives-and-architecture.md),
  [Calico DISK](../../calico/loom/spec/DISK.md) and
  [completion attribution](../../calico/arbor/include/arbor/completion_attribution.h)
  cover output growth, slow consumers, completed-but-undrained work and
  cancellation. Requesting cancellation does not free a backend's active buffer.
  Reuse the cases before selecting queue structures or concurrency limits.

## Contention, preparation and application boundaries

- **Keep the workload's promise fixed.**
  [Large reads](../workbench/spikes/orbital-scenarios/reconsideration/READS.md) and
  [worked ELT histories](../workbench/spikes/orbital-scenarios/ELT-WORKED.md)
  distinguish coherent reports, actions on current state, and historical data
  products. MERGE, cascades, refresh and CDC expose different obligations.
  Finite authored examples do not establish unrestricted SQL support.

- **Distinguish broad effects from broad physical footprints.**
  [Large writes](../workbench/spikes/orbital-scenarios/reconsideration/WRITES.md)
  separates private construction, logical effect description, source dependence
  and atomic visibility. The
  [envelope audit](../workbench/spikes/orbital-scenarios/ENVELOPE-AUDIT.md) tests
  complete ownership and evidence coverage. Index examples belong to the Engine
  binding: logical ownership can avoid unstable physical footprints, but cannot
  make unknown routing or global predicates free.

- **Retain the small examples that spread a large wait.**
  [Locality](../workbench/spikes/orbital-scenarios/reconsideration/LOCALITY.md)
  follows a blocked `x`, a queued `(x,y)` transaction, and an otherwise local `y`
  writer. It separates real dependencies, future claims and resource pressure.
  [History](../workbench/spikes/orbital-scenarios/HISTORY.md) retains the large
  arbitration components and stale-verdict failures that motivated replacement.

- **Separate allocation serialization from computation dependencies.**
  [Pipelining](../workbench/spikes/orbital-scenarios/PIPELINING.md) and
  [release after position](../workbench/spikes/orbital-scenarios/RELEASE-AFTER-POSITION.md)
  compare blind replacement with dependent read-modify-write and retain negative
  provisional-position and capacity-cycle examples. Synthetic ticks and small
  histories do not establish production throughput, bounded backlog or recovery.

- **Distinguish discovering more work from discovering another shard.**
  The [early structured brief](stale-drafts/BRIEF2.md), “Consensus — Multiple
  Shards” and its worked example, follows A→B→A. The
  [arbitration brief](stale-drafts/BRIEF-arbitration.md), “Preparation and
  Execution”, develops the same issue. Keep the scenario without inheriting
  retained locks, invalidation or expanding protection sets.

- **Fold only when all observable results agree.**
  [Findings, “A fold can combine writes”](../workbench/spikes/orbital-scenarios/FINDINGS.md)
  compares integer contributions with operations that return intermediate values.
  Equal final deltas are insufficient; output and execution metadata matter.
  Integer laws do not automatically extend to floating arithmetic or arbitrary
  native programs.

- **Revisit alternatives by the tradeoff they accept.**
  The [prior-art survey](../workbench/spikes/orbital-scenarios/reconsideration/PRIOR-ART.md)
  and [convergence comparison](../workbench/spikes/orbital-scenarios/reconsideration/CONVERGENCE-PRIOR.md)
  compare snapshots, validation, predetermined order, multiversion execution and
  coarse ownership. Preserve counterexamples and limits, rather than assembling
  every mechanism into Orbital. The
  [comparison report](../workbench/spikes/orbital-scenarios/COMPARISON.md) includes
  failed and pending requests as well as completed latency.

## Extensions, determinism and reusable work

- **Capabilities for fast I/O.** The [early narrative](stale-drafts/BRIEF.md),
  sandboxing paragraphs, explores broker-created restricted io_uring rings,
  registered-file slots and brokered opens/connects. Reconsider where this saves
  overhead while keeping ordinary syscalls available; do not inherit an old
  syscall allowlist or an assumption of zero sandbox cost.

- **Verify complete interactions, not just a final return value.**
  [Composition](../workbench/spikes/orbital-scenarios/reconsideration/COMPOSITION.md)
  and the [local-context probe](../workbench/spikes/orbital-scenarios/extension_context_probe.py)
  retain the “same final OK, different requests” counterexample. Compare with
  [rollup.h](../../calico/xmem/include/xmem/rollup.h)'s directive chains and hashing
  fused with copying. Keep today's transaction-wide all-match rule distinct from
  Calico's majority/hash policy and the older probe's epoch-wide gate.

- **Keep logical identity across movement and restarts.**
  [omachine SCOPE](../../calico/omachine/SCOPE.md) distinguishes actor, instance,
  invocation, workflow and locator lifetimes; its
  [CONTRACT](../../calico/omachine/CONTRACT.md) discusses buffered host directives
  and quiescence. Mine identities and lifecycle cases, not the ILNP layout,
  CIDR-derived trust or assumed future hypervisor. Much of that runtime was unbuilt.

- **Retain the versions needed to interpret compact inputs.** The
  [early narrative](stale-drafts/BRIEF.md)'s closing compression ideas and
  [CONSENSUS §§3–4](stale-drafts/CONSENSUS.md) suggest operation/plan identifiers,
  parameters and shared dictionaries instead of repeated descriptions. Ask what
  must remain available for replay and what compression costs. Planning and the
  meaning of those identifiers stay application-owned.

- **Share analysis without letting arrival order select semantics.** The final
  notes in the [early structured brief](stale-drafts/BRIEF2.md) suggest carrying
  partial analysis with propagation. Compare
  [Ikea's curated regions and execution grain](../workbench/spikes/ikea-composition/design.md):
  storage tiles, working width, scheduling stops and output representation are
  separate choices. Versioned hints may save repeated work; they must not alter
  agreed outcomes merely because one consumer receives them first.

- **Make numerical determinism explicit.** The
  [early narrative](stale-drafts/BRIEF.md)'s determinism paragraphs and
  [CONSENSUS §5](stale-drafts/CONSENSUS.md) raise reduction order, FMA, architecture
  differences and captured time/entropy. Preserve the obligations without
  assuming emulation, a particular accumulator or an unmeasured cost.

## Dissemination, latency and resource costs

- **Name the completion event before comparing latency.**
  [CONSENSUS §§1 and 8](stale-drafts/CONSENSUS.md), the
  [CFT commit study](../workbench/spikes/cft-commit-latency/commit/README.md) and
  [Calico's measurement corrections](../../calico/xmem/MEASUREMENT_NOTES.md)
  distinguish payload durability, witness admission, follower propagation start,
  leader acknowledgement and caller-visible completion. Test leader, follower
  and external origins. Serial 4 KiB measurements are not saturated throughput;
  neither RTT/2 nor sums of per-leg percentiles establish a directional tail.

- **Stage and prepare outside the latency-sensitive path, but charge the work.**
  [Calico DESIGN §5](../../calico/xmem/DESIGN.md), its
  [commit report](../../calico/xmem/REPORT.md), and
  [CFT persistence findings](../workbench/spikes/cft-commit-latency/persistence/FINDINGS.md)
  explore staging, batching and prepared log space. Writing and synchronizing
  prepared space can matter beyond allocation. Include concurrent preparation,
  hashing and sustained device limits in the comparison.

- **Choose paths using repeatable evidence.**
  [Network selection](../workbench/spikes/cft-commit-latency/network/selection.md),
  [port sampling](../workbench/spikes/cft-commit-latency/network/studies/port-sampling.md)
  and [clock limits](../workbench/spikes/cft-commit-latency/network/clocks.md)
  support screening, held-out validation and rechecking host/flow tuples. They
  do not justify arbitrary leader rotation or a permanent AZ ranking. Joint
  fanout and persistence need their own measurements.

- **Keep routing policy separate from logical connection identity.** The
  [early narrative](stale-drafts/BRIEF.md)'s networking paragraphs discuss local
  versus tunneled connections and graph costs including bandwidth, egress, NAT
  and requests. The [retired propagation study](../workbench/notebook/consensus-networking.md)
  retains lessons on chain depth, shared-NIC concentration, bursts and repair
  traffic. Its all-recipient synthetic latency was not durable-commit latency;
  its optimizer and proposed graph algorithms are not selected networking design.

- **Make independently produced delivery duplicate-safe.**
  [Calico DESIGN §7](../../calico/xmem/DESIGN.md) and
  [EgressLane](../../calico/xmem/include/xmem/fold.h) use contiguous per-recipient
  acceptance rather than treating any higher sequence number as complete.
  Mine gap, resend and recipient-isolation histories. Deduplicated lane acceptance
  does not establish exactly-once external effects or select Calico's sender policy.

- **Do not let background obligations become a hidden quorum barrier.**
  [CFT results](../workbench/spikes/cft-commit-latency/RESULTS.md),
  [throughput](../workbench/spikes/cft-commit-latency/commit/throughput.md) and
  [the sustained-load cliff](../workbench/spikes/cft-commit-latency/commit/cliff.md)
  expose lagging-replica buffer pressure and overload. Retain completion fraction,
  backlog and unfinished work: improving completed p99 alone can hide a worse
  service. Catch-up and repair still need real capacity.

- **Compress what is actually expensive.**
  [CONSENSUS §4](stale-drafts/CONSENSUS.md) gives frontier/range metadata arithmetic;
  [omachine's compression discussion](../../calico/omachine/CONTRACT.md) considers
  raw, XOR/runs, alignment and shared dictionaries. Count framing, authentication,
  CPU and recovery costs. A specific source conflict matters: xmem DESIGN says
  its single-gap aligner is enabled, while
  [BACKLOG](../../calico/xmem/BACKLOG.md) and rollup.h say it is unbuilt and staging
  sends full dirty pages. The design claim is not implementation evidence.

- **Replicate inputs and derive local state where that pays.**
  [Calico's cross-AZ indexed-ingest report](../../calico/xmem/REPORT.md) and
  [its harness](../../calico/xmem/tools/xmem_clickbench_ingest.cpp) test canonical
  inputs with locally derived indexes. The experiment used a million projected
  ClickBench rows and two indexes. It does not establish arbitrary extension
  determinism, economical cold recovery or a universal network saving.

- **Require saved work to repay its bookkeeping.**
  [Aggregate-maintenance conclusions](../workbench/spikes/aggregate-maintenance/CONCLUSIONS.md)
  and [dirty-buffer findings](../workbench/spikes/aggregate-maintenance/dirty-buffer/FINDINGS.md)
  show that fewer logical updates can still mean more CPU. Filters, coalescing
  and dirty-state tracking must repay construction, replay and reset costs.
  These measurements suggest comparisons, not an Orbital metadata policy.

## Failure, recovery and retained evidence

- **Find durable work that has not yet been admitted.**
  [CONSENSUS §3](stale-drafts/CONSENSUS.md) discusses stream registration,
  discoverable holders, complete tails and torn records. Revisit acceptance,
  cancellation, discoverability and ownership without inheriting permanent
  stream ownership or the old witness hierarchy.

- **Recover ordering obligations as well as application pages.**
  [PITR's reopening frontier](../workbench/spikes/orbital-scenarios/recovery/PITR.md)
  includes pending outcomes and completed-reader bounds. Compare the
  [Calico reopen ledger](../../calico/workbench/science/systems/recovery/XMEM_REGION_OPEN.md)
  and its interruption cases. That ledger explicitly excludes general PITR,
  reconfiguration and multiregion reopen; its LAND/rollback authority rules do
  not carry into SixDB.

- **Prove who stopped admitting and who may now admit.**
  [Handoff](../workbench/spikes/orbital-scenarios/recovery/HANDOFF.md) retains
  possibly chosen suffixes, terminal promises, successor authority and partial
  installs. Calico's evaluate-before-revoke cases offer further counterexamples.
  The bounded probes assume certificates and receipts; they do not implement
  consensus, transfer or finite-resource recovery.

- **Judge distress response by outcomes and false-alarm cost.**
  [Detection](../workbench/spikes/orbital-scenarios/recovery/DETECTION.md),
  [failure scenarios](../workbench/spikes/orbital-scenarios/recovery/SCENARIOS.md)
  and [SOS](../workbench/spikes/orbital-scenarios/recovery/SOS.md) connect probes,
  proportional action and evidence preservation. Witness health does not imply
  payload or consumer health. Keep independent recovery copies, retrieval paths
  and fast prefix restoration in view; historical incidents demonstrate possible
  dependencies, not their frequency.

For reproduction, use each study's evidence and recovery notes. The
[contention workbench](../workbench/spikes/orbital-scenarios/README.md) and
[CFT evidence guide](../workbench/spikes/cft-commit-latency/evidence.md) distinguish
current sources, frozen historical sources, retained results and larger archived
artifacts. Successful probes are evidence for their stated cases, not proofs of
the composed system.
