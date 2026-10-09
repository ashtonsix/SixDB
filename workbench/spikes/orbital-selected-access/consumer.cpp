#include "consumer.h"
#include <cstring>

namespace selected::consumer {
Result<std::uint64_t> word(Selection selection, Id part, Id format) {
  auto view = ready(selection, part, {format, 0, sizeof(std::uint64_t)});
  if (!view) return std::unexpected(view.error());
  std::uint64_t value;
  std::memcpy(&value, view->bytes().data(), sizeof(value));
  return value;
}

Result<std::uint64_t> word(const ReadContext& context, Id object, Id part, Id format) {
  auto selection = bind(context, object);
  if (!selection) return std::unexpected(selection.error());
  return word(*selection, part, format);
}
} // namespace selected::consumer
