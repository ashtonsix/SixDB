# BZHI source experiment proposal

Recommend a separately captured experiment with `explicit_split<Width>` from
[mask_probe.h](mask_probe.h), retaining ordinary C++ as the control and
`explicit_pair` as the register-lifetime control. Neither is a proposed default
yet. The [matched binary intervention](../README.md) justified solving the
partial-count dependency; it did not establish that an opaque source helper
preserves the surrounding compiler optimizations.

The only proposed hashtable substitution is the **value masking operation** in
`permutation<K,A>::split`: replace `(key ^ input_seed) & mask(K)` with the chosen
experimental helper. Preserve the seed construction, producer `format_key`,
remaining permutation, layout, traces, and ordinary Abseil controls. Do not
replace the global constant-producing `mask` helper or narrow accepted keys.
This proposal is independent of the parent's currently running mutation capture.

## Guaranteed definition, bounded scope

For a nonconstant operand and a constant width in 33–63, the proposed split
sequence is:

```cpp
word count, result;
asm("movl %1, %k0" : "=r"(count) : "i"(Width));
asm("bzhiq %2, %1, %0"
    : "=r"(result) : "r"(x), "r"(count) : "cc");
```

`MOVL` defines the full register through zero extension. Both the output and the
second asm's input have 64-bit type. The latter is an opaque **full-register**
operand, so any intervening compiler-generated copy must preserve that value;
the compiler cannot justify replacing it with a low-byte-only definition based
on the BZHI intrinsic's count semantics. The compiler chooses registers; no
historical RCX/RAX assignment is encoded. This guarantees the definition for this
operation, not the absence of other compiler-generated BZHI instructions.

The blocks are nonvolatile and have no memory clobber: they can be eliminated,
commoned, or hoisted where their values permit. They need not remain adjacent.
MOV does not modify flags; BZHI has a `cc` clobber. A single-instruction BZHI
output may overlap an input because it reads its inputs before writing its
result. No early-clobber is required for that block.

The paired control places MOVL and BZHI in one asm. Its count output uses `=&r`,
because MOVL overwrites that scratch register before BZHI consumes `x`. Its
result uses `=r`, because the final instruction writes it. This guarantees
adjacency but repeats MOVL when `x` changes each iteration. It may use a shorter
register lifetime than the split form.

The source preserves ordinary C++ for widths 1–32, constexpr evaluation, and
compiler-known constant inputs. Width 0 returns zero; widths **at least 64**
return the input, including 256 and `UINT_MAX`. Shifts never use an invalid
count. The dynamic-width wrapper is only a semantic probe: it checks the full
unsigned width before BZHI's low-eight-bit count can truncate it. It is not a
new hashtable interface. Non-BMI2/x86 and non-x86 targets use ordinary C++.

For constant widths 33–63, `__builtin_assume` publishes the proven upper-zero
bits of the result to subsequent C++. It is justified by the explicit sequence,
not a caller restriction. The mask expression is first placed in a local
constexpr variable; Clang warns and discards the assumption if the expression
contains the helper function call directly. The retained version is clean under
`-Werror`.

`count_hint` records the weaker earlier avenue: explicit MOVL followed by the
compiler intrinsic. It guarantees count **production**, but leaves the eventual
operand selection and copies to intrinsic lowering. This is not equivalent to
the guaranteed two-asm contract. In the current standalone probe it introduces
an extra `movl %eax,%eax` before BZHI.

## What the compiler probe actually establishes

[compile.py](compile.py) builds assembly and objects with pinned Clang 21.1.8
for Zen5/x86-64-v3, Granite/x86-64-v4, x86 without BMI2, and NEON fallback.
Both vectorizers are disabled. Loop unrolling is disabled only to expose a
small readable loop; these are not the real timed callers. Commands, source
hashes, assembly hashes, and checks are in [codegen.json](codegen.json), with
the complete small Zen5 assembly in [zen5-codegen.txt](zen5-codegen.txt).

Compile checks cover representative widths 0–64 plus 65, 256 and `UINT_MAX`,
constant folding, upper-zero result folding, and identical instruction
lists against ordinary C++ on the fallback/ordinary branches. The constexpr
assertions exercise constant evaluation, not execution of inline assembly.
No native execution, timing, or whole-table correctness run was performed.

| Zen5 emitted code | Ordinary C++ | Split sequence | Paired sequence |
| --- | --- | --- | --- |
| Standalone 40-bit mask | MOVB + BZHI | MOVL + BZHI | MOVL + BZHI |
| Masked producer, then seed XOR and mask | 1 BZHI | 2 BZHI | 2 BZHI |
| Data-consuming loop | BEXTR, no BZHI | 2 BZHI + shift | 2 BZHI + shift |
| Explicit count inside loop | none | hoisted | MOVL each iteration |
| Callee-saved registers used by that loop | none | one push/pop | none |

The source helper loses producer-mask fusion across the seed XOR. The original
producer's remaining BZHI can still have a MOVB count definition. In the loop,
the helper also blocks the original mask-plus-shift BEXTR transformation.
Publishing upper-zero result bits permits later simplification, but cannot
restore either algebraic relationship through opaque asm. Granite likewise
retains two BZHI instructions in the forced loop; its ordinary loop chooses a
different shift/mask lowering rather than BEXTR. These are captured instruction
differences, not measured performance costs. Register pressure and optimizer
opacity are real costs the full consumer must charge.

## Decisive next comparison

Use one new frozen source with original/split/paired candidate arms and unchanged
ordinary Abseil arms. Begin with complete K40 set and multimap consumers at the
small and larger existing geometries, including the original fast composed/wide
controls as well as the slow cases. Preserve the full hit/miss/serial/update and
lifecycle work. Check all eight supported key widths and kinds for unchanged
hash parts, byte representation, independent oracle answers, and zero/high-bit
boundary inputs. The ordinary K8/16/24/32/64 and NEON branches are regression
controls, not a reason to skip their codegen checks.

Audit actual timed caller regions, following their control flow and inline debug
frames rather than clock ordinals. Check both table tiers, remaining producer
masks, full count definitions/copies, hoisting, spills, code size and outlined
calls. Preserve the exact same caller transformations and compiler settings for
all arms, including Abseil. Do not remove the producer's mask to rescue the
forced helper or infer isolation from unchanged hash answers alone.

The first native comparison belongs on Zen5; qualify other x86 targets
separately. A source choice earns adoption only if its full-operation benefit
survives those extra instructions and the previously fast controls. If it does
not, retain the source failure and pursue the compiler lowering with the reduced
case. An opaque-asm hint is not a substitute for that evidence. A forced AND
lowering would be a separate intervention, not part of this count experiment.

Reproduce the compile-only receipt from the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/hashtable-design/evidence/bzhi-control/source-probe/compile.py
```

The earlier `SOURCE-AVENUES.md` remains part of its immutable binary-receipt
bundle. This directory is the subsequent compiled source investigation; nothing
from it is included by live spike or production code.
