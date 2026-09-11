# Heterogeneous bitsets and integer metadata

Experimental evidence owned by the [composition investigation](../../README.md).
Use its [current design](../../design.md) for authoring direction.

Opened and closed 2026-09-10 after the [integer probe](../ikea-integers/README.md).
Start with the [closing assessment](closing.md) for the implementation direction
and evidence to carry forward into Ikea. The completed code and experiments
remain reproducible here.

Can one authored range operation preserve logical block identity, variable-length
body framing and owner lifetimes while substituting integer metadata layouts
and compilation boundaries? Performance and composition are joint requirements.
This experiment does not select a production interface.

The [whole-bitset operations extension](operations/README.md) now adds a scalar
conversion analyser and masked union/intersection over two 65,536-position
bitsets. That campaign covers independent input strategies,
admit-once sources, sparse ordinals, complete versus selected-only output, and
the difference between two decoder inputs and two output slices.

The first operation reads a requested range from a collection of up to 256
headless Bec256 blocks, intersects them with the corresponding blocks of a
separate plain query bitset, and counts matches. Decoded content is necessary;
there is no compressed decode–operate–encode round trip. All layouts reference
the **same dense BEC body allocation**.

| Metadata | Physical representation | Bytes at 256 records |
| --- | --- | ---: |
| `direct32` | Population9, length6 and absolute offset14 in one 32-bit record | 1024 |
| `local16` | 32-byte packets: offset checkpoint, sixteen population9 values and LocalPack6 lengths | 512 |
| `scan128` | Separate checkpoints, local population9 packets and ScanPack6 lengths in intact 32-byte stripes | 512 |

The packed layouts reconstruct starts from preceding lengths within a sixteen-
record group. Population9 uses an eight-bit body plus a LocalPack1 low tail.
The [contract and composition notes](composition.md) explain native metadata16
feeding BEC pairs, range starts, admission, readable suffixes and live reuse.

## What the first campaign established

- One [authored range loop](authoring.h) handles all six layout/region choices,
  including nonzero starts, checkpoint crossings and odd final blocks. It
  preserves native metadata state rather than materializing a descriptor array.
- Packed metadata halves directory bytes but costs **6–14%** against the direct
  layout for fully inlined, repeated full-range reads across these datasets and
  three machines. This is not yet a cold random-access result.
- Splitting at the BEC decode-and-count region costs about **5–6% on Zen 5,
  0–2.5% on Granite Rapids, and is roughly neutral on Neoverse V2** for packed
  metadata in full-range reads. Ordinary calls force preservation of the live
  64-byte metadata frame; full inlining also has register-pressure costs.
- Legal child locality does not establish parent locality. A fresh metadata
  point read touches one line in `local16`, but up to **five** in `scan128`.
  [Locality](locality.md) distinguishes exact byte geometry from measured traffic.

[Measurements](measurements.md) gives the workload, limitations, retained runs
and timing tables. [Boundary inspection](notes/boundaries.md) checks the actual
executables: the initial nominal-inline comparison was compiler-outlined and
was replaced by explicit curated inlining before drawing these conclusions.

## Work here

Run from the Linux repository root; prefix commands with `orb -m ubuntu` from
the Mac. The pinned toolchain and flags follow [BUILDING](../../../../../BUILDING.md).

```sh
# Captured provider + combined checks, with ASan/UBSan:
python3 workbench/spikes/ikea-composition/probes/ikea-heterogeneous/run.py --sanitize
# Small synthetic timing smoke; no performance conclusion:
python3 workbench/spikes/ikea-composition/probes/ikea-heterogeneous/run.py --synthetic -- --quick
# Full hardware campaign; use the same name for machine and target:
python3 workbench/tools/worker.py run workbench/spikes/ikea-composition/probes/ikea-heterogeneous/cloud.sh \
  --machine zen5 --idle-seconds 0 -- --target zen5 -- --pmu
```

Other measured targets are `granite-rapids` and `neoverse-v2`. The full runner
prepares a deterministic whole-window sample from the shared RealRoaring input;
it preserves empty cells inside each sampled 65,536-position window. Local
incremental builds can select `ikea-composition` and
build `ikea_heterogeneous_check` or `ikea_heterogeneous_bench`. Select the provider
checks too when changing their live shared implementation.

The operations extension compares point lookup with metadata16 refill and
separates source admission from binding. These changes have not been backported
to the first range-count comparison. Cold point/range access remains unmeasured;
the repeated operations do not establish cold access performance. Neither
campaign declares a universal cursor, obligation-tag system or compression
policy.
