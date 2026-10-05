# Native object composition evidence

Job `20261002T183452Z-44040228`, October 2, 2026. Fresh Spot c8a.large in
us-east-1b, AMD EPYC 9R45, two visible cores, Clang 21.1.8, Linux 7.0.0-1012-aws,
4 KiB pages. [compose.sh](../../compose.sh) pins checks and timings to CPU 0,
builds for x86-64-v4/Zen5, and performs no cross-AZ or regional traffic. The
script completed successfully in 12.37 seconds; the worker is terminated.

The selection keeps the entire small comparison: three strategies, four
histories and cache sizes zero/eight, plus a native read floor. There are 150
trial rows across [forward](composition-forward.csv) and
[reverse](composition-reverse.csv) order. Each trial sums measured operation
durations for at least 20 ms; its time is a mean, not a latency percentile.
Initial preparation is untimed. Process fault counters also include that setup.

[composition-summary.csv](composition-summary.csv) contains medians, ranges,
per-operation work and absolute capacity. [summary-checks.json](summary-checks.json)
records coverage and exact independent checksum/work checks. Regenerate it from
the retained raw inputs:

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/orbital-objects/evidence/zen5-composition \
  build/reports/object-composition
```

[composition-checks.jsonl](composition-checks.jsonl) records 4,677 successful
byte/lifetime checks. [linux-views.jsonl](linux-views.jsonl) and
[linux-capabilities.jsonl](linux-capabilities.jsonl) preserve the narrower native
mapping observations on this host. Optional feature advertisement remains
distinct from exercised fault handling.

[host.json](host.json), [job.json](job.json), [compiler.txt](compiler.txt),
[lscpu.txt](lscpu.txt) and [source-manifest.json](source-manifest.json) identify
the host, configuration and captured sources. [provenance.json](provenance.json)
hashes selected bytes; [artifact.json](artifact.json) recovers the full verified
bundle, including source capture, build/setup logs and omitted diagnostics.
The measured native sources and runner still match the capture; documentation
was completed afterward.

Interpretation and limits belong in [FINDINGS.md](../../FINDINGS.md). In particular,
the supplied snapshot fixture does not measure durable publication or process
loss, and the local socket path does not establish remote or zero-copy costs.
