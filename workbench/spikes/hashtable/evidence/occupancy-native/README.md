# Occupancy encoding comparison: v2 qualification

This bounded cohort compares count-masked selectors with inactive fingerprints
at matched table geometry and phase inventories. The five cases cover K16/W2
and K40/W2 maps, K40 sets, K40/W8 multimaps and K64/W16 maps. Every case includes
all 16 geometry arms plus ordinary/exact-key Abseil, reference exponents 6/10,
two opposite process-order passes and five repetitions per fresh process.
[cohort.json](cohort.json) identifies the frozen source and worker jobs.
The cancelled v1 is excluded: its post-churn point oracle and construction
snapshot were corrected before this v2 capture.

All 30 native ELFs pass: 7,560 selected regions, comprising 6,480 reads and
1,080 updates, with no phase/call qualification failures or update callgraph
truncation. Every recovered ELF matches its timed-process hash and archived
disassembly, excluding only the objdump input-path prologue. The complete
frozen report replays exactly on each host: 46,800 rows, 720 fresh processes and
all 4,680 comparisons. Phase inventories agree between encodings; all losses
and both process-order passes remain in the exports. [native.json](native.json)
binds the source, codegen and recovery identities. Host exports are
[Zen5](../occupancy-zen5/context.json),
[Granite](../occupancy-granite/context.json) and
[V2](../occupancy-neon/context.json).

Source manifest digest:
`579783af989d57a19d1d4c2576d7d69117c55a5e8e9c51bcbc37249404f743ea`.
Source archive SHA:
`167c35ab369ff77ffa4b86457a15806cc12c24a33adef6384be3240cbd6259e0`.
The source/compile frontend also passes 37 positive and negative checks,
including rejection of v1 and incorrect control macros. A separate preserved
local-v2 AArch64 multimap check is recorded in [local.json](local.json).

The native audit requires the reviewed v2 driver hash and exact CMake target
`hashtable_design_occupancy_K_W_KIND_ENCODING`. Explicit macros bind occupancy
mode, kind, scan32, paired hash masking and count/inactive encoding; composed
selectors and disabled autovectorization are verified. All named timed callsites
are recognized from source. Actual DWARF callsites resolve six reads (`hit`,
`miss`, `serial` or `ordered`, `aged_hit`, `aged_miss`, `reused_hit`) and `update`:
216 read and 36 update regions per ELF. No clock ordinals or lambda numbers
assign phases. Reuse is present in the complete workload report but does not
have a separate mutation callgraph audit. Selected complete read CFGs and
effective update boundaries are included in each host export; all remaining
regions can be recovered by rerunning the frozen auditor against archived ELFs.

The preselected K40/W2 map witness (reference A10, arm1: actual A9 and 128-byte
buckets, 6,144 keys) illustrates why the encoding alone does not close the gap:

| Host | Inactive/count hit time | Inactive/Abseil hit time | Inactive/count serial time | Inactive/Abseil serial time |
| --- | ---: | ---: | ---: | ---: |
| Zen5 AVX2 | 1.013 | 1.489 | 1.021 | 1.214 |
| Granite AVX-512 | 1.033 | 1.195 | 1.007 | 1.089 |
| V2 NEON | 0.841 | 1.159 | 0.987 | 1.094 |

These are medians across both passes and five repetitions, not selected best
geometry results. A lower ratio is faster. Complete phase comparisons include
misses, mutations, aged/reused reads, memory and lifecycle; the table is a
single codegen witness, not a platform or encoding recommendation. Native
primitive checks and complete-process oracles passed on all three hosts.

The first read phases precede update/deletion. Aged hit/miss probes follow
half-table deletion, and reused hit probes follow reinsertion. The independent
point oracle checks presence, full values and multiplicity after both changes.
Multimaps start singleton, update to pairs and reinsert deleted keys as
singletons, so reused reads cover a half-pair/half-singleton history. Build
population columns remain build measurements; they do not describe aged
populations. Memory is sampled outside phases and excludes transient peaks.

`occupancy-run.py` is copied byte-for-byte from the frozen capture. The
qualifier replays it in a separate directory, retains every comparison and loss,
checks complete per-phase inventory agreement between encodings and binds
all ten audited ELF identities to the timed process receipt:

```sh
python3 qualify.py ../occupancy-zen5 /tmp/occupancy-zen5-replay
```

Recovered binaries permit `--verify-binaries`. For a complete codegen replay,
recover each `occupancy-CASE-ENCODING`, `assembly-CASE-ENCODING.gz`, `compile.json`
and the exact source archive from the host artifact receipt, then run:

```sh
python3 audit-dwarf.py assembly-40_8_2-inactive.gz occupancy-40_8_2-inactive \
  FROZEN_VALUE_WIDTH_BENCH_CPP OUTPUT --key-bits 40 --value-bytes 8 --kind 2 \
  --occupancy-variant inactive --compile-commands compile.json
python3 summarize.py OUTPUT/codegen.json OUTPUT
```

Whole-loop read helpers qualify only outside the caller query-loop cycle and
without inner hot calls or unresolved branches. Static update callgraph cycles
are not measured call frequencies or costs. `ordered` set traversal is not
strict serial latency; map/MM call fusion alone does not prove full-byte
machine-code dataflow. These small-table cases do not establish cache residence,
DRAM performance, full supported-domain coverage or a preferred encoding.
