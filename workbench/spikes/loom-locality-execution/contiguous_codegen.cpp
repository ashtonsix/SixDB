#include "contiguous.h"

// Standalone compiler controls; no timing claim. Dynamic exact plans exercise
// the single switch, while fixed classes expose the branchless selected bodies.
extern "C" void contiguous_selected_l1(const loom_locality::ContiguousPlan& plan,
    const void* address) noexcept {
  loom_locality::prefetch<3>(plan, address);
}
extern "C" void contiguous_selected_l2(const loom_locality::ContiguousPlan& plan,
    const void* address) noexcept {
  loom_locality::prefetch<2>(plan, address);
}
extern "C" void contiguous_unique_selected_l1(const loom_locality::ContiguousPlan& plan,
    const void* address) noexcept {
  loom_locality::prefetch_unique<3>(plan, address);
}
extern "C" void contiguous_unique_selected_l2(const loom_locality::ContiguousPlan& plan,
    const void* address) noexcept {
  loom_locality::prefetch_unique<2>(plan, address);
}

#define LOOM_CONTIGUOUS_FIXED(LINE, COUNT) \
  extern "C" void contiguous_fixed_##LINE##_##COUNT(const void* address, std::size_t tail) noexcept { \
    loom_locality::detail::Prefetch<3> sink; \
    loom_locality::detail::emit_class<LINE>(static_cast<const char*>(address), tail, sink, \
        std::make_index_sequence<COUNT>{}); \
  }
LOOM_CONTIGUOUS_FIXED(64, 1) LOOM_CONTIGUOUS_FIXED(64, 2)
LOOM_CONTIGUOUS_FIXED(64, 3) LOOM_CONTIGUOUS_FIXED(64, 4)
LOOM_CONTIGUOUS_FIXED(64, 5) LOOM_CONTIGUOUS_FIXED(64, 6)
LOOM_CONTIGUOUS_FIXED(64, 7) LOOM_CONTIGUOUS_FIXED(64, 8)
LOOM_CONTIGUOUS_FIXED(128, 1) LOOM_CONTIGUOUS_FIXED(128, 2)
LOOM_CONTIGUOUS_FIXED(128, 3) LOOM_CONTIGUOUS_FIXED(128, 4)
LOOM_CONTIGUOUS_FIXED(128, 5) LOOM_CONTIGUOUS_FIXED(128, 6)
LOOM_CONTIGUOUS_FIXED(128, 7) LOOM_CONTIGUOUS_FIXED(128, 8)
#undef LOOM_CONTIGUOUS_FIXED

#define LOOM_CONTIGUOUS_UNIQUE_FIXED(LINE, COUNT) \
  extern "C" void contiguous_unique_fixed_##LINE##_##COUNT(const void* address, \
      std::size_t following_offset, std::size_t tail) noexcept { \
    loom_locality::detail::Prefetch<3> sink; \
    loom_locality::detail::emit_unique_class<LINE, COUNT>(static_cast<const char*>(address), \
        following_offset, tail, sink, std::make_index_sequence<COUNT - 1>{}); \
  }
LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 1) LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 2)
LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 3) LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 4)
LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 5) LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 6)
LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 7) LOOM_CONTIGUOUS_UNIQUE_FIXED(64, 8)
LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 1) LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 2)
LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 3) LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 4)
LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 5) LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 6)
LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 7) LOOM_CONTIGUOUS_UNIQUE_FIXED(128, 8)
#undef LOOM_CONTIGUOUS_UNIQUE_FIXED

namespace {
// Separate compiler experiment: unique immutable class identity inhibits suffix
// merging without emitted assembly, variable operands, or memory/GPR clobbers.
// This is neither the default emitter nor a proposed product assembly policy.
template<std::size_t LineBytes, std::size_t Count, unsigned Locality>
[[gnu::always_inline]] inline void identity_class(const char* base, std::size_t tail) noexcept {
  loom_locality::detail::Prefetch<Locality> sink;
  loom_locality::detail::emit_class<LineBytes>(base, tail, sink, std::make_index_sequence<Count>{});
  asm volatile("" : : "i"(LineBytes), "i"(Count));
}

template<unsigned Locality>
[[gnu::always_inline]] inline void identity_selected(const loom_locality::ContiguousPlan& plan,
    const void* address) noexcept {
  const auto* base = static_cast<const char*>(address);
  const auto tail = plan.tail_offset();
#define LOOM_CONTIGUOUS_IDENTITY_CASE(CODE, LINE, COUNT) \
  case CODE: return identity_class<LINE, COUNT, Locality>(base, tail)
  switch (plan.code()) {
    LOOM_CONTIGUOUS_IDENTITY_CASE(0, 64, 1); LOOM_CONTIGUOUS_IDENTITY_CASE(1, 64, 2);
    LOOM_CONTIGUOUS_IDENTITY_CASE(2, 64, 3); LOOM_CONTIGUOUS_IDENTITY_CASE(3, 64, 4);
    LOOM_CONTIGUOUS_IDENTITY_CASE(4, 64, 5); LOOM_CONTIGUOUS_IDENTITY_CASE(5, 64, 6);
    LOOM_CONTIGUOUS_IDENTITY_CASE(6, 64, 7); LOOM_CONTIGUOUS_IDENTITY_CASE(7, 64, 8);
    LOOM_CONTIGUOUS_IDENTITY_CASE(8, 128, 1); LOOM_CONTIGUOUS_IDENTITY_CASE(9, 128, 2);
    LOOM_CONTIGUOUS_IDENTITY_CASE(10, 128, 3); LOOM_CONTIGUOUS_IDENTITY_CASE(11, 128, 4);
    LOOM_CONTIGUOUS_IDENTITY_CASE(12, 128, 5); LOOM_CONTIGUOUS_IDENTITY_CASE(13, 128, 6);
    LOOM_CONTIGUOUS_IDENTITY_CASE(14, 128, 7); LOOM_CONTIGUOUS_IDENTITY_CASE(15, 128, 8);
    case 16: return;
  }
#undef LOOM_CONTIGUOUS_IDENTITY_CASE
  __builtin_unreachable();
}
} // namespace

extern "C" void contiguous_identity_selected_l1(const loom_locality::ContiguousPlan& plan,
    const void* address) noexcept {
  identity_selected<3>(plan, address);
}
extern "C" void contiguous_identity_selected_l2(const loom_locality::ContiguousPlan& plan,
    const void* address) noexcept {
  identity_selected<2>(plan, address);
}
