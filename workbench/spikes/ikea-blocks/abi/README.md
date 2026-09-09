# Wide results and immediate handoff

2026-09-09. A fresh boundary probe prompted by the rejected bitset prototype.
This addresses the native carrier seam in
[the composition sketches](../../ikea-composition/sketches-2.md#common-kernel-and-execution-boundary).
It does not select a public API, implement a codec, or measure kernel speed.

**A wide `std::expected` return materializes at an opaque ordinary boundary on
both tested ABIs. An immediate continuation can carry a scalar and native
vectors without that materialization using the ordinary ABI.** Inlining the
small known-success `expected` control removes its representation completely.
These are three different observations; none licenses validation inside a hot
kernel or assumes a real codec will always inline.

## What was compiled and checked

[producer.cpp](producer.cpp), [caller.cpp](caller.cpp), and [sink.cpp](sink.cpp)
compile independently, without LTO or unity builds. Every inspected external
function is explicitly `noinline`. The caller cannot see the producer body;
the producer receives an opaque continuation pointer and cannot see the sink.
Both sides use the same declared calling convention and target flags. The
control bodies only load bytes and increment a scalar so that ABI traffic is
visible. Unaligned input loads and terminal output stores are intentional.

Linux Clang **21.1.8**, C++23, `-O3 -g -fno-fast-math -ffp-contract=off
-fdenormal-fp-math=ieee`; libstdc++ release **15**, header stamp **20250917**.
The library installation is recorded, not pinned by SixDB's compiler pin.
Targets: native `aarch64-unknown-linux-gnu` baseline, and
`x86_64-linux-gnu` at `-march=x86-64`, `x86-64-v3`, and `x86-64-v4`.
The latter two supply AVX2 and AVX-512 respectively; no CPU tuning was added.
All comparisons use the same source, compiler and library release.

The native AArch64 executable and the SSE2/AVX2 executables under QEMU passed
cross-TU success/error/continuation checks. AVX-512 is **compile/disassembly
evidence only** here. QEMU is not a performance measurement. SVE, scalable
carriers, Windows, Darwin, other compilers/libraries, and arbitrary high register
pressure are outside this probe.

Small selections of complete functions, with call relocations, are
in [evidence/20260909](evidence/20260909): ordinary returns versus expected,
oversized versus native CPS carriers, regcall controls and inline controls.
The full run also contains every
object, executable, full disassembly, LLVM IR, exact compilation commands,
diagnostics and captured sources. No source assertion about `sret` substitutes
for inspecting the actual instructions.

## Return values: actual carrier placement

`V128/256/512` are Clang integer vector types from `vector_size`, **not stored
block classes**. The 256/512-bit forms are oversized compiler vectors on base
AArch64, which has native fixed vector registers of 128 bits. `Tagged<V>` is
`{uint64_t tag; V bits;}`. `Checked<V>` is `std::expected<V, uint8_t-sized Error>`.

| Return | x86 SSE2 | x86 AVX2 | x86 AVX-512 | AArch64 baseline |
| --- | --- | --- | --- | --- |
| `V128` | XMM0 | XMM0 | XMM0 | Q0 |
| `V256` | XMM0–1¹ | YMM0 | YMM0 | indirect via X8 |
| `V512` | XMM0–3¹ | YMM0–1¹ | ZMM0 | indirect via X8 |
| `Tagged<V128/256/512>` | indirect via RDI | indirect via RDI | indirect via RDI | indirect via X8 |
| `Checked<V128/256/512>` | indirect via RDI | indirect via RDI | indirect via RDI | indirect via X8 |
| `Checked<uint64_t>` | RAX + RDX | RAX + RDX | RAX + RDX | X0 + X1 |
| `{V128 lo, hi}` | indirect via RDI | indirect via RDI | indirect via RDI | Q0 + Q1 (HVA) |

¹ These are the observed Clang fallback **returns**, not a portable wide-vector
ABI promise. Their arguments use stack/by-address passing at those targets.
Clang emits ABI-change warnings for such calls. A `V512` declaration is not a
stable binary boundary if its caller and callee are built for different vector
targets. Enabling AVX-512 changes `raw512` from two YMM return registers to one
ZMM register and changes its by-value arguments from stack to ZMM.

Layout illustrates the cost but does not establish calling convention:

| Type family | x86 size/alignment, bytes (128/256/512) | AArch64 size/alignment, bytes (128/256/512) |
| --- | --- | --- |
| Vector | 16/16, 32/32, 64/64 | 16/16, 32/16, 64/16 |
| Tagged and Checked | 32/16, 64/32, 128/64 | 32/16, 48/16, 80/16 |

`Checked<uint64_t>` is 16 bytes with alignment 8 on both. All these expected
specializations have trivial copy/move constructors and destructors in this
library, despite not satisfying `is_trivially_copyable`. The trait alone does
not predict their ABI. The observed memory return for the mixed wide aggregate
is already present in the entirely trivial `Tagged` control.

For a successful opaque AVX-512 `expected512` call, the caller aligns its stack
to 64 bytes, reserves space, passes the result address in RDI and shifts the
explicit arguments to RSI/EDX. The producer loads the input into ZMM0, writes
64 payload bytes to `[RDI]`, and writes the success byte at `[RDI+64]`. The caller
tests that byte, reloads the 64-byte payload from its stack into ZMM0 and stores
it to the final output. Thus the result boundary adds a **64-byte store and
64-byte reload**, plus status store/load and checking, beyond the input load
and terminal store. RAX returns the result address. A `vzeroupper` occurs when
returning from the memory-result producer; the raw ZMM-return producer has none.

The corresponding AArch64 128-bit call passes the result address in X8. Its
producer writes Q0 to `[X8]` and the success byte at `[X8+16]`; its caller reads
the byte then reloads Q0 from the stack. This adds a **16-byte store and 16-byte
reload** plus status work. Saving a caller's output pointer or return address
is separate call bookkeeping and is visible in the evidence, even on raw
register returns.

These observations accord with the aggregate classification and return rules
in the [x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI/-/blob/master/x86-64-ABI/low-level-sys-info.tex)
and the short-vector, homogeneous-aggregate and result-return rules in
[AAPCS64](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst).
The particular fallback split-register returns above are compiler observations.

## `__regcall` is useful, but does not rescue every expected

On x86 at the matching native width, `reg_tagged128/256/512` returns the scalar
in RAX and the vector in XMM0/YMM0/ZMM0. Its caller uses those registers directly;
there is no aggregate temporary. Without AVX-512, `reg_tagged512` still returns
indirectly. `__regcall` changes argument placement too: the input pointer and
tag for the register-return case arrive in RAX and RCX. Both declarations and
function-pointer types must agree; this is not the SysV external ABI.

**Every wide `reg_expected` tested still returns in memory.** In particular,
`reg_expected128` returns via RAX as a hidden result pointer and the caller
reloads the payload; adding the attribute to `std::expected` is insufficient.
A trivial control `UnionChecked<V128> { union {V128 bits; Error error;}; bool
valid; }` *does* return XMM0 plus AL under regcall, so a union by itself does not
explain that difference.

[regcall-lowering.txt](evidence/20260909/regcall-lowering.txt) records a useful
trap: Clang's LLVM IR initially declares `reg_expected128` as a direct aggregate
return. Its expected layout is packed and has an explicit `[15 x i8]` tail
padding member, unlike the trivial union control. The **machine ABI nevertheless
uses memory** after backend lowering. The 256-bit expected is already `sret` in
the emitted IR. This probe establishes the difference and exposes the lowering;
it does not isolate a complete compiler/library root cause or claim an ABI bug.
The pinned [Clang x86 classifier](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/clang/lib/CodeGen/Targets/X86.cpp)
is the relevant implementation for a further investigation.

AArch64 rejects `regcall` as unsupported when `-Werror=ignored-attributes` is
used. The [diagnostic](evidence/20260909/aarch64-regcall-rejected.txt) is retained.
Silently ignoring the attribute cannot establish an alternate ARM stage ABI.
[Clang documents regcall as an x86 convention](https://clang.llvm.org/docs/AttributeReference.html#regcall).

## Immediate continuations: ordinary ABI, separate arguments

The candidate hot shape is an opaque native carrier handoff with **no expected,
validation, status tag or error branch**. The scalar below models useful live
state (such as a position or an accumulator), not an error code:

```cpp
using Next512 = void (*)(void* context, uint64_t scalar, V512 bits);
void cps512(const void* bytes, uint64_t scalar, void* context, Next512 next) {
    next(context, scalar + 3, load<V512>(bytes));
}
```

With AVX-512, the entire producer is:

```asm
addq    $3, %rsi
vmovups (%rdi), %zmm0
movq    %rdx, %rdi
jmpq    *%rcx
```

The sink receives the scalar in RSI and bits in ZMM0, stores them to the final
destination and returns to the original caller. No payload goes through an
intermediate stack slot, and no `vzeroupper` destroys the live carrier before
the jump. XMM0 or YMM0 works analogously at the corresponding native widths.
This is possible because **argument classification differs from returning one
mixed aggregate**. CPS does not require regcall for this shape.

On baseline AArch64 the 128-bit version uses Q0, X1 and `br x3`, with X0 carrying
context. To hand off 512 positions using native carriers, the separate
`cps_chunks512` has four V128 arguments. Its actual producer is:

```asm
ldp q0, q1, [x0]
add x1, x1, #3
ldp q2, q3, [x0, #32]
mov x0, x2
br  x3
```

The sink stores Q0–Q3 and X1 directly. Four XMM arguments give the same property
on SSE2. Conversely, passing one oversized `V512` argument on AArch64 writes
64 bytes to a local frame and passes its address in X2; the sink reloads them.
At x86 AVX2, the oversized `V512` CPS producer even performs two successive
64-byte stack copies before its call. Choosing a source type named "512" does
not choose a suitable carrier.

`load<V>` is the same visible body used by the inline and CPS paths. The storage
pointer, logical repetition count and native carrier width are independent
decisions. A larger physical bitset can traverse many 512-position tiles without
a separate TU for every storage size. An ARM implementation can use four
native registers for that tile or choose a different traversal grain.

The evidence proves immediate tail handoffs in these straight-through wrappers,
not tail calls under arbitrary composition. These changing-signature wrappers
do not use `musttail`; a uniform recursive stage protocol should separately
enforce/inspect its tail calls. Register pressure, retaining values across a
non-tail call, and carrier bridges may introduce spills. Prepared dispatch must
bind compatible carrier/ISA families as well as logical operation contracts.

## Inlining and validation lifetime

For all widths and all four targets, `inline_expected_successN` has the same
load/store instruction body as `inline_rawN` (apart from addresses/alignment
padding). Ordinary `inline` suffices in this small control; no forced inlining
is used. The dynamic-validity version also has no expected temporary after
inlining, but retains the test/branch and scalar error result. The source error
semantics remain work even when their C++ object representation disappears.

The useful seam for the restart is therefore: checked storage admission or
binding owns bounds/format/metadata validation and fixes the trusted bytes'
lifetime; trusted kernels consume those established facts; their inline/CPS
wrappers carry only the actual computational values. A changed buffer or
invalidated snapshot requires renewed admission. An analyser's scalar result
can use an ordinary scalar return; a native-vector stage can use a bare native
return or an immediate compatible continuation. A checked wrapper returning
expected is a separate convenience/correctness boundary, not a prerequisite for
every internal call.

This probe does not measure the inliner cost model for a full codec, instruction
cache growth, cycle costs or large continuation graphs. The tiny successful
control refutes a blanket claim that `expected` necessarily blocks inlining;
it does not justify relying on inlining to repair an unsuitable opaque ABI.

## Reproduce

From this macOS/OrbStack checkout:

```sh
orb -m ubuntu python3 workbench/spikes/ikea-blocks/abi/run.py
```

The script uses captured source and stable independent CMake/Ninja objects under
`build/workspaces/ikea-blocks-abi`, runs cases sequentially, and writes a receipt
under `build/experiments/ikea-blocks-abi`. `--output PATH` overrides the directory;
`SIXDB_RESULTS` is used as its parent when supplied. Its present target matrix
expects an AArch64 Linux host with the installed x86 GCC15 cross sysroot and
QEMU; it is not a generic worker launcher. No kernel timings are collected.

Recover all retained raw evidence with:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/ikea-blocks/abi/evidence/20260909 \
  build/recovered/ikea-abi-20260909
```

After recovery, reproduce the small assembly selection without compiling:

```sh
orb -m ubuntu python3 workbench/spikes/ikea-blocks/assembly.py abi \
  build/recovered/ikea-abi-20260909 build/recovered/ikea-abi-excerpts
```

The `.txt` diagnostic is a byte-for-byte copy of the full run's
`aarch64-regcall-rejected.stderr`; the extension keeps it visible to Git.
