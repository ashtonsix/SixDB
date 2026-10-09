#include "probe.h"

namespace loom_metadata {
Word caller8(Packet8 value) { return reinterpret_cast<std::uintptr_t>(pass8(value).anchor); }
Word caller16(Packet16 value) {
  const auto result = pass16(value);
  return reinterpret_cast<std::uintptr_t>(result.anchor) ^ result.cut;
}
Word caller32(Packet32 value) {
  const auto result = pass32(value);
  return reinterpret_cast<std::uintptr_t>(result.anchor) ^
      reinterpret_cast<std::uintptr_t>(result.descriptor) ^ result.cut ^ result.generation;
}
Word caller_checked16(Packet16 value) {
  const auto result = checked16(value);
  return result ? reinterpret_cast<std::uintptr_t>(result->anchor) ^ result->cut
                : static_cast<Word>(result.error());
}
Word caller_result16(Result16 value) {
  const auto result = pass_result16(value);
  return result.anchor ? reinterpret_cast<std::uintptr_t>(result.anchor) ^
                         reinterpret_cast<std::uintptr_t>(result.detail.descriptor)
                       : static_cast<Word>(result.detail.error);
}
Word caller_progress(orbital::Progress value) {
  const auto result = pass_progress(value);
  return result.kind() == orbital::Progress::Kind::waiting ? result.observed_generation()
      : static_cast<Word>(result.kind()) ^ static_cast<Word>(result.failure());
}
std::size_t caller_selection(orbital::object::Selection value) {
  return pass_selection(value).part_count();
}
std::size_t caller_checked_selection(orbital::object::Selection value) {
  const auto result = checked_selection(value);
  return result ? result->part_count() : static_cast<Word>(result.error());
}
orbital::Bytes caller_checked_view(orbital::object::View value) {
  const auto result = checked_view(value);
  return result ? result->body() : orbital::Bytes{};
}
// Real Object boundaries, compared with retaining state once in an operation.
orbital::Result<orbital::object::Selection> discover(const orbital::object::ReadContext& context,
    orbital::ObjectId id) { return orbital::object::select(context, id); }
orbital::Result<orbital::object::View> resolve(orbital::object::Selection selected, std::size_t index) {
  return selected.part(index);
}
orbital::object::RetainedView retain_once(orbital::object::View view) { return orbital::object::retain(view); }
Word prepared_caller(const object_access_check::NativeBody& body, Word count) {
  Word sum = 0;
  for (Word row = 0; row < count; ++row) sum += native_word(body, row);
  return sum;
}
} // namespace loom_metadata
