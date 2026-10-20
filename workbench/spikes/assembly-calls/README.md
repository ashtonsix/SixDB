# Assembly calling compiled modules

Write the prototype as relocatable assembler source (`.S` with preprocessing,
`.s` without), link it with the normal CMake module targets, and call symbols.
Addresses printed by a disassembler belong to that binary; they are not a
link interface. Compiler-emitted assembler preserves relocations, constants and
ABI information that a copied disassembly listing can lose.

This example supports **Linux AArch64 and AVX2 x86-64** with the pinned Clang
driver (the current Ikea reader requires NEON or AVX2).
It exercises real Loom and Ikea code, not substitute implementations. From the
repository root (prefix Linux commands with `orb -m ubuntu` on the Mac):

```sh
cmake -S . -B build/clang/assembly-calls -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=assembly-calls
cmake --build build/clang/assembly-calls --target assembly_calls_check -j1
build/clang/assembly-calls/workbench/spikes/assembly-calls/assembly_calls_check
```

On x86-64, add `-DSIXDB_MARCH=x86-64-v3` when configuring this fixture.
Choose an ISA appropriate for the machine where it will execute.

## Start with an ABI sketch, then write the algorithm in assembly

For Engine's directory prototype, keep construction, backing/lifetime management
and the independent test oracle in C++. Write tiny sketches of the **module calls**
needed by the directory, then emit their assembler. The directory algorithm itself
can be written in assembly first; the sketches need not implement it in C++.

[seed.cpp](seed.cpp) demonstrates both relevant cases:

- `prototype_seed` calls the existing compiled `loom::bind_demand` directly.
  Clang supplies the mangled name, aggregate argument placement and
  `std::expected` result handling. There is no bridge call on this path.
- `prototype_ikea_contains` instantiates a concrete Ikea hash-table lookup.
  The inline template becomes the seam's actual body, callable under a readable
  C symbol. The constructor/storage owner stays in the C++ fixture. There is no
  pre-existing exported template symbol to discover with `nm`.

Emit the configured TU's assembler without touching its original object:

```sh
python3 workbench/tools/compile_probe.py build/clang/assembly-calls \
  --source workbench/spikes/assembly-calls/seed.cpp --assembly \
  --output build/assembly-calls/seed
cp build/assembly-calls/seed/00-seed/source.s build/assembly-calls/edited-seed.s
```

Edit `edited-seed.s`, initially retaining its complete constants, local helpers,
section and unwind directives. Select it **instead of** the C++ seed, and rebuild:

```sh
cmake -S . -B build/clang/assembly-calls \
  -DSIXDB_ASSEMBLY_SEED_SOURCE="$PWD/build/assembly-calls/edited-seed.s"
cmake --build build/clang/assembly-calls --target assembly_calls_check -j1
build/clang/assembly-calls/workbench/spikes/assembly-calls/assembly_calls_check
```

Use a new output directory for each emission. To restore the C++ seed, configure
with `-U SIXDB_ASSEMBLY_SEED_SOURCE`. That seed remains the small ABI reference;
check/recreate affected call sequences when module declarations or compiler flags
change. A hand-edited `.s` does not track C++ header dependencies automatically.
For a measured/worker experiment, put the selected assembler in the spike source
tree and reference it there. `build/` is excluded from
[source captures](../../tools/README.md#captured-experiment-runs); the temporary
absolute path above is only a local editing example. Retain the useful prototype
and its ABI sketches, rather than every full compiler dump.

## Hand-written calls and narrow compiled seams

[caller.S](caller.S) shows a non-leaf call with live state across it, plus an Ikea
tail call. The optional [bridge.cpp](bridge.cpp) gives Loom's result a scalar C ABI
for code written wholly by hand. It costs a separate boundary; use the emitted
direct call above when that overhead matters. `extern "C"` stabilizes this private
symbol's name; it does not change production declarations or choose a magic
register-only calling convention.

For a richer module operation, give the private seam typed or opaque pointers to
live C++ owners/views and explicit output storage. Let C++ perform operations on
`std::expected`, RAII types, callbacks and errors. C linkage **alone** does not
make a C++ aggregate return simple. Generate a direct caller from the real types
if studying that aggregate ABI is the point. Keep module lifetime, admission,
selection and Lane requirements in force in either version.

The example's scalar arguments follow these ordinary platform conventions:

| Boundary | Linux AArch64 | Linux x86-64 System V |
| --- | --- | --- |
| First scalar arguments | `x0`–`x7` (`wN` for 32-bit) | `rdi`, `rsi`, `rdx`, `rcx`, `r8`, `r9` |
| Scalar result | `x0` | `rax` |
| Direct call / matching tail call | `bl symbol` / `b symbol` | `call symbol` / `jmp symbol` |
| State live across call in this example | saved/restored `x19` | saved/restored `rbx` |
| Stack at call | `sp` aligned to 16; save original `x30` before nested call | `rsp` aligned to 16 **before** `call`; return address changes entry alignment |

Caller-saved registers and flags can change across a call; AArch64 linker veneers
may also use `x16`/`x17`. Do not keep live cross-call values in those registers or
in the x86 red zone. Preserve callee-saved registers you use. The example includes
CFI for its frame; it uses `noexcept` seams and does not exercise exception
propagation through hand-written code. Floating-point, vector, larger aggregate
and custom calling conventions require their own ABI treatment.

Sources: [AAPCS64](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst),
[x86-64 psABI](https://gitlab.com/x86-psABIs/x86-64-ABI/-/wikis/home),
[CMake ASM support](https://cmake.org/cmake/help/v3.24/command/enable_language.html).
The [CMake file](CMakeLists.txt) enables ASM only for this selected study and uses
the same compiler/target as C++. A new spike can copy those few declarations,
include its `.S`/`.s` among normal target sources, and link `ikea::hashtable`, `ikea::seriespack`,
`ikea::tuplepack`, `loom::loom` or `orbital::orbital` as its actual operation needs.

For instruction scheduling, the [MCA helper and model notes](../../tools/mca.md)
reuse literal assembler and preserve control-flow/callee limitations.

## Owning returns and complete-operation review

[ownership.cpp](ownership.cpp) is a small real-Loom lifetime check and ABI sketch:

```sh
cmake --build build/clang/assembly-calls --target assembly_ownership_check -j1
build/clang/assembly-calls/workbench/spikes/assembly-calls/assembly_ownership_check
python3 workbench/tools/compile_probe.py build/clang/assembly-calls \
  --source workbench/spikes/assembly-calls/ownership.cpp --assembly \
  --output build/assembly-calls/ownership
```

Inspect `forward_allocate`, `acquire_into`, `initialize_frame`, `destroy_frame`
and `borrow_bytes`. The assign adapter creates a temporary owning result,
moves it into the destination, and destroys the moved-from result. Direct member
initialization lets the actual allocator construct its result in the caller's
stationary operation frame. It removes that extra move/temporary cleanup; the
frame still owns the result and must be destroyed after its final use. Replacing
an already-occupied owner still needs its real retirement work.

With the current Clang 21.1.8/libstdc++15 cut, `Result<Allocation>` is 40 B and
`Result<PartSource>` is 96 B. The emitted AArch64 assign adapter reserves 96 B of
stack; direct frame initialization reserves 48 B, with the 40 B owning result in
caller storage. That is a local-frame/transfer difference, not a memory saving
or a measured speedup. Inspect actual return handling: `sizeof`, `extern "C"`
and the presence of `std::expected` alone do not determine register passing.

For the directory retry, inspect the **linked complete caller and its callees**:
separate callee-save preservation from payload spills; look for hidden result
buffers, duplicate entry/source materialization, repeated admission/selection,
owner transfers and per-point dispatch. Preserve the same useful output and real
lifetime rules when comparing alternatives. Scope-protected native reads need
no extra per-point physical ownership merely to cross an assembly boundary.

Measure reads from the declared snapshot/request boundary through consumption
of the required dirent/Part output. Measure writes from validation/basis selection
through real Ikea edits, Loom allocation/dispatch as used, capture, the complete
coupled prepare/install packet and request-local destruction. Keep service latency
and whole-history maintenance/settlement visible separately, with bounded memory;
deferred cleanup is still charged to the history. Time complete operations before
accepting the C++ translation. Inner-kernel timings only help explain that result.

Inspect unresolved **relocations** before linking, then resolved targets after:

```sh
llvm-objdump-21 -dr build/clang/assembly-calls/workbench/spikes/assembly-calls/CMakeFiles/assembly_calls_check.dir/caller.S.o
llvm-objdump-21 -d --demangle build/clang/assembly-calls/workbench/spikes/assembly-calls/assembly_calls_check
```

The fixture checks Loom's class boundaries, invalid input and a live post-call
value, plus Ikea hit/miss results against independently known inserted keys.
It measures no timings. During the assembly-first spike, measure the complete
operation including real calls/setup belonging to that operation. Keep the
independent oracle and the same workload for the later C++ implementation, then
compare emitted paths and measurements across that transition.
