# Orbital scenario workbench

An executable contention slice of the evolving [Orbital brief](../../../orbital/BRIEF.md).
Use it to inspect preparation, discovery, retained protection, retries and
yielding, and to preserve small counterexamples while the design changes.
[MODEL.md](MODEL.md) owns the assumptions; [findings](FINDINGS.md) explain the
first policy comparison. This is a disposable design instrument, not the
architecture of the eventual simulator or an Orbital implementation.

## Open it

From the repository root, using the Linux Python standard library:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/serve.py
```

Open <http://127.0.0.1:8767>. On Linux directly, omit `orb -m ubuntu`.
`--port 0` chooses a free port and prints it. Stop the foreground server with
Ctrl-C. There are no packages to install or cloud resources to provision.

Choose a scenario, run it, and step through its shard epochs. The display shows
part generations, retained protection, reservations, current wait edges and
the transition trace. Small runs keep every epoch; longer runs thin the display
frames to about 300 while preserving the full event trace in the export.
Policy comparison uses the same workload, horizon and capacity for all four
policies. The global cycle oracle is deliberately idealized.

Start with **Reservation blocks an older contender** and compare policies.
Age priority resolves the two-transaction example but stalls on this
three-transaction example. The [finding](FINDINGS.md#reservation-ownership-can-prevent-the-useful-yield-request)
explains why. **A → B → A discovery** follows the brief's shape. **Slow
participant, local convoy** makes cross-shard protection visible to local work.
The generated workloads mix local and cross-shard work with adjustable hotspot
probability, arrival span and seed.

Expand the editor for per-shard periods/capacities, DAG dependencies, read/write
key sets, weights, arrival times and equal-time shard order. Apply JSON edits
before running. Unsupported fields are errors, so adding a hypothetical loss
or recovery parameter cannot silently pretend to simulate it.

**Save replay** explicitly writes the displayed run, normalized scenario, source
hashes, all events and final state to ignored `build/orbital-scenarios/replays/`
and displays its path. The server reruns and verifies the displayed trace before
saving. Pending edits are not part of that run. Import either that export or a
standalone scenario. No session state is silently written to the repository.
Restart the server after engine/model changes; requests refuse
to label an old loaded engine with new source hashes.

## Headless run and replay

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py \
  --preset reservation --policy older \
  --output build/orbital-scenarios/reservation.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py \
  --replay build/orbital-scenarios/reservation.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py \
  --preset hotspot --compare
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check.py
```

A positional JSON file replaces `--preset`; exported runs are also accepted as
scenario inputs after code changes. `--replay` requires unchanged model sources
and compares the trace digest, summary, stop reason and final state. `--frames`
includes browser-style snapshots. `--steps` bounds shard epochs (default 2,000);
the stop reason distinguishes that limit, the time horizon and full completion.

## What is useful to carry forward

The browser and CLI call the same Python engine. `scenarios.py` authors inputs;
`model.py` holds logical state, reducers and a deterministic event scheduler;
`run.py` supplies replay identity and comparison; `serve.py` and `app.js` project
that state. There is no second browser implementation of the protocol.

The model checks cover the executable slice: stale generations, transitive
invalidation, C2 fencing, all-or-none acquisition, compatible readers,
same-epoch contention, fork/join discovery, replay, and variations of seeded
workloads and equal-time shard order. They do not establish distributed safety,
confluence, eventual progress, serializable object semantics or calibrated
performance. Agreed epochs and atomic invalidation are assumed services.

Git keeps source, small scenarios and the compact comparison that motivates a
finding. Exploratory replays and full traces belong in ignored
`build/orbital-scenarios/`. [compare.py](compare.py) regenerates the retained
comparison from its authored presets and records source/scenario/trace hashes.
Nothing in this spike needs the full historical network experiment archives.
