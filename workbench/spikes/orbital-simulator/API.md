# First executable boundary

This is a working interface for the learning spike, not an Orbital contract.
Python imports below refer to the adjacent `sim.py`. Time is integer nanoseconds;
all service costs are authored model inputs, not measured latency predictions.

```python
class Example(Actor):
    def on(self, ctx: Context, kind: str, data: dict):
        ...

w = World(seed=1, ordering="fifo")  # or "shuffle": reorder simultaneous events
w.add_host(Host("h", domain="az-a"))
w.add_host(Host("other", domain="az-b"))
w.add_link(Link("h", "other", latency=10_000))  # directed
w.add_actor("example", "h", lambda: Example())
w.inject("example", "start", {}, at=0)
w.run(until=1_000_000)
```

Actor factories receive no recovered state. Recovery starts with `boot` and uses
the same storage ports as ordinary execution; the factory may capture immutable
bootstrap configuration, never the workload history or another actor's state.
The initial actor also receives `boot`. Each actor handles one event at a time.
Handlers are atomic at modeled boundaries and consume `Host.handler_ns` on its
control CPU pool. Compute continuations consume its separate worker pool.

An actor's `Context` exposes its `actor`, `host`, `incarnation` and local `now`,
and these operations:

- `send(target, kind, data, size=128) -> bool`: local buffer acceptance only;
  receiving and sending share machine NIC capacity. No direct link or remote
  failure produces no acknowledgement. Actors implement relaying/retries.
- `timer(delay, kind, data=None)`: incarnation-bound local timer.
- `compute(duration, then, data=None, pool="workers", leases=()) -> bool`:
  schedule finite service; callback carries supplied data. Pools `workers` and
  `control` exist. An actor must choose where work runs.
- `persist(key, value, then, data=None, size=None) -> bool`: enqueue an atomic
  durable-record replacement in its host's actor namespace. Callback contains
  supplied data plus `ok` and `key`. Process death may leave a submitted write
  completing; power loss cancels uncompleted device work. Completed writes
  survive power loss, but not destruction of that host's storage.
- `load(key, then, data=None) -> bool`: physical read; callback contains supplied
  data plus `ok`, `key` and `value` (`None` when absent).
- `scan(prefix, then, data=None) -> bool`: physical read of matching records;
  callback contains supplied data plus `ok` and `records` (a key/value dict).
- `reserve(size, label) -> int | None` and `release(lease)`: explicit resident
  memory ownership. Compute may retain leases; releasing an actor's ownership
  does not reclaim memory until asynchronous users retire. Actor death releases
  its ownership, not outstanding backend users.
- `note(kind, op=None, **fields)`: semantic telemetry; observers receive facts,
  never supply decisions. `wait(op, reason, **fields)` records an outstanding
  wait; `clear_wait(op)` retires it.

Only immutable/copy-isolated JSON-compatible data crosses ports. Durability is
bounded per host. Allocation/enqueue refusal is synchronous and local. A true
return never establishes remote delivery, durable completion or publication.

The harness owns `crash(actor)`, `restart(actor)`, `power_loss(host)`,
`power_on(host)`, `destroy(host)` and `partition(source_host, target_host, blocked)`.
Use `fault(at, action, **args)` for timed faults and
`when(kind, predicate, action, **args)` for a one-shot semantic fault trigger.
`action` uses those method names. Triggers run in the environment, not actors.
`slowdown(host, resource, factor)` scales service duration when a queued job starts;
`pause(host, resource, paused=True)` prevents new starts. Active work retains its
booked service time. These methods are also fault actions. A partition drops at
the end of link byte service; packets already propagating can still arrive.
`crash` loses one actor incarnation, not every worker in a modeled OS process.
Machine power-on creates fresh incarnations of all its actors. Destruction also
removes all of that machine's completed durable records.
Directed topology can be built in ordinary Python; no topology/configuration DSL.

`w.trace` is a list of records (`id`, `time`, `kind`, `cause`, and fields).
`w.waits` contains outstanding waits; `w.explain(op)` returns waits and a causal
slice. Observer code may inspect `w.hosts`, `w.actors` and `w.durable(host, actor)`;
algorithm code must not. `w.report()` summarizes modeled resources, offered and
terminal semantic events, unresolved waits, and source-independent replay data.
`w.decisions` records simultaneous-event choices; a new `World(replay=...)`
rejects divergence. Replaying uses the same scenario and source version, not an
unversioned trace as executable code.

Wait reports belong to the reporting incarnation. Losing it moves them to
`report()["retired_waits"]`, without marking an obligation complete. The application
observer separately accounts for required outcomes. Service reports include
completed, cancelled, queued and active work; `busy_ns` includes time already spent
on active work. `wire_transmitted` counts completed link byte service, including
subsequent loss; `packet_departed` means only completion of the source NIC stage.

The model admits whole atomic records, and a read snapshots its record at submission
then pays device service. Every actor has its own durable namespace on its current
host. This is an explicit storage abstraction, not raw shared block access or a
promise about filesystem ordering. Device byte throughput and I/O slots are shared;
submitted replacement records temporarily reserve their full durable size.
