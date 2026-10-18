#pragma once
#include "../../author/table.h"
#include "../bucket-write.h"
#include "../control.h"
namespace ikea::hashtable {
/// Fixed-capacity accumulation never allocates. Discard destination on failure.
enum class accumulation_status { complete, incompatible, alias, needs_overflow };
namespace detail::accumulation {
namespace ht = ikea::hashtable;
using ht::word;
using ht::byte;
using access = ht::author::table_access;
using merge_status = accumulation_status;
using detail::control_empty;
using detail::control_tombstone;
inline constexpr auto none = detail::no_slot;
} // namespace detail::accumulation
} // namespace ikea::hashtable
