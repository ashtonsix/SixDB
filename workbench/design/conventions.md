# Build and code conventions

## Current build settings

[BUILDING.md](../../BUILDING.md) owns the compiler pin, presets, target selection
and release packaging. [Tuning](tuning.md) owns ISA and microarchitecture flags.
Settings are applied by [sixdb_target](../../cmake/Targets.cmake), keeping ordinary
CMake declarations and target-local dependencies.

C++23 is selected with language extensions off. First-party targets disable
fast-math, implicit FMA contraction and non-IEEE denormal handling. Explicit FMA
can belong in a specified algorithm; these flags alone do not make algorithms,
parallel reductions or math-library results deterministic.

Unity builds and LTO are off. Source paths are remapped; hidden visibility and
Linux section garbage collection reduce exposed names and unused code. A future
shared-library API will need explicit exports. Distribution strips binaries
while keeping private debug information: inexpensive friction against casual
inspection, not a bespoke obfuscation scheme.

## Source boundaries and composition

Ikea uses `include/ikea/`, `src/` and the `ikea` namespace. The proposed default
for other modules is the same shape with their own name; private helpers use
`detail` or adjacent private headers. Small prototypes can keep files together.
Headers include their own requirements.

Compile shared orchestration and validation once behind declarations. Keep
kernel templates and useful inlining visible. Reuse stages across compositions
and specialize where measurements justify the runtime, build-time and code-size
trade-off. [Ikea's stage guide](../../ikea/docs/extension.md) owns its
implemented model; it is not a prescribed ABI for every module.

## Open choices

**Errors.** Prefer small typed status codes or `std::expected<T, Error>`;
format diagnostics at the appropriate boundary. Mark fallible results
`[[nodiscard]]`. Avoid throwing `.value()` access when failure is possible.
Assertions express programming errors. No common project error type is fixed.

**Exceptions and RTTI.** Both stay at compiler defaults. Explicit error returns
are the preferred API style, but dependencies, allocation and adapters still
need agreed failure boundaries before disabling exceptions globally. Targets
can disable either facility when their contract permits.

**Configuration flags.** Use `SIXDB_` for project settings. Shared booleans use
`0`/`1` and `#if`, as the current tuning flags do. Compiler macros establish ISA
availability; deployment modes are separate. Introduce other flags or a generated
configuration header when a concrete consumer needs them.

Standard-library, linker and sysroot pins remain open; workers record installed
package versions. Header conventions above are a starting proposal, not a
migration request for every prototype.

## Documentation and comments

Use concise `///` comments for non-obvious caller contracts and ordinary comments
for implementation invariants and reasons. Skip signature narration and repeated
shared obligations. Guides teach through examples, specifications own exact
semantics, and measurements stay with evidence. Replace stale explanations.
