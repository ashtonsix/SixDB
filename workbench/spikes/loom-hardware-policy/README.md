# Loom hardware policy

Can Loom look up known hardware characteristics at startup, keep working on
reasonable defaults after a miss, and learn missing characteristics in the
background?

Ashton's October 8 brief drove this standalone spike. Its implementation now
lives in [Loom](../../../loom/README.md#memory-rings): abstract demands, bounded
inline hint selection, owner-boundary snapshots and shared C++ startup calibration.
The receipts below describe the original spike inputs and their limits; they
do not measure the implemented policy's performance.
Loom also calibrates unresolved fields after partial table hits, preserving
known values and publishing learned fields at a later revision. The original
controller below calibrated only complete context misses.

## Historical Python prototype

[startup.py](startup.py) reuses the existing [memory-characterisation](../memory-characterisation/README.md)
discovery, qualified lookup table, actual probes and inference.

1. Read reported geometry and hardware/environment context, then look up that context.
2. A hit supplies known policy candidates. On a miss, native work starts on
   provisional performance defaults while one bounded calibration job runs.
3. Two independent seeds produce conditional observations. Unknown, censored,
   inconsistent or failed fields keep their defaults.
4. The owner adopts a usable profile at a returned native boundary. Foreground
   work and shutdown do not depend on calibration succeeding.

Duplicate requests for the same context share the calibration job. Background
results remain local and explicitly record foreground overlap; they cannot
silently become an isolated global hardware profile. Context/probe-version
mismatches refuse reuse. This prototype covers one context and has no persistent
learned-profile cache.

[policy.h](policy.h) carries eight approximate extent classes and two access
shapes. Loom maps these sixteen demand codes through a small code LUT and an
`always_inline` switch. Qualified address envelopes remain valid across mapping
changes. The selection rule is provisional; pressure is supplied coarsely.

[native.cpp](native.cpp) drives an actual Loom Ring over held Orbital storage.
Each batch drains before policy changes. A fixed 64-slot bank permits changing
admitted width without fallible replacement allocation. The Python controller
and command pipe demonstrate startup flow, not production startup latency; its
hit path also uses a default-ready handshake before profile adoption.

## Results

| Experiment | Result |
| --- | --- |
| Actual background calibration | Native work continued; both real seed sweeps completed. Training remained unknown and width was censored in one seed, so defaults were retained. |
| Controlled table hit with actual native caller | Calibration was skipped; revision 1 was acknowledged with width 4 and training 3. These policy values were synthetic test inputs. |
| Failed probe | Foreground defaults continued and the owned child was reaped. |
| Exhausted calibration budget | The probe was terminated/reaped; foreground defaults continued. |

Native, full ASan/UBSan with always-UAR, and TSan checks pass. Actual-driver checks
cover profile/pressure changes, stale or mismatched updates, fragmented commands,
saturated stop and EOF. Eighteen controlled startup checks cover invalidation,
partial inference, cancellation, child cleanup and coalescing.

The native switch has no ordinary helper call or stack-pointer use in pinned
V2/Zen5 object checks. Local timing controls were also run, but VM disturbances
prevent a stable overall dispatch ranking. Primary-hardware policy performance
is unmeasured; the startup result does not depend on choosing among those controls.

[The selected receipt](evidence/20261008-receipt.json)
indexes the source capture, journals and checks. The [selected summary](evidence/20261008-summary.json)
keeps the local timing controls and their limits. Measured C++ and startup inputs
are frozen in `build/captures/loom-hardware-policy-20261008` (digest `88ac8e1c…`).
Report helpers and this explanation were later curated without changing those
inputs. Earlier exploratory timings are kept separate.

## Reproduce

The [verified archive](evidence/20261008-artifact.json) preserves the selected
startup journals, checks, codegen, binaries and captured code. Earlier exploratory
timings and ordinary build objects are omitted. Members retain their repository-relative
`build/...` paths; `selection.json` records their hashes. Recover with:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/loom-hardware-policy/evidence/20261008-artifact.json \
  build/recovered/loom-hardware-policy-20261008
```

Run on Linux, prefixing with `orb -m ubuntu` from the Mac. Choose actual geometry
and an allowed CPU; the local example below uses 128-byte lines and CPU 0.

```sh
cmake -S . -B build/loom-hardware-policy-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Release '-DSIXDB_SPIKES=loom-hardware-policy;memory-characterisation'
cmake --build build/loom-hardware-policy-local --target \
  loom_hardware_policy_driver loom_hardware_policy_check memory_characterisation_probe -j2
build/loom-hardware-policy-local/workbench/spikes/loom-hardware-policy/loom_hardware_policy_check
python3 workbench/spikes/loom-hardware-policy/check_driver.py \
  --driver build/loom-hardware-policy-local/workbench/spikes/loom-hardware-policy/loom_hardware_policy_driver \
  --output build/loom-hardware-policy-local/driver-check.json
python3 workbench/spikes/loom-hardware-policy/startup_check.py
python3 workbench/spikes/loom-hardware-policy/startup.py \
  --driver build/loom-hardware-policy-local/workbench/spikes/loom-hardware-policy/loom_hardware_policy_driver \
  --probe build/loom-hardware-policy-local/workbench/spikes/memory-characterisation/memory_characterisation_probe \
  --force-miss --cpu 0 --journal build/loom-hardware-policy-local/startup.json
```

The journal must be a new path. Calibration defaults to 64 MiB sweeps, two seeds,
and a 15-second wall budget, with separate mapping/scratch bounds recorded.
Stop, cancellation, output limits and owned-child reaping are explicit.

For timing, let construction/calibration/tests finish first:

```sh
build/loom-hardware-policy-local/workbench/spikes/loom-hardware-policy/loom_hardware_policy_driver \
  bench --line 128 --page 4096 --width 16 --nodes 65536 --rounds 262144 --reps 9 --cpu 0 \
  > build/loom-hardware-policy-local/bench.jsonl
python3 workbench/spikes/loom-hardware-policy/analyze.py \
  --input build/loom-hardware-policy-local/bench.jsonl --output build/loom-hardware-policy-local/summary.json
python3 workbench/spikes/loom-hardware-policy/codegen.py --output build/loom-hardware-policy-local/codegen
```

The codegen check uses the installed V2 and x86 cross-header paths recorded in its
receipt. Timing includes initial hints, enqueue and complete native drain; table
preparation, storage construction and independent expected results are excluded.
