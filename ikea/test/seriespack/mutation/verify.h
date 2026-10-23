#pragma once
#include <ikea/seriespack/author/mutation.h>
#include <array>
#include <vector>

void verify_mutation_coverage(const void* source, const std::array<ikea::seriespack::plane<ikea::byte>, 3>& planes,
                              const std::array<std::vector<std::uint8_t>, 3>& before,
                              std::span<const ikea::journal::entry> writes,
                              std::array<std::size_t, 3> occupied);
