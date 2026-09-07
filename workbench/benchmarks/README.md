# Benchmarks

Durable shared workloads for transactional, analytical, and mixed execution,
plus shared measurement machinery. Benchmarks for a particular investigation
belong in its [spike](../spikes/README.md).

Google Benchmark provides microbenchmark machinery. The usual convention
is to pin CPUs and run cases/repetitions sequentially, without interleaving,
so experiments can isolate performance across cache-residence and resident-set
size tiers. The workload controls footprint and access pattern; those tiers
need to be established on the measured machine.

Calico's [Google Benchmark pilot](../../../calico/qhash/bench/GOOGLE.md) and
[affinity helper](../../../calico/tools/fleet/recipes/lib/microbench.sh) are
initial references.

The [aggregate delta probe](../spikes/aggregate-maintenance/README.md) is the
first working use: pinned Google Benchmark 1.9.4, affinity checked in the
executable, sequential repetitions, raw JSON, and separate logical accounting.
Its [runner](../spikes/aggregate-maintenance/run.py) is a concrete starting
point; a general benchmark API and cache-residence harness remain open.
