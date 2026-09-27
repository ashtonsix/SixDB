# TLC on resumable workers

[Checkpoint usage](../checkpoints.md) owns the worker commands and guarantees.
`run.sh` installs a headless Java 21 JDK if needed, then runs ordinary standalone
breadth-first TLC. Pass the model/configuration and TLC options after `--` in the
worker command. `TLC_JAVA_OPTS` sets JVM options, for example
`--env 'TLC_JAVA_OPTS=-Xmx8g -XX:+UseParallelGC'`. The default uses 70% of available
memory. Set `-workers` explicitly; its value is preserved across recovery.
Deadlock checking remains enabled unless explicitly disabled by the caller.

For current Orbital working checks, start with a modest Zen 5 Spot instance and
four TLC workers. The [September hardware pilot](evidence/20260927-workers/README.md)
compares completed graphs on C8a and C9g, including prices and limits. C8a.2xlarge
was faster and cheaper at the measured Spot quotes; M8a.2xlarge buys twice the RAM
with the same core count when needed, but was not measured. Large final graphs
need their own memory, metadata and scaling evidence before choosing a large host.

The [source reference](source.json) pins exact jar bytes, retained in the existing
S3 bucket and cached by the shared input helper. `TLC_JAR` can point to a local
copy with the same hash. Models, configs and custom modules should be in the
captured repository. `invocation.json` records jar identity, Java version, JVM
options, TLC arguments, fingerprint polynomial and seed; mismatches reject recovery.
Generated trace files and `tlc.log` remain in results, including nonzero outcomes.

For local use with Java 21 already installed:

```sh
export SIXDB_CHECKPOINT_STATE="$PWD/build/tlc/local/state"
export SIXDB_RESULTS="$PWD/build/tlc/local/results"
export SIXDB_DATA_CACHE="$PWD/build/datasets"
bash workbench/tools/tlc/run.sh -workers 1 -config path/to/Model.cfg path/to/Model.tla
```

Use a fresh local state/results directory for each attempt. Ordinary worker jobs
provide those directories automatically. The adapter controls `-tool`, `-metadir`,
`-checkpoint`, `-recover`, `-fp` and `-seed`. Distributed TLC, simulation and DFID
are outside this recovery recipe.

## Why this pin and snapshot procedure

The upstream `v1.8.0` asset changes in place. The tested build is **8f4bc8b**, dated
September 25, 2026; the hash in `source.json` identifies it, not the release label.
An older July build, **227f61b**, failed a fair-liveness recovery test with an
internal exception. Its checkpoint routine resumed exploration before recording
the intern table and liveness graph. The selected build has the
[corrected checkpoint order](https://github.com/tlaplus/tlaplus/blob/8f4bc8b73ad1202774a6bf70143436f8ba50aab0/tlatools/org.lamport.tlatools/src/tlc2/tool/ModelChecker.java).

`checkpoint.sh` requests a checkpoint through a local JVM attach/JMX connection,
waits for TLC's checkpoint-completed event, pauses all JVM threads, and copies
the complete metadata tree. No network management port is opened. TLC's own
periodic checkpoint timer is disabled so another generation cannot race the copy.
The TLC coordinator polls at five-second intervals so a notice need not wait
for the default minute-long progress interval. The original JVM resumes before upload; a notice still causes the worker to stop
it after the bounded save attempt.

There is one version-specific normalization of the **copy**: truncate each
liveness graph's `nodes_` and `ptrs_` files to the offsets TLC wrote in
`dgraph_*.chkpt`. This discards only post-checkpoint data. The pinned
[graph recovery implementation](https://github.com/tlaplus/tlaplus/blob/8f4bc8b73ad1202774a6bf70143436f8ba50aab0/tlatools/org.lamport.tlatools/src/tlc2/tool/liveness/AbstractDiskGraph.java)
seeks back without truncating; a later full graph scan otherwise consumed stale
tail bytes and failed with EOF in the recovery test. The live files are unchanged.
Revalidate this adapter when changing the TLC build; do not carry this format
assumption to another version implicitly.

## Exercised recovery

`check.py worker_checkpoint` injects producer/upload failures, corruption and
interruption ordering offline. With a local copy of the pinned jar:

```sh
TLC_TEST_JAR=/absolute/path/to/tla2tools.jar python3 workbench/tools/check.py tlc
```

The TLC check compares uninterrupted and killed/restored runs of a finite grid:
a passing invariant and weak-fairness eventual-completion property, a known
invariant violation, and a known temporal violation. It publishes/restores through
the checkpoint format using an in-memory S3 substitute, removes original local
state, then recovers into another directory. Passing and temporal-failure cases
have 1,002,001 expected distinct states. Generated-state counters can differ
across recovery; outcomes and reachable states are the useful comparison.

These are recovery checks, not a claim about every TLC mode or model. Checkpoints
do not make the middle of a long transition, initial-state enumeration or final
liveness search interruptible: TLC must reach a checkpoint opportunity. Large
state, many workers, different liveness tableaux and real interruption/upload
latencies still need workload-sized pilots. The offline check downloads nothing
and skips unless `TLC_TEST_JAR` is supplied.
