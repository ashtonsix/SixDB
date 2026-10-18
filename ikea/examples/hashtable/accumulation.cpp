#include <ikea/hashtable.h>
#include <cstring>
namespace ht = ikea::hashtable;
ht::bytes<8> value(std::uint64_t n) {
    ht::bytes<8> out{};
    std::memcpy(out.data(), &n, 8);
    return out;
}
int main() {
    using Worker = ht::table<24, 10, 8, 14>;
    using Result = ht::table<24, 11, 8, 14>;
    Worker first(713), second(713);
    first.insert(42, value(7));
    second.insert(42, value(11));
    second.insert(43, value(13));
    Result result(713); // The caller chooses final geometry and allocation context.
    ht::reserve_overflow(result, first.size() + second.size()); // Conservative external bound.
    auto add = [](const auto& a, const auto& b) {
        std::uint64_t x, y;
        std::memcpy(&x, a.data(), 8);
        std::memcpy(&y, b.data(), 8);
        return value(x + y);
    };
    for (const auto* source : {&first, &second})
        if (ht::accumulate(result, *source, add) != ht::accumulation_status::complete)
            return 1; // Discard result.
    bool correct = false;
    result.find(42, [&](const auto& bytes) { correct = bytes == value(18); });
    if (!correct || result.size() != 2 || first.size() != 1 || second.size() != 2)
        return 2;
    // An immutable result starts with an explicit caller copy, then uses the same leaf.
    auto preserved = first.clone();
    ht::reserve_overflow(preserved, first.size() + second.size());
    if (ht::accumulate(preserved, second, add) != ht::accumulation_status::complete)
        return 3;
    bool original = false;
    first.find(42, [&](const auto& bytes) { original = bytes == value(7); });
    return original ? 0 : 4;
}
