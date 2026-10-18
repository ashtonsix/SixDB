#pragma once
#include <orbital/detail/object/representation.h>
#include <orbital/detail/allocation/capture.h>
namespace orbital::object_detail {
struct SourceAccess {
  static bool retained(const PartSource& source) { return source.retained.kind_ != RetainedSource::Kind::none; }
  static bool shared(const PartSource& source, PartOwners owners) {
    if (retained(source)) return source.retained.kind_ == RetainedSource::Kind::shared;
    return mixed(owners) ? source.backing.use_count() != 0 : object_detail::shared(owners);
  }
  static const std::shared_ptr<const void>& backing(const PartSource& source) {
    return retained(source) ? *source.retained.shared_ : source.backing;
  }
  static allocation::Storage* storage(const PartSource& source) {
    return retained(source) ? source.retained.allocation_ : source.allocation ? source.allocation->storage : nullptr;
  }
  static RetainedSource certificate(Bytes body, std::size_t padding, allocation::Storage* owner) {
    RetainedSource result;
    result.footprint_ = {body.data(), body.size() + padding};
    result.kind_ = RetainedSource::Kind::allocation; result.allocation_ = owner;
    return result;
  }
  static RetainedSource certificate(Bytes body, std::size_t padding, const std::shared_ptr<const void>& owner) {
    RetainedSource result;
    result.footprint_ = {body.data(), body.size() + padding};
    result.kind_ = RetainedSource::Kind::shared; result.shared_ = &owner;
    return result;
  }
  static bool within(const PartSource&, std::size_t padding);
};
bool valid(Bytes, std::size_t padding = 0);
inline Result<void> capture_ready(allocation::Allocation* source) noexcept {
  return source && *source ? allocation::detail::ready_for_capture(*source) : Result<void>{};
}
inline allocation::Storage* take_source(allocation::Allocation* source) noexcept {
  return source && *source ? allocation::detail::take_for_capture(*source) : nullptr;
}
void drop(allocation::Storage*);
Result<allocation::Storage*> pin(allocation::Storage*);
bool within(const allocation::Allocation&, Bytes, std::size_t padding);
bool unique_allocations(std::span<const PartSource>, std::span<allocation::Storage*> scratch);
} // namespace orbital::object_detail
