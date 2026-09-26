# Earlier contention experiments

The active model follows the brief revised in `31dba60`. Earlier evidence remains
useful with its original assumptions; it is not a regression contract for later
models. The compact JSON records retain their original source and trace hashes.

| Source commit | Retained evidence | Useful observation |
| --- | --- | --- |
| `4b509ee` | [Initial comparison](evidence/comparison.json), 28 cases | In the three-transaction fixture, an individual reservation prevented the contender capable of requesting useful yielding from doing so. Completion-only p99 concealed unfinished work. |
| `825a566` | [Component comparison](evidence/batch-comparison.json), 45 cases and nine graph probes | Component ownership removed that obstacle. Replacing reservations every 500 µs with 1,200 µs arbitration made every returned verdict stale; holding them completed the fixture. |

The extension also found a 72-transaction component among 80 transactions, a
large cost for remote arbitration of local work, and an eager-retry case that
discarded more preparation and completed less. Its graph probes distinguished
cliques, chains and stars: a 256-vertex star selected only its oldest center under
the anchored policy, while the chain selected 128. Pairwise expansion of 256
writers produced 65,280 directed arcs from only 256 transaction/key incidences.

Those are synthetic observations, not measured wire sizes or performance
predictions. The extension assumed complete multishard snapshots, atomic verdicts
and no CPU cost for collection. Its global hold, read/read reservation merging,
oldest anchor and completion-based reservation release were experimental choices.
The current model revisits lifetime, compatibility and release. Selection still
uses the oldest anchor; alternatives remain open.

## Reproduce historical results

Use a detached checkout rather than retaining duplicate legacy implementations:

```sh
orb -m ubuntu git worktree add --detach /tmp/orbital-history 825a566
orb -m ubuntu sh -c 'cd /tmp/orbital-history && python3 workbench/spikes/orbital-scenarios/study_batch.py --output build/historical-batch.json'
```

For the initial study, use commit `4b509ee` and its `compare.py --output PATH`.
Each checkout also contains its original findings, model description and
`run.py --preset reservation --policy ...` for inspecting an individual case.
No current check requires reproducing the 28 initial trace identities. Current
replay checks require repeatability only under the current executable sources.
