# Value-width read-path audit

This adapts the native phase audit to `value_width_bench.cpp`. It does not
modify the benchmark or implement a container policy. Each width has 60 read
regions: map/multimap, A6/A10, three candidate layouts, ordinary/exact-key
typed Abseil, and hit/miss/serial. The benchmark's current key width is K40.

`audit-dwarf.py` resolves each `ht::trial<Kind,A,Arm>` phase using caller
locations in the supplied source. Ending clocks cloned across control-flow
paths must all resolve to that same phase. Outlined timing wrappers are opened
at their actual source callsite; neither clock ordinal nor lambda numbering
determines phase. W and K are translation-unit macros, so their selection is
checked against the width-specific compile command and source defaults. The
ELF, disassembly, source and compile receipt are hashed.

`summarize.py` distinguishes a helper around an entire read loop from a call
inside the query loop. A whole-loop helper must be called outside a CFG cycle;
its body must contain no hot calls or unresolved external branches. Indirect
branches are explicitly unqualified. Cold-format and failure calls remain
visible. Both candidate and typed-Abseil consumers undergo the same checks.
The report is emitted even if fusion qualification fails, and the command
returns failure. This is static call-boundary evidence, not a branch-frequency
or performance model. Selected full-value-load witnesses remain a separate
check: fusion alone does not prove that every requested byte is consumed.

The final local Release W2/W8/W16 binaries pass all 180 read regions; see
[local.json](local.json) for exact ELF, source and compile receipts. The
[selected load witnesses](local-load-witnesses.txt) show real two-byte loads
for narrow values and both eight-byte halves entering the wide-value digest,
including the serial dependency, on both candidate and typed-Abseil paths.
These are AArch64 codegen checks. They do not establish performance, qualify
unmeasured widths, or substitute for auditing the subsequent native binaries.
The source matches native group `20261013T211118Z-4a441080`, manifest
`8962f570759f348c24a89c60a1483c01ef469f9f73763c592c84a4ac50ee513d`.

Run with the actual binary, its matching source, and compile receipt:

```sh
llvm-objdump-21 -d --demangle --no-show-raw-insn BENCH | gzip > assembly.gz
python3 audit-dwarf.py assembly.gz BENCH FROZEN_VALUE_WIDTH_BENCH_CPP OUTPUT \
  --value-bytes 2 --key-bits 40 --compile-commands COMPILE_JSON
python3 summarize.py OUTPUT/codegen.json OUTPUT
```

`llvm-addr2line-21 --no-debuginfod` uses the supplied ELF/DWARF without remote
symbol lookup. Full timed regions and any whole-loop helper bodies are written
to the output directory for inspection. Local qualification does not establish
native performance or coverage of unmeasured value widths.
