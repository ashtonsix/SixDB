# One-image predicate control

This bounded study is complete. It follows the seven predicate-affected x86
exceptions in the [copied-projection native comparison](PROJECTION-NATIVE.md).
It does not replace those separate-process observations or infer that fewer
instructions made their losses inconsequential.

The [checked-point placement experiment](../../executable-placement/evidence/checked-point-layout-20260911/summary.md)
changed surrounding layout while preserving the executed candidate bytes and
substantially reduced a control regression. That is an intervention we can
learn from, rather than a general alignment policy. The
[Stream control](../evidence/interface-cohesion-v2-20261023/stream/README.md)
reached parity by slowing the old executable; it did not recover candidate
performance. The Bec controls also reversed rankings with equal hot bytes and
addresses. These findings make a process/image control more useful here than
another static instruction count or an untested fetch-boundary explanation.

[The driver](predicate-panel.cpp) links the literal frozen `runtime.cpp`,
`extra.cpp` and `extra_tuple_2.cpp` three times under distinct namespaces. Each
arm gets its own nine-source TuplePack library and mutable fixture state. The
arms are final-v5 before, final-v5 copied-projection after, and a separately
emitted second copy of before. No Ikea header or maintained consumer body is
edited. No LTO or linker identical-code folding is requested. The original
byte oracles, request trace, full/sparse masks, 512-by-eight own-arm warmup,
dependent and independent-eight loops, and result consumption remain in their
original source. Only the outer order of arm calls changes.

The selected op2 translation unit has 48 existing commands per arm: tight
widths five/eight and strided width five, four routes, two activity masks and
two dependency modes. This includes both losing AVX2 inline/dependent cases,
the losing AVX-512 width-five stage case, its width-eight stage counterexample,
and shared/strided controls. Equality and `less` are outside this bounded panel.
Preparation and ABI costs remain separate from these retained-loop commands.

The prior independent-process observations motivating this subset remain:

| Profile / full tight op2 command | Old ns/window | New ns/window | Paired new/old | Paired duplicate/old |
| --- | ---: | ---: | ---: | ---: |
| AVX2 width5 inline/dependent | 5.0425 | 5.4234 | 1.0761 | 0.9981 |
| AVX2 width8 inline/dependent | 5.1019 | 5.4239 | 1.0642 | 0.9982 |
| AVX-512 width5 stage/independent8 | 3.3484 | 3.5728 | 1.0670 | 0.9995 |
| AVX-512 width8 stage/independent8 | 3.7970 | 3.5699 | 0.9410 | 0.9996 |
| AVX-512 width8 shared/independent8 | 4.7022 | 5.3589 | 1.1396 | 1.1382 |

Absolute columns are medians of five process trials; paired columns are
medians of five corresponding ratios. The complete samples remain in the
projection native receipt, whose canonical CSV SHA256 is
`0c2a6701e26318231adc09b6e3e73e06919de85bb8af2c95ec874792c4837744`.
The new panel must not silently substitute its results for those observations.

The old-code duplicate has different function and state addresses in the same
image. It is a separately emitted equivalent code copy, **not** an identical
ELF duplicate. The earlier panel's identical-binary duplicate remains a
different control. All three arms share one loader/process environment here;
this cannot equalize their instruction/data addresses, predictor history or
residence. Five counterbalanced trials occur within one process per target,
not five independent process launches.

[The audit](predicate-panel-audit.py) checks symbol-sized bodies against actual
ELF bytes. Before timing it requires the selected full-tight leaf/loop paths
to match the corresponding original native ELF, and old/duplicate selected
complete functions to match. It admits only the explicit namespace spelling
correspondence and verified PC-relative code/data locations. Actual registers,
immediates, other memory displacements, comparisons and payload bytes survive.
Whole-function differences outside those paths are retained. Unknown control
flow, altered selected work, duplicate mismatch or oracle failure stops the
panel before timing.

[The controller](predicate-panel.py) retains source maps, commands, consumed
dependency hashes, objects, ELFs, maps and disassembly. All target builds,
identity checks and runtime oracles finish before either profile is timed.
The original 300,000 iterations and one repeat are used. The driver applies
the same five before/after/duplicate orders as the earlier panel and preserves
every command in each selected translation unit. The
[reader](predicate-panel-results.py) validates all 48 cells and five trials per
arm, retains absolute ns/window and paired ratios with sample dispersion, and
does not pool routes or divide by the duplicate ratio.

The question is whether the old/new separation survives common process/image
context and own-arm conditioning while an equivalent old-code copy remains
stable. A persistent separation with a stable duplicate leaves the cause within
the complete caller unresolved. A collapsed separation would implicate the
changed experimental context, without identifying a particular placement
mechanism. A nondiscriminating duplicate or inconsistent selected cases gives
no attribution. This bounded panel ends after native qualification and evidence
retention; no subsequent crossover or production change is part of it.
Absolute times and the original losses remain reported in every outcome.

Local identity preparation uses the pinned Linux compiler, configured Release
`-O3 -g`/warnings/strict-FP flags and the actual no-vectorization treatment of
the filter source. The x86 profiles are AVX2/Zen 5 and AVX-512/Zen 5. Local
compiles run serially on the coordinated CPU8 slot; timing belongs to one
separate Zen worker after source/identity review. This adds 37 bounded objects
per profile and no new 360-cell tournament.

```sh
orb -m ubuntu python3 \
  workbench/spikes/ikea-composition/handle-boundaries/predicate-panel.py stage \
  --source-pair build/ikea-cohesion-v2/predicate-projection-final-v5-sources \
  --original-results build/workers/20261023T045513Z-1f952ffa/results \
  --output build/ikea-cohesion-v2/predicate-panel-inputs

orb -m ubuntu taskset -c 8 python3 \
  workbench/spikes/ikea-composition/handle-boundaries/predicate-panel.py build \
  --inputs build/ikea-cohesion-v2/predicate-panel-inputs \
  --output build/ikea-cohesion-v2/predicate-panel-build --jobs 1
```

The staged source references and managed native capture are immutable. A
source or runner correction requires a fresh stage/capture identity; historical
projection controls and their receipts remain unchanged.

## Observed result

The single on-demand Zen worker `20261023T113248Z-8a6044a3` completed both
profiles. All 74 objects built. All 66 selected symbol-sized clone functions
match their corresponding original native functions after the explicit
namespace and verified PC-reference substitutions, including registers,
instruction order, immediates and addressed payload identities. All 48 selected
hot-path comparisons and 22 old/duplicate complete-function comparisons pass.
These are normalized instruction/payload comparisons; code addresses and raw
relative encodings differ. Both runtime oracle/checksum preflights passed
before any timing began.

The five within-process trial groups retain 48 commands per arm/profile:

| Full tight op2 command | Old ns/window | New ns/window | Paired new/old | Paired duplicate/old |
| --- | ---: | ---: | ---: | ---: |
| AVX2 width5 inline/dependent | 5.0343 | 5.4157 | 1.0774 | 1.0137 |
| AVX2 width8 inline/dependent | 5.0174 | 5.4227 | 1.0820 | 1.0001 |
| AVX-512 width5 stage/independent8 | 3.6203 | 3.8462 | 1.0609 | 0.9994 |
| AVX-512 width8 stage/independent8 | 3.3984 | 3.4172 | 1.0055 | 1.0034 |
| AVX-512 width8 shared/independent8 | 4.7459 | 4.9676 | 1.0455 | 1.0470 |

The targeted AVX2 dependent losses persist in every trial: width-five ratios
span 1.0686–1.0959 and width-eight ratios 1.0702–1.0876. The AVX-512 width-five
stage ratios span 1.0233–1.0637. Equivalent old-code duplicates are much closer
to their old arms for these cases. Common process context and the retained
own-arm conditioning therefore do not remove these selected losses.

Other controls change. The prior AVX-512 width-eight stage winner becomes
near-flat. In AVX2 width-eight stage, the separately emitted old-code duplicate
itself improves 5.66%, close to the new arm's 6.11% gain. The AVX-512 shared
route's new/old separation tracks the duplicate/old separation. Those outcomes
keep code/state/context sensitivity visible without establishing a specific
placement explanation or supplying a duplicate correction factor.

Four of the 96 case medians exceed 5% new/old: the two AVX2 dependent callers,
AVX-512 width-five stage, and AVX-512 width-five inline/dependent. The last
gives 5.0001→5.2641 ns/window, paired 1.0556 with duplicate 0.9980. Every case,
absolute sample and paired-ratio dispersion remains in the
[compact receipt](../evidence/interface-cohesion-v2-20261023/predicate-panel.json).
No route or gain is pooled to offset a loss.

The panel establishes that the selected slow paths survive this control; it
does not establish their microarchitectural cause. The complete native bodies
retain the previously inspected lower instruction counts, yet timing remains
worse. Dependency scheduling, prediction, residence and linked placement are
unresolved aspects of the complete caller. No PMU or quantitative pipeline
model was collected here. Equality/less exceptions and other source epochs
remain outside this op2 panel.

The durable result artifact is
`s3://calico-fleet-artifacts/sixdb/artifacts/sha256/9d39552dd9580a2b82436bf6d9b0236a3eeea1a8893e50453ef96fe6fc96f569.tar.gz`
(69,979,764 bytes, 376 files). It retains the source archive, all objects,
commands/dependencies, maps, ELFs, disassembly, identity receipt and raw trials.
Recovery is `worker.py fetch 20261023T113248Z-8a6044a3 --full`. AWS reports the
worker terminated; no local compiler or timed process remains. This panel is
finished, with attribution handed over as an unresolved problem.
