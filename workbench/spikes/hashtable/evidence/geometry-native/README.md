# Geometry-panel codegen audit

Prepared for `HT_GEOMETRY_POLICIES` in `value_width_bench.cpp`. Parent-owned
[geometry.md](../../geometry.md) defines the experiment and its limits.

The frozen native cohort passes all 5,184 regions across 36 timed ELFs:
3,888 read consumers and 1,296 effective update CFGs, without truncation.
Every recovered ELF matches its timed-process hash and archived disassembly
(excluding only objdump's input-path prologue). [native.json](native.json)
records the shared source and all build identities; the
[Zen5](../geometry-zen5/context.json),
[Granite](../geometry-granite/context.json) and
[V2](../geometry-neon/context.json) exports include recovery artifacts.

The captured report replays exactly on every host: 38,880 timed rows, 864 fresh
processes and all 3,888 comparisons per host. Raw rows, losses, per-pass medians,
independent fitting capacities, padding and tier populations remain available
in each export. Source manifest digest:
`4e6e037b706978fdbc4967e0fdce3499b5d591703346070e0904014cab06e809`;
source archive SHA:
`82491d88b46830ff0f24b69785e58616217327744d854a7b875901fa5e9fdb49`.
This capture precedes the separate selector direct/scan32 controls.

The preserved local AArch64 K16/W0 and K64/W16 binaries pass all 288 regions,
including 216 read consumers and 72 effective update CFGs. Their exact hashes
match the preserved timed-process receipt; see [local.json](local.json).
K16/W0 resolves twelve outlined timers and sixteen cloned endings; K64/W16
resolves thirty-two cloned endings. Sets are labeled `ordered`, and actual
versus reference bucket exponents match every captured arm. No audit defect or
call-graph truncation was found. Those two local receipts are separate from the
native audits above. Independent dataflow witnesses were not repeated here.

Each binary covers one K and value width: W0 sets or W2/W8/W16 maps. The initial
panel is K16/K40/K64. The CMake receipt is selected by the exact target
`hashtable_design_geometry_K_W`; macros must agree on K, W and container kind,
geometry mode must be enabled, bag-policy mode absent, and compiler
autovectorization disabled. Source, compile receipt, ELF and disassembly hashes
are recorded for subsequent binding to the frozen cohort artifacts.

All 18 arms are mandatory: 16 candidate geometries and ordinary/exact-key
Abseil at arms 16/17. With reference exponents 6/10 and four phases, each ELF
requires 144 regions: 108 reads and 36 updates. There is no selected-arm option.
The 16 geometry definitions are read from the supplied frozen source and must
cover the full four-unit/four-bucket-shift product. Reports keep `reference_A`
separate from the arm's actual primary `A`; the template argument is the former.

DWARF assignments and actual callsites identify phases, including outlined
timing wrappers and cloned ending clocks. The source variable `serial` is
reported as `ordered` for sets, matching the driver's emitted label. Ordered
set traversal is **not qualified serial lookup latency**: predicted-hit paths
can synthesize constant Boolean results. Call fusion does not repair that
limitation. Maps retain the driver's value-dependent `serial` label, but a
call-boundary audit alone does not prove full-byte dataflow or the machine-code
dependency chain.

Both candidate and Abseil consumers undergo the same fusion checks. A helper
around an entire loop must be called outside the caller's CFG cycle, without
unresolved branches or per-lookup calls inside. Cold initialization and failure
calls remain visible. Update helpers and reachable owner calls are retained
separately; static reachability around a CFG cycle does not measure dynamic
call frequency or allocate a timing cost to conditional overflow/allocation.
Call-graph truncation is reported explicitly.

Run with the pinned Linux toolchain after the coordinated build/capture:

```sh
llvm-objdump-21 -d --demangle --no-show-raw-insn geometry-40-0 | gzip > assembly.gz
python3 audit-dwarf.py assembly.gz geometry-40-0 FROZEN_VALUE_WIDTH_BENCH_CPP OUTPUT \
  --key-bits 40 --value-bytes 0 --compile-commands COMPILE_JSON
python3 summarize.py OUTPUT/codegen.json OUTPUT
```

The optional `--selector-variant composed16|direct16|composed32|direct32`
binds the exact target `hashtable_design_selector_K_W_VARIANT` for the bounded
K16/K40 W2 comparison. It requires explicit `HT_VALUE_DIRECT` and
`HT_RECIPE_SCAN32` definitions matching that variant. All 18 arms, source-derived
geometries and phase checks remain mandatory. Selector identity is retained in
both summaries and update-boundary reports. Omitting the option preserves the
ordinary geometry target binding and output fields. The completed selector
cohort and exact audit version used are retained in
[selector-native](../selector-native/README.md).

The separate `--mask-variant ordinary|split|paired|and` is mutually exclusive
with selector mode. It binds `hashtable_design_mask_40_W_VARIANT` for K40 W0
sets/W2 maps and requires explicit `HT_HASH_MASK=0/1/2/3`, respectively,
`HT_VALUE_DIRECT=0` and `HT_RECIPE_SCAN32=0`. Mask identity is retained only for
mask-mode output; default and selector receipt fields are unchanged. All 18
arms and the ordered-set limitation remain mandatory. Completed native
qualification and the exact audit version used are retained in
[mask-native](../mask-native/README.md).

The separate `--occupancy-variant count|inactive` also requires `--kind` and
binds `hashtable_design_occupancy_K_W_KIND_VARIANT`. This bounded mode accepts
only the reviewed occupancy-v2 driver hash and its five declared cases;
the cancelled v1 is excluded. Explicit compile definitions must select
occupancy study, the matching container kind, scan32, paired hash masking and
the requested count/inactive encoding. The composed selector default is
checked. All named timed callsites, including reuse writes, are recognized
from source before DWARF resolution. Six reads (`hit`, `miss`, `ordered` or
`serial`, `aged_hit`, `aged_miss`, `reused_hit`) and `update` are qualified:
252 regions per ELF, comprising 216 reads and 36 updates. Reuse is covered by
the complete workload report but has no separate mutation callgraph audit.
The ordinary, selector and mask modes retain their four-phase coverage and
receipt fields. Existing frozen panel auditors are unchanged.
Completed three-host qualification and the exact audit version used are in
[occupancy-native](../occupancy-native/README.md).

Complete timed CFGs, helper bodies and selected owner callees go to the output
directory, intended for ignored storage or archival. Native performance,
strict set latency, cache residence and preset selection are not implied by
codegen qualification. Existing frozen audits and benchmark sources remain
unchanged.

`geometry-report.py` is copied byte-for-byte from the frozen source.
`qualify.py` replays the complete report and binds all twelve audited binaries
to each host's unchanged process receipt. Compact-export replay needs no ELF:

```sh
python3 qualify.py ../geometry-zen5 /tmp/geometry-zen5-replay
```

To repeat machine-code qualification, recover `geometry-K-W`,
`assembly-K-W.gz`, `compile.json` and `source.tar.gz` using the per-host
`artifact.json`. Add `--verify-binaries` to report qualification when those
ELFs are present. Complete per-region outputs remain in ignored
`build/hashtable-design/geometry-native/HOST/audit-K-W`. The selected x86
K40/W2 arm0/arm1 hit and serial CFGs are also retained in each x86 export;
their phase receipts show full fusion, without assigning a cause to the
observed timing differences.
