# First SeriesPack x86 hardware checks — 2026-09-10

The corrected checkpoint passes the aggregate on both machines in all three
feature profiles. These are correctness and build observations; throughput
measurements are separate.

| Hardware | AVX2 | AVX-512 BW only | BW + VBMI/VBMI2/GFNI |
| --- | --- | --- | --- |
| Zen 5, c8a.large | Pass | Pass | Pass |
| Granite Rapids, c8i.large | Pass | Pass | Pass |

The full profile exercised scalar, AVX2 and AVX-512 public paths, including the
206-description oracle and native composition. BW-only exercised the available
BW body kernels and AVX2 public dispatch; it does not establish a separate BW-only
public implementation. Builds used pinned Clang 21.1.8, `-O2 -g`, one build job,
and CPU 0 affinity on 4 GiB workers.

Both machines initially failed all profiles in `bound_construction`, comparing
the second head plane. The focused diagnostic on unchanged native code exposed
`k=9, h=8, local, n=8, input=u8`: the first head byte was `0x80` rather than `0x00`.
The implementation owner corrected the x86 byte shift's missing per-byte mask
and expanded the independent oracle to cover input types narrower than K.

The fixed capture overlays exactly three Ikea files onto the earlier frozen
source: `native.cpp`, `physical_operations.cpp`, and `operations.cpp`. Runner/docs
changes are recorded separately. The concurrent NEON header optimization was
excluded; this corrected capture is for x86. [checks.json](checks.json) records
the source digests, changed-file hashes, per-profile outcomes and build resources.

Reuse preserved the toolchain and compiled libraries: the diagnostic target
rebuilt in 1.82 seconds; corrected aggregate builds/checks took 43–78 seconds on
Zen 5 and 53–93 seconds on GNR. These are incremental build-plus-check observations,
not isolated compiler benchmarks. The first full Zen build peaked at 2.04 GiB
process RSS, supporting the choice of a 4 GiB worker.

The five standalone artifact references retain the failed, diagnostic and passing
source/log/binary bundles in S3. For example, recover the corrected Zen run with
`python3 workbench/tools/artifacts.py fetch
workbench/benchmarks/seriespack/evidence/hardware-checks/zen5-corrected.artifact.json
build/recovered/seriespack-zen5-corrected`. Full disassemblies and raw logs stay
outside Git; see the [recovery guide](../../../../tools/artifacts.md).
