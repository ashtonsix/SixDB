# Constant-mask lowering in Clang 21.1.8

This completed October 14, 2026 compiler probe explains why an ordinary 40-bit
mask can introduce a partial-register count definition before BZHI. It preserves
the reduced case and rejected workarounds; it selects no production helper,
compiler flag or toolchain change. Related complete-container measurements stay
with the [hashtable hash-mask study](../../../spikes/hashtable/hash-masks.md).

In the pinned LLVM source, the four normal/EVEX register/memory AND-to-BZHI
patterns explicitly use `MOV8ri` and `INSERT_SUBREG(IMPLICIT_DEF)`. The adjacent
BEXTR pattern uses a full `MOV32ri` definition instead. See
[X86InstrMisc.td at llvmorg-21.1.8](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/llvm/lib/Target/X86/X86InstrMisc.td#L1362).
This explains the instruction selection. Whether the remaining register bits
create an expensive carried dependency depends on the complete caller and CPU.

The same pinned Clang cross-compiled the reduced cases for Zen5/x86-64-v3 and
Granite Rapids/x86-64-v4. Both vectorizers and loop unrolling were disabled to
keep the loops readable. [summary.json](summary.json) records selected
instruction lists, source identities and the three option controls. Full
commands, eight assembly files and two assembled objects are archived.

| Source control | Result in the reduced cases |
| --- | --- |
| Ordinary mask, shift pair, unsigned `_BitInt(40)` cast | Byte count definition and BZHI for the standalone mask |
| BZHI builtin, including an opaque count-only asm input | Full definition for that mask, but the producer retains another BZHI with a byte count |
| BEXTR builtin with start 0 and length 40 | One BEXTR after producer/XOR, full count definition, folded memory operand in the load case |

Disabling AND-immediate shrinking, disabling byte/word fixup, or increasing the
partial-register clearance left every reduced function's instruction list
unchanged on both targets. Those flags are not remedies for this case.

The BEXTR candidate preserves the producer-mask simplification in this reduced
expression, but loses the ordinary Zen5 loop's combined mask-and-shift lowering:
an additional shift remains. Widths 33, 40, 48, 56 and 63 emit full count
definitions and no helper calls; the guarded ordinary branches cover 0, widths
through 32, and widths at least 64, including 256 and `UINT_MAX`. These are
compile-only observations, without x86 execution or timing. They do not justify
adopting BEXTR in a complete caller.

[llvm-full-count-candidate.patch](llvm-full-count-candidate.patch) sketches a
replacement of the four byte-count patterns with zero-extended 32-bit counts.
It was **not applied, TableGen-built, LLVM-tested or performance-qualified**.
Keep it as a candidate with the reduced case, not a validated compiler fix.

## Reproduce or recover

Copy [count.cpp](count.cpp) and [run.py](run.py) into a fresh ignored directory,
then run the script with the pinned Linux compiler. From the Mac repository root:

```sh
orb -m ubuntu mkdir -p build/reviews/bzhi-replay
orb -m ubuntu cp workbench/tools/evidence/bzhi-20261014/count.cpp workbench/tools/evidence/bzhi-20261014/run.py build/reviews/bzhi-replay/
orb -m ubuntu python3 build/reviews/bzhi-replay/run.py
```

The script asserts the compiler version, emitted forms, width branches and
unchanged option controls, then writes a fresh receipt. It does not execute the
cross-compiled code. The original receipt's two source hashes, eight assembly
hashes and two object hashes were rechecked during retention on October 15.

[artifact.json](artifact.json) recovers the original receipt, assembly, objects,
tagged LLVM source copies and complete file hashes. The Git selection keeps the
small authored reproducer, candidate patch and compact findings. Four superseded
exploratory assembly files are omitted from the selected archive; their final
target-named replacements retain the qualified comparisons.
