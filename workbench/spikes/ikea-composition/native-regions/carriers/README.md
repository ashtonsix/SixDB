# Deferred reduction carrier

This workbench diagnostic keeps the measured B result-representation alternative
available beside the current physical/composition implementation. It adds
`authored-u64-carrier` to the composition benchmark. The existing authored,
direct, materialized, and dense32/dense64 cases retain their labels and oracles.
The full AVX-512-enabled x86 profile has 136 composition cases, including 32 new
carrier cases; NEON has 64, including 16 new carrier cases.

`carrier.h` contains the target-specific adapters from the B probe. Each inherits
the current tile Ops and changes only the representation returned by `sum`.
The same generic `composition::selected_sum` still performs the authored read,
comparison, and sum. Tile grain, original coordinates, and active masks remain
those of the ordinary authored case. The carrier plan does not use dense-region
substitution or an intermediate values array.

A returned carrier owns native u64 partial sums representing **one logical
modulo-u64 fragment sum**. The driver combines carriers with native u64 addition
and interprets the accumulated result once after 8192 original positions. Byte
lanes use SAD on x86; halfword pairs reach u64 before crossing fragments; dword
pairs and qword values likewise accumulate in u64. NEON uses widening pair adds.
There is no hidden accumulator mutation in Ops. These are workbench result types;
no Ikea interface, erased calling convention, or suspension/publication ABI is
established. Source views and encoded owners remain live throughout the immediate
read; returned carriers retain no source references.

The benchmark runs the B untimed checks before each new case: seven runtime
cutoffs for both selections, plus K56, K60/H8, and K64 fixtures whose selected sums
exceed 2^64. Carrier, ordinary authored, and direct results must match the original
unsigned scalar oracle. `check.cpp` runs those checks without benchmark timing;
link it with the composition benchmark TU and Google Benchmark, using function
sections and linker garbage collection. It is separate from the normal benchmark
main. The registered materialized and dense controls retain their existing checks.

The initial integration preserved the nine existing fixture/control/helper
bodies from benchmark SHA `04da2c2320a85f10db23fa9fccc7f569499e625fe600a8dd12aff886bb6a873d`.
Carrier adapter and driver/check bodies came unchanged from B overlay SHA
`bc7559efca7c1325a9ceb24473ccbe8ca077a1e5999e2ba816a598b72d49904f`.
Initial local compilation/checks use headers identical to coherent capture
`db9238fea0ea2294b974c34a63447571ec068c2aedf0310f29ec5564c5da8d00`:
pinned Clang 21.1.8, native NEON with V2 tuning, and x86 cross-compilation with Zen5
tuning. Native NEON checks pass; x86 is compile-only evidence. These checks do
not establish performance on the new physical checkpoint. Preserve the original B evidence separately when measuring
all current plans together.
