#include <ikea/hashtable/table.h>
#include <ikea/hashtable/author/table.h>
#include <iostream>
#include <random>
namespace ht = ikea::hashtable;
using T = ht::table<16, 0, 8>;
std::vector<std::size_t> expected(T& owner, ht::word key) {
    const auto image = ht::author::table_access::get(owner);
    const auto h = image.mix(key);
    const auto* primary = image.primary.data;
    for (unsigned row = 0; row < T::N; ++row)
        if (!(primary[T::flags_offset + row / 8] & (1u << (row % 8))) &&
            ht::point<T::encoding>(primary, row, T::N) == h.quotient)
            return {};
    if (!primary[T::overflow_offset]) return {};
    std::vector<std::size_t> slots;
    auto group = h.swiss % image.groups;
    std::size_t step = 0;
    for (std::size_t visited = 0; visited < image.groups; ++visited) {
        bool empty = false;
        for (unsigned lane = 0; lane < 16; ++lane) {
            const auto index = group * 16 + lane;
            const auto code = image.controls.data[index];
            empty |= code == 0;
            if (code == h.fingerprint()) {
                slots.push_back(index);
                if (ht::load<T::key_bytes>(image.slots.data + index * T::tuple_bytes) == key)
                    return slots;
            }
        }
        if (empty) return slots;
        group = (group + (++step)) % image.groups;
    }
    ht::require(false, "trace oracle termination");
    return {};
}
int main() {
    std::mt19937_64 rng(321);
    T owner(713);
    std::size_t queries = 0, tuple_checks = 0;
    ht::word digest = 0;
    for (unsigned epoch = 0; epoch < 8; ++epoch) {
        for (unsigned i = 0; i < 1700; ++i) {
            const auto key = rng() % 4096;
            if (i % 3) owner.insert(key);
            else owner.erase(key);
        }
        owner.validate();
        for (unsigned i = 0; i < 12000; ++i) {
            const auto key = rng() % 8192;
            const auto oracle = expected(owner, key);
            ht::batch::tuple_trace.clear();
            const bool found = owner.find(key, [](auto) {});
            asm volatile("" : : "r"(found) : "memory");
            ht::require(ht::batch::tuple_trace == oracle, "actual tuple accesses and order");
            digest = (digest ^ ht::batch::tuple_trace.size()) * 0x9e3779b185ebca87ULL;
            for (auto slot : ht::batch::tuple_trace)
                digest = (digest ^ slot) * 0x9e3779b185ebca87ULL;
            ++queries;
            tuple_checks += oracle.size();
        }
    }
    std::cout << queries << " actual lookups: identical scalar tuple-check sequence; "
              << tuple_checks << " full-key checks; trace digest " << digest << '\n';
}
