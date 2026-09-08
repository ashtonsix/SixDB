# Build and code conventions

## Established direction

One repository; CMake with Ninja; independently and incrementally compilable
TUs, particularly for prototypes. C++ is primary, with pinned Clang and modern
C++. Prefer error codes to exception unwinding. Use `-O2` or `-O3` without
options such as fast-math that compromise deterministic execution.

Release artifacts should shed symbols and present a modest barrier to casual
reverse engineering. Keep build time and binary size under control through
CPS and deliberate inlining, particularly across combinatorial spaces.

See the [Workbench guide](../README.md) for research tooling, shared datasets,
and evidence storage, and [AGENTS.md](../../AGENTS.md) for agent working defaults.

## Implemented starting choices

- Clang **21.1.8**, already installed in the Linux development VM. The exact
  version is checked; the pin lives in
  [ClangVersion.cmake](../../cmake/ClangVersion.cmake). This is an initial
  working pin, not a claim that it is the latest release. Standard-library,
  linker, sysroot, and worker-image pins remain to be selected.
- C++23, with language extensions off. The installed Clang/libstdc++ combination
  can compile `std::expected`; this does not establish full library coverage.
- Development uses `-O2 -g`; release uses `-O3 -g`. Both retain assertions for
  now. Distribution binaries are stripped separately from the local binaries
  used for debugging and profiling.
- Project targets use `-fno-fast-math`, `-ffp-contract=off`, and
  `-fdenormal-fp-math=ieee`. Implicit FMA contraction can change rounding even
  without fast-math. An explicit FMA may still belong in a specified algorithm.
  These flags alone do not establish deterministic algorithms, parallel
  reductions, math-library results, or runtime FP state.
- ISA selection is explicit through `SIXDB_MARCH`; empty means the compiler's
  target baseline. `SIXDB_TUNE` independently selects generic, Granite Rapids,
  Zen 5, or Neoverse V2 compiler tuning and [source-level flags](tuning.md).
  Compiler/ISA/tuning/optimization variants use separate build directories.
  Unity builds and interprocedural optimization are off initially.
- Ordinary CMake targets and target-local dependencies, with `sixdb_target(name)`
  applying project settings. Prototypes are individually selected for
  configuration and excluded from the default build.
- Hidden symbol visibility, hidden inline visibility, and source-path remapping
  apply to project targets. Linux builds put functions/data in separate
  sections and discard unreferenced sections at link time. Public library
  entry points will need explicit export annotations when introduced.
- `sixdb_release_artifact(name)` adds a Linux executable packaging target,
  `name_dist`. It creates a stripped executable in `dist/bin/` and separate
  debugging information in `dist/symbols/`. Ship the former; retain the latter
  privately. Required dynamic-linking symbols remain. This is inexpensive
  friction for inspection, not protection against determined reverse engineering.

See [build iteration](build-iteration.md) for the Calico findings.

## Proposals for discussion

**Headers and namespaces.** Use `<module>/include/<module>/name.h` for shared
headers, `<module>/src/*.cpp` for compiled implementation, and adjacent private
headers. Use module namespaces: `ikea`, `orbital`, `loom`, `engine`, `shore`,
and `workbench`. They match ownership and include paths; a second `sixdb::`
prefix on every module seems unnecessary. Reserve `detail` for internal
helpers. Small prototypes can keep their `.h` and `.cpp` files together.

Headers include their own requirements. Compile shared orchestration and
validation once behind small declarations. Templates and code that benefits
from inlining can remain visible, especially Ikea kernel stages. Explicit
instantiation can help when the relevant type set is known. Avoid a blanket
PImpl, virtual-interface, or TU-size rule.

**CPS and specialization.** Compile reusable stage implementations and compose
recipes through explicit continuations. Inline within a stage where that pays;
do not instantiate every combination of stages, types, widths, and options by
default. Specialize selected compositions when measurements justify the extra
code. The precise stage ABI, calling convention, handoff state, and placement
of TU boundaries remain experiment questions. Compilation time, generated code
size, and runtime performance all matter when assessing a composition.

**Errors.** Use small typed status codes, or `std::expected<T, Error>` for a
value-or-failure result. Keep hot-path errors cheap and format diagnostics at
an appropriate boundary. Mark fallible results `[[nodiscard]]`. Avoid `.value()`
when failure is possible because that uses throwing access. Assertions express
programming errors, not recoverable failures. No common error type is fixed yet.

**Exceptions and RTTI.** Keep explicit error returns as the default API style
without imposing global `-fno-exceptions` yet: dependencies, allocation, and
adapters need an agreed failure boundary. RTTI stays at the compiler default
for now. Either can be disabled for a target with a defined contract. The
initial scaffold does not otherwise change them.

**Preprocessor flags.** Prefix project configuration with `SIXDB_`. For shared
booleans, generate `0` or `1` and use `#if SIXDB_FEATURE`; `#ifdef` is true even
when a defined flag has value zero. Keep platform/ISA capabilities separate
from runtime deployment choices. Distribution and durability should not become
compile-time switches merely because they have different operating modes.
The first source-level flags are the four `SIXDB_TUNE_*` booleans described
above, supplied by CMake. ISA feature availability uses compiler macros.
Other feature flags and a generated configuration header can be introduced
when needed.

## References

- [Clang C++ status](https://clang.llvm.org/cxx_status.html).
- [Clang floating-point controls](https://clang.llvm.org/docs/UsersManual.html#controlling-floating-point-behavior).
- [LLVM symbol stripping](https://llvm.org/docs/CommandGuide/llvm-strip.html).
- [Calico build documentation](../../../calico/BUILDING.md).
