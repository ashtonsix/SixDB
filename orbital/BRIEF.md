# SixDB Orbital

Calico xmem and Orbital—now bundled as “SixDB Orbital”—made several audacious bets, some right and some wrong. The clearest concise account of their intended role is in `~/consurgent/pitch/` (web slides and script).

Orbital was conceived as a full OS+hypervisor; that's still the eventual vision, but emphasis on “eventual”. This lens positioned Calico as “one process among many”, but I hubristically underestimated the investment required to pull a frontier-advancing hypervisor off, and will need to keep Consurgent focused on HTAP+ETL for longer than I initially anticipated. I now see SixDB as a central and extensible locus.

Deterministic WASM extensions can safely run untrusted, hot-swappable, resource-bounded computation close to the data, and produce fold-local mutations with microsecond-latency consensus. Clients can ship small programs to eliminate request waterfalls; gateways can deploy packet filters into SixDB and establish direct database-to-destination connections, keeping bulk traffic off metered NAT gateways. Application-facing extensions belong partly in `shore/`; runtime, networking, permissions, and substrate concerns belong in Orbital. A hardened WASM runtime is far more tractable than a full hypervisor.

Let's now consider consensus.

Our system has three shard kinds: global witness (one per deployment), regional witness (many), and data (many). Witness shards always use 2-of-3 consensus, with machines elected to fill three sub-roles: leader, fast follower, and slow follower. Election as fast follower grants permission to initiate propagation. VMs may belong to many shards and fill one role per membership.

Every VM owns an independent WAL for each shard it submits to. These logs never change owner. Each WAL is identified by its contributor VM and destination shard, and LSNs are scoped to that stream. This makes distributed durability simple: one other VM needs a durable copy of the WAL for the log to survive loss of either machine. To survive AZ loss, that other VM needs to be in a different AZ, and so on for region loss, provider loss, and other survival criteria.

A witness quorum must agree which entries belong to each ordered epoch before their effects become part of a data shard's logical state or are exposed as admitted results. Submitting to a journal is final.

For latency-critical logs, a database may feed a journal and a data-loss-tolerant processor concurrently, within a microsecond of collection, without waiting for persistence or admission.

The witness and data shards need not overlap, so long as data shards accept witness authority; witness shards don't even need access to the WAL payloads to ensure consistency across data shards.

Given a deterministic reducer over data shard epochs, the witness shard must affirm something to the effect of “Data shard A, in epoch B, admits LSNs C_i–C_j from WAL stream C, D_i–D_j from D, …”, with each stream identifying its contributor and destination shard.

This sort of admission is amenable to merge on fan-in (of contributors and LSN ranges), resource-cheap (in terms of network and compute), and even permits cross-jurisdiction commits without requiring data to pass national borders. Latency aside, a single witness shard could likely scale to govern consistency across all data shards in a planetscale deployment. This is attractive for multi-shard transactions: VMs can submit into multiple data shard journals with a single request.

The witness shard can be Raft/Paxos-like. Recovery under the TLA+-checked consensus algorithm preserves committed history. Point-in-time disaster recovery (PITR) may lose data.

| Witness members unavailable | Consensus and recovery consequence                                                                                                                       |
| --------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| One of three                | The remaining two can form a quorum and continue once they can communicate.                                                                              |
| Two of three                | Admissions pause until a quorum is restored or lossless succession completes. Destroying both stores can also erase admissions held only by that quorum. |
| All three                   | No witness authority remains available. Restore durable witness state if possible; otherwise use PITR.                                                   |

If a data shard member considers its witnesses inoperable and unable to recover, it may nominate replacements, whose adoption requires a ⌊n/2⌋+1 quorum of data shard members under the current election membership. The election must establish one unambiguous successor, even if unreachable machines continue recognising the old witnesses.

PITR may abandon a missing suffix after a bounded recovery period, choosing a boundary consistent across participating journals and recording the successor authority and cutover. This can lose acknowledged work and falls outside consensus safety. Returning members must synchronise to the restored history or be evicted.

Distressed VMs could broadcast an emergency reduction in their election stake to help survivors retain a quorum. These reductions must not ambiguate succession, even when broadcasts reach only some members.

To motivate further design, consider a money transfer service that needs fast intra-region transfers without needless delays from awaiting global transfers.

A VM maintains separate WALs for submission to its regional/single-shard-covering journal and its global/all-shard-covering journal. The loop for a global admission is thus:

1. The VM submits to the global journal's witness shard.
2. The global journal mints an epoch and shares it with subscribed regional journals.
3. Regional journals resolve interleaving of global and self-authored epochs, canonicalising region-local epoch order, and inform VMs in the data shard.
4. VMs start to apply log effects.

If regional journals A and B accept entry C, VMs must keep its result hidden until C is durably admitted in every participating journal and the containing epochs have cleared, including predecessor and cross-region dependencies.

Approached naively, regional interleaving fights both deterministic reduction and the latency objective. Reading mutable state from another shard makes an epoch’s result depend on read timing, implying complex time travel requirements. We therefore require each epoch’s result to be a deterministic function of the state produced by preceding journal epochs and the inputs admitted to that epoch. Snapshots record the state produced by an earlier journal prefix, accelerating recovery and optionally allowing the covered WAL records to be discarded.

Each epoch also poses hazards for its successor, making an in-order clearance constraint attractive. However, this could leave a region-authored epoch blocked behind a global epoch with high-latency data dependencies. A planetscale-capable database should therefore support splitting transactions across epochs and tracking continuations. In our money transfer example, one epoch might reserve funds for an outgoing transfer, leaving a continuation to finalise or release the reservation in a later epoch. The earlier epoch completes the reservation, allowing intervening region-local work to proceed while the transfer awaits settlement.

Deterministic reduction requires (non-exhaustive):

- Random numbers are procedurally generated from journaled entropy. Clock values are journaled too.
- Where VMs differ in architecture, floating-point math is emulated (possibly Kulisch-style for OoO), or alternatively, results are journaled before availability. Automatically compiler-selected FMA is disabled.
- Operations applied out-of-order must converge to the same fixpoint under every permitted ordering before epoch clearance.

So long as VMs agree on logical state and observable behaviour, physical representation may diverge. VMs may independently prepare for future epochs, maintain auxiliary indices and caches, and choose lossless encodings suited to their architecture and access patterns. A VM may, for example, Zstd-compress cold data without consulting its peers. Object identity therefore binds to logical content rather than local encoding. These objects should be small enough to recover, convert, and verify independently without processing unrelated state.

Let's connect consensus back to Orbital writ large.

A VM can host the database alongside sandboxed extensions, with a single epoch encompassing dataflow and iteration among them. Intermediate outputs may feed further invocations within the same epoch without separate journaling, provided they are deterministically derivable from the epoch's admitted input. Execution must either follow a canonical order or yield the same logical result under every permitted ordering. Extension output hashes (XXH3 or BLAKE3) should be journaled for verification. For extensions, identity at the interface demands equality.

Replay reconstructs logical state from recorded admissions and inputs. It does not redeliver entries to immediate log consumers. State-producing deterministic extension computation is replayed as needed; replay must not reissue external effects.

Initially, extensions will run in a determinism-hardened WASM runtime. We may also support basic Firecracker containment, subject to the same determinism requirements. Features like memory sharing, GPU access, and vCPU hot-swapping are outside the initial scope.

Much of Orbital's design still applies without KVM: SixDB workers should receive broker-provisioned, restricted `io_uring` instances and, when using `io_uring`, access files and sockets only through immutable registered-file capability slots. Dynamic opens and connects remain brokered; seccomp blocks ring creation or reconfiguration and direct resource creation, permitting only `io_uring_enter` and the minimal runtime syscall set. The aim is to keep SixDB practically sandboxable without performance compromise. The specification should cover operation and flag restrictions using [liburing's restriction API](https://man7.org/linux/man-pages/man3/io_uring_register_restrictions.3.html).

Calico xmem made extensive use of UFFD, and SixDB should too. SixDB and extension code can use ordinary memory reads and writes, calling Orbital APIs only to allocate, free, or persist memory. Underneath, Orbital can provide hardware-enforced single-writer access, copy-on-write, and lazy materialisation. Copies can be skipped for uniquely referenced data when prior state is recoverable from retained state. Objects remain addressable through virtual pointers while untouched pages stay on disk, and, with suitable address-space reservation, page remapping lets containers grow linearly without copying existing contents. Separating staging from commitment spreads work over time, reducing resource pressure during bursts.

Let's consider commit routing topology.

SixDB should retain the proposed WireGuard-based networking over IPv6 ULAs, adaptive MTU/packet/FEC tuning, Tailscale-style NAT punch-through, custom UDP transport, and separation of logical connections from physical tunnels, switching between local and network transport based on peer location without changing connection semantics.

Imagine three AZs with three machines each, where cross-AZ bandwidth incurs cost. Sending a payload directly to every peer incurs six cross-AZ transfers. Sending once to each remote AZ and relaying locally reduces that to two. Bulk transfer can follow these cheaper paths while small, direct exchanges of hashes verify content equality.

The same principle applies across regions and providers. Public internet egress often carries hefty premiums, while paying once to reach a relay that distributes data over cheaper links can substantially reduce broadcast costs. Requests originating at the edge are especially attractive because they avoid that initial transfer. Private interconnects can further reduce per-byte charges, with their break-even determined by fixed costs. Blob storage often incurs fixed per-request charges; for small objects it can be economical to request these from peer VMs instead.

For modelling, cross-AZ transfer, internet egress, metered NAT processing, and per-request charges can all be represented as annotated graph edges; failure, uncertainty, resource exhaustion, live graph rewriting, and other factors complicate, but the delivery problem is somewhat amenable to modified Edmonds' algorithm.

Let's consider commit sequencing.

For a regional commit, there are five temporally overlapping objectives:

| Objective                      | Dependencies                                                |
| ------------------------------ | ----------------------------------------------------------- |
| Log persistence                | Can start immediately.                                      |
| Admission                      | Depends on persistence and the admission constraints below. |
| Log propagation                | Can start immediately.                                      |
| Persistence-status propagation | Depends on persistence.                                     |
| Admission-status propagation   | Depends on admission.                                       |

Taking overlapping roles and sequencing into account, the critical path can become remarkably short. 140µs p99 cross-AZ durable commits at the fast follower appear viable. The [CFT commit-latency work](../workbench/spikes/cft-commit-latency/README.md) informs the following schedule, using network budgets of 125µs to the fast follower (half the cited p99 RTT) and 175µs to the slow follower.

| Time     | Event                                                                                                                                                                                                                                              |
| -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0µs      | The witness leader, also a data shard member, initiates commit. It sends the WAL entry to both followers and starts a pre-prepared NVMe PLP write (15µs).                                                                                          |
| 15µs     | The leader's write is complete. The leader notifies both followers.                                                                                                                                                                                |
| 125µs    | The fast follower, also a data shard member, receives the WAL entry and begins its write.                                                                                                                                                          |
| 140µs    | The fast follower's write completes and the leader's notification arrives. The fast follower commits, notifies the leader, initiates log and status propagation along a minimum spanning arborescence, and starts applying the WAL entry's effect. |
| By 265µs | The fast follower's notification has reached the leader; all three witnesses have committed.                                                                                                                                                       |

For improved batching, the leader's write may move to T=15µs, while its initial payload sends and notification move to T=30µs, shifting later events accordingly. Immediate data-loss-tolerant processors need not wait for this schedule. If the fast follower doesn't return to the leader in a timely manner, the leader may idempotently initiate propagation upon acknowledgement from the slow follower.

The admission constraints are:

1. LSN runs within a contributor's shard-specific WAL are gapless.
2. An entry and its continuation cannot share the same epoch; continuations go in a later epoch.

The witness leader buffers submissions until their admission prerequisites clear. Capacity backpressure applies before accepting additional work; accepted submissions cannot be retracted because of buffer pressure or a retention deadline. Admission-status propagation reports committed epoch assignments.

Global commits, commits originating with a follower, commits originating outside the witness shard, and commits interrupted by failures all deserve as much attention to low-latency sequencing.

SixDB doesn't merely cache query plans, but stores them long-term; journaling plans creates WAL compression opportunity. Entries need only provide a query identifier, version, and parameter values. With shared dictionaries and models also journaled these parameters may compress exceptionally well. The plan, dictionary, model, and code versions needed to interpret retained WAL must remain available for replay.

Calico xmem's design mistakes include:

1. Starting with contigous bytes in memory and building durability around them. SixDB Orbtial should flip this, starting with durable objects and building projection into memory around them.
2. Admission into deterministic reducers/folds was designed, but not recognised as the central organising principle, causing the admission mechanism to feel tacked on.
3. xmem was built independently from Orbital and without much regard for Calico, so lost out on optimisation opportunities a deeper integration could afford through WAL compression, storage tuning, and network tuning.
4. The story for multi-shard transactions was thin, and depended on contributors obtaining exclusive write claims to the byte ranges affected before submission. Determining those byte ranges to fine granularity ahead-of-time was not always tractable for independently mutable shards.

(One arc of) the path ahead looks something like:

1. Write this brief (done)
2. Write consensus spec (next)
3. TLA-check consensus
4. Reference implementation (consensus)
5. Production implementation (Orbital)
