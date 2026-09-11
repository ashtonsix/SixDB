# Microarchitecture tuning and ISA availability

Two independent build settings apply to first-party targets:

- `SIXDB_MARCH` selects the compiler's allowed instruction set with `-march`.
  An empty value leaves the target baseline. Compiler feature macros such as
  `__AVX512F__`, `__AVX2__`, and `__ARM_FEATURE_SVE2` describe enabled features
  for the TU. These are compile-time capabilities, not runtime CPU detection.
- `SIXDB_TUNE` selects a microarchitectural cost model with `-mtune`, plus
  explicit SixDB flags for choosing software tuning policies. It does not
  enable instructions or select an ISA baseline.

| `SIXDB_TUNE` | Clang option | SixDB flag set to `1` |
| --- | --- | --- |
| `generic` (default) | `-mtune=generic` | `SIXDB_TUNE_GENERIC` |
| `granite-rapids` | `-mtune=graniterapids` | `SIXDB_TUNE_GRANITE_RAPIDS` |
| `zen5` | `-mtune=znver5` | `SIXDB_TUNE_ZEN5` |
| `neoverse-v2` | `-mtune=neoverse-v2` | `SIXDB_TUNE_NEOVERSE_V2` |

All four flags are defined as `0` or `1`; exactly one is `1`. Use `#if` for
them, not `#ifdef`. They arrive through `sixdb_target(name)` and its build-options
dependency, with no generated header to include. Unsupported tuning choices
for the configured compiler target fail at configuration. There is no host CPU
autodetection. Even generic tuning is explicit, so a CPU name passed to
`-march` cannot silently choose a different compiler tuning policy.

For example, build on an x86 builder with an AVX2-capable baseline while tuning
for Zen 5:

```sh
cmake -S . -B build/clang/zen5-v3 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5
```

The same ISA baseline can be used with `-DSIXDB_TUNE=granite-rapids` in another
build directory. On an AArch64 builder, `-DSIXDB_MARCH=armv8-a
-DSIXDB_TUNE=neoverse-v2` selects Neoverse V2 tuning without enabling SVE2.
An appropriate explicit `-march` value can enable additional instructions
independently. These examples demonstrate separation, not a chosen fleet ISA
policy. Use a cross toolchain when compiling for another architecture.

In source, gate instruction legality and microarchitectural preference separately:

```cpp
#if defined(__AVX512F__) && SIXDB_TUNE_ZEN5
// An AVX-512 implementation with a policy selected for Zen 5.
#endif

#if defined(__ARM_FEATURE_SVE2) && SIXDB_TUNE_NEOVERSE_V2
// An SVE2 implementation with a policy selected for Neoverse V2.
#endif
```

Tuning choices can guide scheduling, prefetch policy, unrolling, and stage
implementations as experiments establish useful values. They must preserve
the algorithm's results and serialization contracts. No cache sizes, unroll
counts, or SIMD widths are prescribed by these flags.

The [build check](../tools/tests/check_build.py) compiles probes with Clang 21.1.8 for
x86-64 and AArch64. It checks the flag values and verifies that changing tuning
at a fixed ISA leaves compiler feature macros unchanged, while explicit ISA
changes enable AVX-512/SVE2 independently. Cross-compiled probes are not run;
this is build validation, not CPU performance evidence.

Reference: [Clang target options](https://clang.llvm.org/docs/ClangCommandLineReference.html#target-dependent-compilation-options).
