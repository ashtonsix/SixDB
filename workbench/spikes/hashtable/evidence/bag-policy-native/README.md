# Coupled bag-policy codegen audit

This audits the `HT_BAG_POLICIES=1` panel in `value_width_bench.cpp`.
The frozen native cohort passes all 720 regions across its nine ELFs: 540 read
consumers and 180 effective update CFGs, without call-graph truncation. See
[native.json](native.json) for exact identities and recovery artifacts, and the
[Granite](../bag-policy-granite/context.json),
[V2](../bag-policy-neon/context.json) and
[Zen5](../bag-policy-zen5/context.json) receipts. Every archived disassembly
matches the recovered timed ELF after excluding objdump's input-path prologue.

The complete captured reports replay exactly: 45,000 timed rows and 1,000 fresh
processes per host, including every comparison and each pass median. Each host
also has nine untimed observer histories, 27 phase inventories and 270 matching
timed rows. The earlier local observer check matched 162 rows because it used
three repetitions per process; native used five, both with two passes. These
logical observer events do not measure traffic or MVCC copying, and issued
damage ranges can overlap. All losses remain in the per-host `summary.csv`.

Source manifest digest:
`0354c3e814aba6f2ecaedfdf52d305a0fd44ff1059d214b73ecc6574c985054c`.
Source archive SHA:
`41e8ec5fad3a2d20392b5442bfa8815661f6888712ddcfc7b5c28a5d4896455b`.
The measured panel is K40 multimaps with W2/W8/W16 values, A6/A10 and the captured
fanout histories. Policies change geometry, inline capacity and/or spill
ownership together. This evidence does not select presets, establish cache
residence from bytes, or cover other widths and key domains.
The [submission provenance](cohort.json) identifies the frozen source and
Zen5/Granite/V2 jobs; submission does not qualify their outcomes.

The three local AArch64 Release binaries pass all 240 regions, including all
180 read consumers and 60 effective update CFGs; see [local.json](local.json)
for exact source, compile and ELF hashes. W8 has six outlined timing wrappers;
W16 has ten and one compiler-cloned ending clock. All resolve to their actual
source phases. Every candidate update uses a whole-loop helper; conditional
key-overflow, bag reservation and owner/allocation paths remain visible. Buffer
arm 7 also retains an identity-admission guard and its conditional initializer.
No call graph was truncated. These are local codegen findings, without native
timing or policy-selection claims. Full-byte dataflow witnesses were not
repeated in this audit scope.

Each W2/W8/W16 binary must contain 80 audited regions: multimap only, A6/A10,
eight candidate policies plus ordinary/exact-key typed Abseil, and
hit/miss/serial/update. That gives 60 read regions and 20 update regions per
binary. Arms 0–7 are candidates; arms 8–9 are the baselines. There is no
selected-arm option that could silently reduce this coverage.

`audit-dwarf.py` binds W/K and the policy macro to the matching CMake target
command, checks that compiler autovectorization is disabled, and records the
source, ELF, disassembly and compile-receipt hashes. Cohort retention must also
verify these against the actual frozen source and archived build artifacts;
a compile database alone is not a reproducible-build proof.

Phases come from the frozen source assignments and actual DWARF callsites.
Compiler-cloned ending clocks must resolve to the same phase. Outlined timing
wrappers are opened at their source callsite. Clock ordinals and lambda numbers
do not identify phases. The source's two conditional `build` assignments are
both recognized; only the compiled branch contributes DWARF locations.

`summarize.py` checks candidate and baseline read consumers alike. A helper
around an entire read loop must be called outside a caller CFG cycle, and its
body must have no unresolved branches or calls beyond the reported cold-format
and failure paths. Full-value-load/dataflow witnesses remain a separate check:
call fusion alone does not prove that every value byte reaches the digest.

Updates retain the effective query-loop calls and reachable owner call paths,
separating linked values, buffer values, key overflow, packing primitives,
Orbital, allocation, and exception handling. A call reachable again through a
CFG cycle is not automatically hot: allocation may occur only on a spill
branch. The audit assigns neither dynamic branch frequency nor timing cost.
Reachable-call-graph truncation is reported explicitly.

Run on the pinned Linux toolchain with the matching frozen files:

```sh
llvm-objdump-21 -d --demangle --no-show-raw-insn BENCH | gzip > assembly.gz
python3 audit-dwarf.py assembly.gz BENCH FROZEN_VALUE_WIDTH_BENCH_CPP OUTPUT \
  --value-bytes 2 --key-bits 40 --compile-commands COMPILE_JSON
python3 summarize.py OUTPUT/codegen.json OUTPUT
```

`llvm-addr2line-21 --no-debuginfod` uses the supplied ELF. Complete timed CFGs,
whole-loop helpers and selected owner callees are saved in the output directory.
Keep those bulky outputs in ignored storage or the archived artifact; retain
compact qualification and selected witnesses here. These scripts are a
separate adaptation; the previous mutation and value-width audits are unchanged.

The two report scripts here are byte-for-byte copies of the captured source.
To replay reports and qualify the compact export without downloading ELFs:

```sh
python3 qualify.py ../bag-policy-granite /tmp/bag-policy-granite-replay
```

Repeat for the other hosts. To repeat ELF auditing, recover `values-{2,8,16}`,
`assembly-{2,8,16}.gz`, `compile.json` and the exact `source.tar.gz` using the
per-host `artifact.json`. Native timing argv keep their original remote paths;
`qualify.py --verify-binaries` checks recovered binary basenames against those
unchanged receipts. Optional observer-binary hashes remain in the archived
event summary when regenerating from compact exports that omit the binaries.
