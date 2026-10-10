#pragma once

#include <loom/ring.h>
#include <orbital/mvcc_baseline.h>

namespace loom_metadata {
// Mechanical packet controls, not a proposed version/ownership contract.
struct Packet8 { const void* anchor; };
struct Packet16 { const void* anchor; std::uint64_t cut; };
struct Packet32 { const void* anchor; const void* descriptor; std::uint64_t cut, generation; };

// A two-word status encoding without stealing pointer or generation bits.
// Success uses both pointers; a null first word selects the exact Error.
struct Result16 {
  const void* anchor;
  union Detail { const void* descriptor; orbital::Error error; } detail;
  static Result16 success(const void* anchor, const void* descriptor) {
    return {anchor, {.descriptor = descriptor}};
  }
  static Result16 failure(orbital::Error error) { return {nullptr, {.error = error}}; }
};

Packet8 pass8(Packet8) noexcept;
Packet16 pass16(Packet16) noexcept;
Packet32 pass32(Packet32) noexcept;
orbital::Result<Packet8> checked8(Packet8) noexcept;
orbital::Result<Packet16> checked16(Packet16) noexcept;
orbital::Result<Packet32> checked32(Packet32) noexcept;
Result16 pass_result16(Result16) noexcept;
orbital::Progress pass_progress(orbital::Progress) noexcept;
orbital::object::View pass_view(orbital::object::View) noexcept;
orbital::object::Selection pass_selection(orbital::object::Selection) noexcept;
orbital::Result<orbital::object::View> checked_view(orbital::object::View) noexcept;
orbital::Result<orbital::object::Selection> checked_selection(orbital::object::Selection) noexcept;
orbital::Result<orbital::Bytes> checked_bytes(orbital::Bytes) noexcept;
orbital::object::RetainedView move_view(orbital::object::RetainedView) noexcept;
orbital::Result<orbital::object::RetainedView> checked_owner(orbital::object::RetainedView) noexcept;
orbital::Result<orbital::allocation::Allocation> move_allocation(orbital::allocation::Allocation) noexcept;
orbital::Result<orbital::allocation::Hold> move_hold(orbital::allocation::Hold) noexcept;
orbital::Result<loom::detail::RingStorage> move_bank(loom::detail::RingStorage) noexcept;

using Word = std::uint64_t;
} // namespace loom_metadata

namespace object_access_check { struct NativeBody; }
namespace loom_metadata {
Word native_word(const object_access_check::NativeBody&, Word);
}
