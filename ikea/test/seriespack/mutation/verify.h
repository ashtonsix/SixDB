#pragma once
#include <ikea/seriespack/detail/mutation/physical.h>
#include <array>
#include <vector>

void verify_mutation_coverage(const std::array<ikea::seriespack::plane<std::uint8_t>, 3>& planes,
                              const std::array<std::vector<std::uint8_t>, 3>& before,
                              std::span<const ikea::seriespack::byte_write> writes,
                              std::array<std::size_t, 3> occupied);
