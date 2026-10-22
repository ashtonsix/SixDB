# Recover the warm Zen 5 cohort

Worker `20261022T224742Z-3eb630aa` completed on demand. The selection retains
all 3840 medians (1920 per ISA), four exact duplicate pairs per applicable case,
check/command/ELF identities and ISA/host records. Raw repetitions, capacity
passes and binaries remain in the worker's verified archive; they are not
discarded losing cases. `peak_nonidle_bytes=0` in plain records means unavailable.

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/ikea-frame/lifecycle/evidence/native-zen5 \
  build/recovered/frame-owner-zen5
```

The recovered `source.tar.gz` is a repository-relative source tree; its
`source-manifest.json` identifies all 3470 files. Source digest:
`a525096b5b8f323e83439f08039c33c896ffe6801375f3e0264c856b0e131e48`.
Use those frozen bytes in an isolated checkout for a new worker launch, rather
than today's changing Frame/Ikea files. The runner is
`workbench/spikes/ikea-frame/lifecycle/native.sh`, with `native.py` and
`native-inputs.json` beside it. It needs worker-provided `SIXDB_RESULTS`, CPU,
source-commit and build-job variables. The job used:

```sh
orb -m ubuntu python3 workbench/tools/worker.py run \
  workbench/spikes/ikea-frame/lifecycle/native.sh --source FROZEN_CHECKOUT \
  --machine zen5 --instance-type c8a.xlarge --capacity on-demand \
  --deadline 2400 --max-age 5400 --idle-seconds 0 --detach
```

Both measured executables are
archived at `avx2/binaries/frame_phase_measure` and
`avx512/binaries/frame_phase_measure`. Compare SHA256 with the respective
`identity.json` before replay/disassembly.

The CSV rows retain source-specific complete phase medians, not individual
request latency. Group duplicate arms 1/10,4/11,13/15,16/17 by
input/trace/coverage/mix/pattern/mask/retain_old. Compare their `total_ns` and
logical work separately. Native controls are arms 0/1/9; representative inline,
override-debt and final-fold programs are 16/13/14. The source-qualified scorecard
and limits are [ZEN5.md](../../ZEN5.md).
