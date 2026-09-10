#pragma once
#include <cstddef>
#include <cstdint>

namespace ikea::integers::wide56::study {
struct Codec {
    std::uint64_t (*point)(const std::uint8_t*,unsigned);
    void (*get16)(const std::uint8_t*,unsigned,std::uint64_t*);
    void (*decode)(const std::uint8_t*,std::uint64_t*);
    void (*encode)(const std::uint64_t*,std::uint8_t*);
    std::uint64_t (*sum)(const std::uint8_t*);
};
enum class Wire { aos7, planes, plain };
struct Arm { const char* name; Codec codec; Wire wire; std::size_t bytes; };
extern const Arm prior, fixed_planes, plain;
}
