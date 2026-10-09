#include "probe.h"

namespace loom_metadata {
Packet8 pass8(Packet8 value) noexcept { return value; }
Packet16 pass16(Packet16 value) noexcept { ++value.cut; return value; }
Packet32 pass32(Packet32 value) noexcept { ++value.cut; ++value.generation; return value; }
orbital::Result<Packet8> checked8(Packet8 value) noexcept { return value; }
orbital::Result<Packet16> checked16(Packet16 value) noexcept { return value; }
orbital::Result<Packet32> checked32(Packet32 value) noexcept { return value; }
Result16 pass_result16(Result16 value) noexcept { return value; }
orbital::Progress pass_progress(orbital::Progress value) noexcept { return value; }
orbital::object::View pass_view(orbital::object::View value) noexcept { return value; }
orbital::object::Selection pass_selection(orbital::object::Selection value) noexcept { return value; }
orbital::Result<orbital::object::View> checked_view(orbital::object::View value) noexcept { return value; }
orbital::Result<orbital::object::Selection> checked_selection(orbital::object::Selection value) noexcept { return value; }
orbital::Result<orbital::Bytes> checked_bytes(orbital::Bytes value) noexcept { return value; }
orbital::object::RetainedView move_view(orbital::object::RetainedView value) noexcept { return value; }
orbital::Result<orbital::object::RetainedView> checked_owner(orbital::object::RetainedView value) noexcept {
  return std::move(value);
}
orbital::Result<orbital::allocation::Allocation> move_allocation(orbital::allocation::Allocation value) noexcept {
  return std::move(value);
}
orbital::Result<orbital::allocation::Hold> move_hold(orbital::allocation::Hold value) noexcept {
  return std::move(value);
}
orbital::Result<loom::detail::RingStorage> move_bank(loom::detail::RingStorage value) noexcept {
  return std::move(value);
}
} // namespace loom_metadata
