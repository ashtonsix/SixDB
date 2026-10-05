# Repeated view updates, October 5, 2026

Job `20261005T113317Z-87bce6de`: Spot c8a.large, Zen5 AMD EPYC 9R45,
Clang 21.1.8, 4 KiB pages, pinned to CPU 0. The instance terminated after
collection. [Artifact](artifact.json) retains the executed sources and binaries.

The [probe](../../pattern_probe.cpp) repeats begin, borrow, store 64 bytes,
close, stage 64 bytes, seal and install the next revision. It prepares only
page 0 of 4 KiB, 64 KiB and 1 MiB parts. Each part starts with an eager private
cache outside timing. Every iteration checks the latest bytes against an
independently calculated prefix. Checks for the full composed history run too.

Copy/COW/take are crossed with direct mappings or a pool retaining free homes.
All paths are trusted, with no forced hardware protection. The summary uses
six trial means per cell, 60 iterations each: three forward and three reverse
method orders. Phase clocks and the pooled path's release hint are included.
These are not latency percentiles.

| Part | Method / retained free homes | Acquisition | Seal/install | Whole cycle |
| --- | --- | ---: | ---: | ---: |
| 4 KiB | Take / off | 64 ns | 1.953 µs | 2.191 µs |
| 4 KiB | Copy / off | 2.191 µs | 3.292 µs | 5.648 µs |
| 4 KiB | Copy / on | 3.033 µs | 1.956 µs | 5.190 µs |
| 64 KiB | Take / off | 75 ns | 10.007 µs | 10.260 µs |
| 1 MiB | Take / off | 84 ns | 218.450 µs | 218.759 µs |
| 1 MiB | Copy / off | 2.929 µs | 220.706 µs | 223.833 µs |

Taking retains the prepared binding, including across publication. Free-home
pooling avoids repeated fresh allocation and some teardown, but restoring raw
write access and establishing the next view adds acquisition work. New copy/COW
views need one extra home in the first iteration; the following 59 reuse it.
Take needs neither a new home nor restoration within these cycles.
Seal/install includes retiring the previous cached image for copy/COW. Its
reduction with pooling therefore does not demonstrate faster source sealing alone.

Every iteration stages **64 bytes**, but the fixture then reads the complete
source and writes a new complete sealed memfd. Thus a 1 MiB part entails
1 MiB source reading and 1 MiB new material writing per update. The comparison
exposes this amplification; it does not propose full snapshots for production.
Retaining unchanged material while recording changed ranges/pages is the more
important opportunity than tuning a few nanoseconds of acquisition here.

No disk durability, journal decision, network transfer, compression, compaction,
concurrent owner mutations or memory pressure is timed. Source/history remains
resident. The fixture retains every source revision, and full replay and byte
comparison are research validation. These costs are not end-to-end Orbital
transaction costs or rankings of optimized copying/fault-handling mechanisms.

[Comparisons](comparisons.csv) retain all 18 cells. All six raw CSVs and the five
native probe outcomes are selected; source/build/host context remains recoverable
in the worker archive. [Provenance](provenance.json) records selected hashes.
All 22 native source and runner files matched the capture when checked.

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/orbital-views/evidence/patterns build/reports/orbital-view-patterns
```

The [summary](../../pattern-summary.py) independently checks cell coverage,
staged/source/material work, copy/mapping counters and pool reuse/restoration
before combining means. Local AArch64 native and ASan/UBSan checks pass too;
local exploratory timings are kept in ignored storage.
