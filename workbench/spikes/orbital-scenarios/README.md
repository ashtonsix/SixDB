# Orbital scenario workbench

Experiments behind the [current Orbital brief](../../../orbital/BRIEF.md), with
comparisons against the [archived arbitration brief](../../../orbital/stale-drafts/BRIEF-arbitration.md). Start with the
[contention recommendation](CONVERGENCE.md): complete mutation envelopes followed
by fixed-position execution, releasing allocation access before computation.
[Ordered pending versions](PIPELINING.md) preserve dependencies, with explicit
backlog and predicate-waiting costs. It combines the [1,783-run comparison](COMPARISON.md),
[worked SQL/ELT histories](ELT-WORKED.md), [independent audit](ENVELOPE-AUDIT.md),
[prior art](reconsideration/CONVERGENCE-PRIOR.md) and
[extension/epoch composition](reconsideration/COMPOSITION.md).

The [recovery study](recovery/README.md) composes normal-path witness selection
with prefix recovery, terminal handoff, SOS evidence preservation and PITR.
It keeps ordinary admission at 2-of-3 and connects the
[measured network frontier](../cft-commit-latency/network/README.md) to the cost
of changing leaders or witnesses. Its probes use assumed protocol evidence;
they do not implement consensus or the transport.

The earlier comparison's “BRIEF2” is the former compute/promote/renew proposal,
preserved with its original evidence. The new brief selects complete conservative
mutation coverage, not the earlier predicted-footprint or expansion repairs.

The [earlier reconsideration](reconsideration/README.md) supplies 38 SQL/HTAP/ETL
scenarios, prior art and alternatives. Its fixed-position MVTO probe predates
BRIEF2's promotion and renewal rules. The aim remains to remove mechanisms with
explicit tradeoffs, including reconsidering retained C1 protection itself.

The central constraint is [locality of waiting](reconsideration/LOCALITY.md).
The recommendation removes shard-wide transaction-completion barriers while
admitting that conservative target/effect domains can still spread WAN delay.

[Findings](FINDINGS.md) report the earlier component and fold comparisons;
[model boundaries](MODEL.md) explain their assumptions.

The browser and original CLI share the earlier key-protection scheduler; the new
comparison runs headlessly. A separate integer-delta probe explores epoch folds
without per-transaction lock transitions. These are revisable design instruments,
not Orbital's implementation.

## Run the contention comparison

From the repository root, using Linux Python's standard library:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_comparison.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_output_policies.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_fixed_execution.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_fixed_simulation.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/comparison.py --output build/orbital-scenarios/comparison.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/comparison.py --case ordinary_distributed_hot --policies arbitration,snapshot-wait,ordered-writes --full --output build/orbital-scenarios/hot-traces.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/comparison_study.py --output build/orbital-scenarios/comparison-sensitivity.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/discovery_probe.py --full
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/invariant_probe.py --full
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/index_discovery_probe.py --full
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/output_policies.py --output build/orbital-scenarios/output-policies.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/fixed_simulation.py --output build/orbital-scenarios/fixed.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/fixed_fold_comparison.py --output build/orbital-scenarios/fixed-fold.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/arbitration_fold_comparison.py --output build/orbital-scenarios/arbitration-fold.json
```

The shared comparison checks committed observations and effects against a serial
history. It reports successful, failed and pending requests separately; times
and service costs are synthetic inputs. The discovery probe additionally tests
selecting and updating a data-dependent maximum row. Retained evidence and exact
sweep commands are linked from [the comparison](COMPARISON.md). The fixed-position
model is distinct from BRIEF2's fixed-position ablation; its stricter output
coverage requirement, full exchange costs and publication limits are described
in [the model guide](MODEL.md#fixed-position-execution).

The [convergence report](CONVERGENCE.md#evidence-and-reproduction) gives commands
for the new dynamic SQL, authority audit, composition and envelope-locality probes.

## Inspect a scenario

From the repository root, using the Linux Python standard library:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/serve.py
```

Open <http://127.0.0.1:8767>. `--port 0` chooses a free port. Stop with Ctrl-C.
There are no packages to install or cloud resources to provision.

Start with **Independent contention arriving during arbitration** and compare
reservation lifetimes. The second group can start while the first verdict is
pending. **Separate components share a read scope** compares read/write-aware
reservations with scope-only exclusion and introduces a late writer bridging
the components. **Three-transaction preparation cycle** still exposes stale
verdicts when reservations are replaced faster than arbitration can finish.

Step through shard epochs to inspect parts, granted-lock waits, component
reservations and the trace. The editor exposes authored DAGs, read/write scopes,
delays, capacities and equal-time shard order. Unsupported fields are errors.
The seeded workloads retain completion and pending counts alongside latency;
unfinished work must not disappear from the comparison.

**Save replay** verifies the displayed run and writes it to ignored
`build/orbital-scenarios/replays/`. Pending edits are not silently saved. Large
runs thin browser frames to about 300 while retaining the full trace in exports.
Restart the server after executable-source changes; a running engine refuses
to claim source bytes it has not loaded.

## Headless probes and checks

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py --preset independent --compare
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py --preset overlap --output build/orbital-scenarios/overlap.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/run.py --replay build/orbital-scenarios/overlap.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/folds.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/study.py --output build/orbital-scenarios/component-study.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_batch.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_folds.py
```

A positional JSON file replaces `--preset`; exports can supply scenarios for new
comparisons. Exact replay requires the same executable sources and compares the
trace, summary, stop reason and final state. Documentation hashes are recorded
as context, separately from executable identity. `--frames` includes snapshots;
`--steps` bounds ordinary shard epochs (default 2,000).

Current checks cover dependencies, stale work, C2 fencing, component lifetimes,
compatible readers, late bridges, repeatability and the finite fold schedules.
They do not preserve historical policies or claim general confluence, liveness,
serializability or distributed safety. Complete collection, atomic verdicts and
uncharged retry work remain major abstractions.

[History](HISTORY.md) keeps the useful earlier findings and their source commits.
The old individual-reservation policies, instantaneous cycle oracle and frozen
trace regressions are retired from the active model; Git can reproduce them.
