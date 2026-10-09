#pragma once

#include "../orbital-representation-conversion/fixture.h"
#include <cassert>
#include <cstdint>
#include <vector>

namespace stage2_composition {
namespace fixture = orbital_conversion_fixture;

inline fixture::Schema schema() { return {{1, 7, 9, 20, 32, 63, 64, 12}}; }

// Logical input is the oracle. It is independent of either encoded form and
// remains available after both source and target descriptor handles are gone.
inline std::vector<fixture::Record> records(std::size_t count, unsigned revision = 0) {
  const auto fields = schema().widths;
  std::vector<fixture::Record> result(count);
  for (std::size_t row = 0; row < count; ++row) {
    for (std::size_t field = 0; field < fields.size(); ++field) {
      const auto mask = fields[field] == 64 ? UINT64_MAX : (UINT64_C(1) << fields[field]) - 1;
      auto value = UINT64_C(0x9e3779b97f4a7c15) * (row + 1) +
          UINT64_C(0xd1b54a32d192ed03) * (field + 1) + revision;
      value ^= value >> 29;
      result[row][field] = value & mask;
    }
  }
  if (count > 0) result[0] = {};
  if (count > 1) result[1] = {1, 127, 511, 1048575, UINT64_C(4294967295),
      UINT64_C(9223372036854775807), UINT64_MAX, 4095};
  if (count > 2) result[2] = {0, 65, 257, 524289, UINT64_C(2147483649),
      UINT64_C(4611686018427387905), UINT64_C(9223372036854775809), 2049};
  if (revision && count > 0) result[0][0] = revision & 1;
  return result;
}

inline void matches(const std::vector<fixture::Record>& actual,
                    const std::vector<fixture::Record>& expected) {
  assert(actual.size() == expected.size());
  for (std::size_t row = 0; row < expected.size(); ++row)
    for (std::size_t field = 0; field < fixture::max_fields; ++field)
      assert(actual[row][field] == expected[row][field]);
}
} // namespace stage2_composition
