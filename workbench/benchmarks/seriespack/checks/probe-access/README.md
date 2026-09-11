# Immediate-predecessor adapter checks

This checks the Workbench LocalPack/ScanPack and width-56 adapters used by the
bulk and random-access controls. It has an independent bit-map/value oracle,
three-cell arrays, protected exact ends, output canaries and sparse mappings
whose logical indices exceed 2³². The sparse mappings reserve address space;
they do not allocate a corresponding materialized array.

From the repository root on Linux, using the same ISA flags as the measured
control (the example selects baseline NEON):

```sh
mkdir -p build/seriespack-predecessor-access
clang++-21 -std=c++23 -O2 -g0 -Wall -Wextra -Werror -march=armv8-a \
  -Iikea/include \
  workbench/benchmarks/seriespack/checks/probe-access/check.cpp \
  workbench/benchmarks/seriespack/probe_controls.cpp \
  -o build/seriespack-predecessor-access/check
build/seriespack-predecessor-access/check
```

The initial NEON and AVX2 runs each passed 15,360 point and 960 get16 cases.
The full AVX-512 adapter also compiled and its native widening bridges were
inspected, but those observations do not substitute for executing this check
under that profile. Ordinary benchmark preflight separately checks values on
each measured machine.
