#pragma once
#include "access.h"
namespace selected::consumer {
Result<std::uint64_t> word(const ReadContext&, Id object, Id part, Id format);
Result<std::uint64_t> word(Selection, Id part, Id format);
}
