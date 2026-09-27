# Native reference models

`orbital.hpp` exposes a bounded composition that remains useful as the simulator
grows: `assemble` installs actors into a caller-owned `Simulation`; `run_case` is
an ordinary convenience client. Workloads, placement, host services, links and
faults are assembled independently. Supplying an empty link vector creates no
inter-host connectivity. Actors receive local options and authenticated message
senders, never the case's future traffic, fault schedule, topology or observer.

The composition includes two shards. Each has a prepared leader, two followers
and three consumers. Every metadata command carries its full body, is written
by the leader, replicated and written by a follower, and reaches each consumer
through messages. Consumers persist their own receipt evidence before folding
the same ordered commands. The current prepared path requires **the leader and
at least one follower**, on distinct hosts. It is a particular 2-of-3 path, not
a leader election or general consensus implementation. No harness supplies
agreed epochs or durable outcomes.

Transactions discover source frontiers, reserve output scopes in shard order,
announce inclusive minima after all reservations, persist an immutable position,
and fix each participant. A participant releases its reservation after its own
fix; the coordinator waits for every fix before execution. Reads register bounds
and wait for overlapping unresolved predecessors. A durable outcome resolves
all declared outputs, including no-change/abort outcomes, before client success
or failure. A two-shard transfer exercises this composition.

The default reservation policy preserves the brief's no-overtaking rule.
`QueuePolicy::eligible_first` is an explicit counterfactual: an earlier blocked
waiter no longer excludes otherwise eligible writers. Live reservations still
exclude conflicting grants, and all other transaction rules remain the same.
The policy is fixed for the complete run, including recovery. The
[ordering experiment](../experiments/README.md) tests both its locality benefit
and its loss of broad-writer progress; it is not a change to the brief.

The integer-cell application owns read/write declarations, its canonical value
bytes and the meaning of put, sum and transfer. The ordering fold treats scopes
and effect bytes as opaque. This small fixture is not an Engine plan format.
Checked sums persist a context naming source, program and representation
versions; two checker actors obtain private projections at its fixed cut, using
two real consumers, and persist their reports. Continuing source writes may
finish while one checker is delayed. The coordinator durably compares all
required reports before recording an outcome. Disagreement durably aborts the
transaction and resolves its output obligation. The fixed sum transcript is not
a general extension interaction/status transcript, a WASM runtime or Firecracker
execution. Fingerprints are accidental-corruption checks, not BLAKE3 or a security
claim.

Recovery reads an actor's durable root, pages through bounded directory listings
and loads individual actual records. A destroyed consumer rebuilds from available
witness messages and folds the retained prefix. The coordinator can recover its
input, immutable position, verification and outcome after a machine reset; its
outcome journal is **local**, not replicated. Permanent loss of that journal,
prepared authority transfer, source-body/frontier admission equivalence,
complete-replacement supersession and history reclamation remain outside this
model. Witness process restart can replay its own surviving local records, but
permanent loss of leader storage is not automatically repaired. There are no
claims about elections, network adaptation or sustained throughput.

Each immutable retained record owns a charged buffer. Consumers additionally
charge a per-command allowance for logical metadata; recovery pays the same
allowance. Requests and private checker projections hold explicit buffers until
their documented owner can release them. A 2 KiB per durable actor allowance
approximates bookkeeping, and actual retained serializations dominate the
variable charge. STL container/allocator overhead and duplicate parsed fields
are not measured exactly. Record recovery/fetch reserves a 16 KiB destination;
root reads reserve 256 bytes. Service delays and these allowances are authored
model costs, not measurements. Both protocol and observer retain growing history:
these finite experiments cannot establish sustainable throughput or reclamation.

The always-attached observer joins submitted record bytes to actual
`storage.write` commits. It checks actual quorum evidence, prefix agreement of
full logical state and emitted outputs, immutable position/outcome correspondence,
private snapshot values and nonmutation, unresolved-predecessor read gates,
verification/report provenance, independent application arithmetic, and publication
after all participant resolutions. A finite-prefix read negative is caught while
the skipped predecessor is still unfinished. Trace output is a separate sink;
turning it off does not remove safety checks or fault coverage. Safety checks
observe executions; they do not constitute a proof of progress or refinement.

`orbital.*` records use JSON details with a `bytes` hex field containing typed,
length-bounded little-endian model values. Additional fields expose transaction,
shard, command, position and fixture values for independent tooling. The format is
version-local diagnostic evidence, not production wire compatibility. Actual
storage commits retain runtime operation IDs so a write accepted before its
callback is not confused with an unpersisted intention.

The result's `offered` count is the complete authored arrival schedule, the cohort
denominator: completed + failed + unfinished. `arrived` counts distinct client
receipts; `not_yet_offered` distinguishes scheduled arrivals beyond a paused,
budget-limited or finite observation horizon. `accepted` counts actually persisted
coordinator inputs, and `admitted_unfinished` counts those without a terminal client
response. Missing incidents, unfinished obligations and safety violations are
separate. `pending` normally remains true because retry timers persist after all
offers finish. Fault receipts mean the action actually ran. A fault scheduled at a
durable write's timestamp precedes its callback under FIFO ordering; seeded ties
may deliver that callback first. Exact write-before-callback recovery is tested
under FIFO, while seeded cases explore both tie orders.

The separate [retention-pressure model](retention_pressure.hpp) composes an old
reader, replay consumer, overwrite stream and small independent writer. Its
serialized root authority reconstructs actual bytes through the runtime's ports;
independent receipt checks guard reclamation and terminal results. The
[experiment guide](../experiments/README.md#old-readers-replay-and-reclamation-under-pressure)
owns the matched cases, negative controls and limits of this fixture.
