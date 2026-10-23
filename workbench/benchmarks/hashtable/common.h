#pragma once
#include <ikea/hashtable/types.h>
#include <chrono>
#include <span>
#include <string>

namespace screen {
using byte = std::byte;
using ikea::hashtable::word;
using clock = std::chrono::steady_clock;
struct options {
    std::string operation = "bench", owner = "ikea", cpu, isa;
    unsigned source_k = 30, source_b = 10, inputs = 8;
    word seed = 713;
    unsigned rep = 0, repetitions = 5, miss_percent = 0, occupancy_percent = 85;
    unsigned new_percent = 0, overlap_percent = 50;
    std::size_t requests = 1048576, conditioning_bytes = 0;
};
options parse(int argc, char** argv);
// Payload construction is inside timed RMW consumers; preserve the original
// inline mixer there rather than adding calls per byte.
inline word random_bits(word x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
void condition(std::span<const byte>);
double elapsed(clock::time_point);
} // namespace screen
