# Native selector execution qualification

This retains the frozen K16/K40 W2 map comparison of composed/direct selector
execution at 16/32 bytes. Every variant keeps all 18 geometry arms and both
reference cardinalities. The original [cohort.json](cohort.json) records its
submission; [native.json](native.json) records completed qualification and
recovery identities for [Zen5](../selector-zen5/context.json) and
[Granite](../selector-granite/context.json).

All sixteen timed ELFs pass 2,304 source-resolved regions: 1,728 read consumers
and 576 effective update CFGs, without truncation. Each ELF matches its timed
process receipt, and regenerating its disassembly matches the archived assembly
after excluding only objdump's input-path prologue. Compile receipts bind the
exact selector target and both execution-control macros. Selected K40/W2
arm0/arm1 hit and serial CFGs are retained per host for the separate codegen
diagnosis; call fusion alone does not assign a cause to timing differences.

The captured report replays exactly: 25,920 timed rows, 576 fresh processes and
2,592 comparisons per host. Every comparison, loss and forward/reverse-pass
median remains in each `summary.csv`; raw rows and process receipts are also
retained. A separate check verifies that every phase's requested bytes, owned
capacity, metadata, geometry and tier population agree across all four variants.
The ordinary and exact-key Abseil controls are retained separately in every
binary, including their variation across processes.

Source manifest digest:
`d01ce893c648d9d64348ebae2227c3094e29c83ff41ae35a8468a8e5bbea6c59`.
Source archive SHA:
`8c72350e742fdcc67ddbc424b46d93b7a7d817f7d3c06bb84f500ec66b3ebe94`.
Driver SHA:
`e8b82cd8f891dd76cc560c33f4ea854c18e69476a1bdad426b69e48f3fc5501a`.
Later hash-mask controls are excluded from this capture.

This is a finite whole-operation comparison, not isolated selector instruction
timing. Register allocation and dependencies elsewhere in the same loop can
change, including the hash count-register feedback under investigation.
Primitive guard/codegen evidence alone does not establish complete-table speed.
Maps retain the driver's value-dependent `serial` label; this call-boundary
audit does not independently prove full-byte dataflow or the actual dependency
chain. No set/multimap result, cache-residence classification, growth policy,
or preset follows from this panel.

Subsequent selected analyses are retained separately: [serial-dataflow.json](serial-dataflow.json)
traces actual returned two-byte values into the next query for twelve candidate
and baseline cases; [structural-paths.json](structural-paths.json) records the
ordered primary-success instruction paths for the K40/W2 scan32 candidate and
ordinary Abseil. The latter's 46 versus 34 static instructions are neither
average lookup work nor cycles. Neither analysis extends its conclusion to
every cohort case.

`geometry-report.py` is copied byte-for-byte from the frozen selector source.
The audit scripts here are the exact versions used for these native audits,
preserved before extending the shared geometry audit for later experiments.
To replay the complete compact export without fetching binaries:

```sh
python3 qualify.py ../selector-zen5 /tmp/selector-zen5-replay
```

Recover `selector-VARIANT-K-2`, `assembly-VARIANT-K-2.gz`, `compile.json` and
`source.tar.gz` through the per-host `artifact.json` to repeat the codegen audit:

```sh
python3 audit-dwarf.py assembly-composed16-40-2.gz selector-composed16-40-2 \
  FROZEN_VALUE_WIDTH_BENCH_CPP OUTPUT --key-bits 40 --value-bytes 2 \
  --compile-commands compile.json --selector-variant composed16
python3 summarize.py OUTPUT/codegen.json OUTPUT
```

Add `--verify-binaries` to report qualification when recovered binaries are
present. Full ignored audit output is under
`build/hashtable-design/selector-native/HOST/audit-VARIANT-K-2`.
