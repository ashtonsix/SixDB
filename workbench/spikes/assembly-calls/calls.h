#pragma once
#include <cstddef>
#include <cstdint>

// Prototype-local scalar ABI. No production module ABI changes.
// The bridge returns a demand code or UINT64_MAX for invalid input.
extern "C" std::uint64_t prototype_bind_demand(std::size_t bytes, std::size_t minimum,
                                               unsigned pattern) noexcept;
extern "C" std::uint64_t prototype_handwritten(std::size_t bytes, std::size_t minimum,
                                               unsigned pattern, std::uint64_t salt) noexcept;
extern "C" std::uint64_t prototype_seed(std::size_t bytes, std::size_t minimum,
                                       unsigned pattern, std::uint64_t salt) noexcept;

// The pointer is a live const PrototypeTable, kept/constructed by C++ setup.
extern "C" std::uint64_t prototype_ikea_contains(const void* table, std::uint64_t key) noexcept;
extern "C" std::uint64_t prototype_ikea_lookup(const void* table, std::uint64_t key) noexcept;
