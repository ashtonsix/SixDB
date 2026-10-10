#pragma once
#include <orbital/detail/object/representation.h>
namespace orbital::object_detail {
bool valid(Bytes, std::size_t padding = 0);
Result<allocation::Storage*> hold_source(allocation::Allocation*);
void drop(allocation::Storage*);
Result<allocation::Storage*> pin(allocation::Storage*);
bool within(const allocation::Allocation&, Bytes, std::uint32_t padding);
bool unique_allocations(std::pmr::memory_resource&, std::span<const PartSource>);
} // namespace orbital::object_detail
