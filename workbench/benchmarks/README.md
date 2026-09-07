# Benchmarks

Benchmark programs and helpers for broad transactional, analytical, and mixed
workloads, plus focused measurements of individual mechanisms.

Google Benchmark will provide microbenchmark machinery. The usual convention
is to pin CPUs and run cases/repetitions sequentially, without interleaving,
so experiments can isolate performance across cache-residence and resident-set
size tiers. The workload controls footprint and access pattern; those tiers
need to be established on the measured machine.

Calico's [Google Benchmark pilot](../../../calico/qhash/bench/GOOGLE.md) and
[affinity helper](../../../calico/tools/fleet/recipes/lib/microbench.sh) are
initial references. Harness details and result-recording process are pending.
