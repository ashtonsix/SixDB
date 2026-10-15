# Native hash-mask control qualification

This retains the frozen `ordinary`, `split`, `paired` and `and` source controls
for K40 sets and W2 maps, at all 18 geometry arms and both reference
cardinalities. Composed 16-byte selectors remain fixed. The original
[cohort.json](cohort.json) records submission and source hashes;
[native.json](native.json) records completed qualification and recovery
identities for [Zen5](../mask-zen5/context.json) and
[Granite](../mask-granite/context.json).

All sixteen timed ELFs pass 2,304 source-resolved regions: 1,728 read consumers
and 576 effective update CFGs, without truncation. Each ELF matches its timed
process receipt and archived disassembly (excluding only objdump's input-path
prologue). The exact mask target and all three required macros are verified.
Captured source hashes, including `hash.h` and `hash_mask.h`, match the
submission receipt and manifest. All four native mask correctness targets
passed on both hosts.

The frozen report replays exactly: 25,920 timed rows, 576 fresh processes and
2,592 comparisons per host. Every comparison, loss and pass median remains in
the per-host `summary.csv`, alongside raw rows and process receipts. Every
phase's requested bytes, owned capacity, metadata, geometry and tier population
agree across all four variants. Ordinary and exact-key Abseil controls remain
separate in each binary, including their observed process variation.

Source manifest digest:
`7dd5c574e1e3b6860b58bff783924aafc0a99c796934b002fe3a46e280c83a4f`.
Source archive SHA:
`3719f57492207d5317bd5ae1b10dca0bc5dcca2e83c3d8da8c61f950a20b12e6`.
Driver SHA:
`e8b82cd8f891dd76cc560c33f4ea854c18e69476a1bdad426b69e48f3fc5501a`.

Selected K40/W2 arm0/arm1 hit and serial CFGs are retained per host. The
changed-mask selected hit loops moved into outlined timing wrappers, which
were resolved by their actual source callsites; their timed CFGs have no calls.
That observation does not isolate an instruction's hardware cost. Source
interventions can also change register allocation, branches and placement.
The later [targeted instruction review](../mask-diagnostic/review-index.json)
records selected primary paths, count-register dependencies and mask/spill
differences. Its separate archive retains the diagnostic snippets and complete
set control-flow graphs. It finds that the AND set regression exposes a producer
count dependency even though the targeted seeded mask uses AND correctly.
Call fusion does not independently prove complete map-byte dataflow or the
actual serial dependency chain. Set `ordered` rows are **not strict serial
lookup latency**. No multimap, other-domain/width, growth policy, cache-residence
or preset conclusion follows from this panel.

`geometry-report.py` is copied byte-for-byte from the frozen mask source.
The audit scripts here preserve the exact versions used for this cohort.
Replay a compact export without fetching ELFs:

```sh
python3 qualify.py ../mask-zen5 /tmp/mask-zen5-replay
```

Recover `mask-VARIANT-40-W`, `assembly-VARIANT-40-W.gz`, `compile.json` and
`source.tar.gz` through the per-host `artifact.json` to repeat machine-code
qualification:

```sh
python3 audit-dwarf.py assembly-split-40-2.gz mask-split-40-2 \
  FROZEN_VALUE_WIDTH_BENCH_CPP OUTPUT --key-bits 40 --value-bytes 2 \
  --compile-commands compile.json --mask-variant split
python3 summarize.py OUTPUT/codegen.json OUTPUT
```

Add `--verify-binaries` to report qualification when recovered ELFs are present.
Full ignored audit output is under
`build/hashtable-design/mask-native/HOST/audit-VARIANT-40-W`.
