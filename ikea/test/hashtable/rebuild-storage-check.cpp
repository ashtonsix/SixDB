#include <ikea/hashtable.h>
#include <map>
#include <iostream>
namespace ht = ikea::hashtable;
struct denied {};
struct counted_storage {
    struct context {
        int remaining = -1;
        unsigned alive = 0, total = 0;
    };
    using retention = ht::heap_storage_policy::retention;
    using trim_result = ht::heap_storage_policy::trim_result;
    struct buffer : ht::heap_storage_policy::buffer {
        context* memory = nullptr;
        static std::size_t admit(std::size_t n, context* c) {
            if (c) {
                if (c->remaining == 0)
                    throw denied{};
                if (c->remaining > 0)
                    --c->remaining;
            }
            return n;
        }
        buffer() = default;
        buffer(std::size_t n, context* c = nullptr, bool zero = true, std::size_t reserve = 0)
            : ht::heap_storage_policy::buffer(admit(n, c), nullptr, zero, reserve), memory(c) {
            if (c) {
                ++c->alive;
                ++c->total;
            }
        }
        buffer(buffer&& x) noexcept
            : ht::heap_storage_policy::buffer(std::move(x)),
              memory(std::exchange(x.memory, nullptr)) {}
        buffer& operator=(buffer&& x) noexcept {
            swap(x);
            return *this;
        }
        void swap(buffer& x) noexcept {
            ht::heap_storage_policy::buffer::swap(x);
            std::swap(memory, x.memory);
        }
        ~buffer() {
            if (memory)
                --memory->alive;
        }
    };
};
template <unsigned V> void check() {
    using D = ht::erased_table<24, V, counted_storage>;
    counted_storage::context memory;
    auto descriptor =
        D::template explicit_encoding<10, 14>(713, &memory, ht::write_strategy::existing);
    ht::permutation<24, 14> mix{713};
    std::map<ht::word, ht::bytes<V>> expected;
    for (unsigned i = 0; i < 600; ++i) {
        ht::bytes<V> v{};
        for (unsigned j = 0; j < V; ++j)
            v[j] = std::byte(i * 13 + j * 7);
        auto k = mix.recover(0, i);
        descriptor.insert(k, v);
        expected[k] = v;
    }
    for (unsigned i = 0; i < 600; i += 5) {
        auto k = mix.recover(0, i);
        descriptor.erase(k);
        expected.erase(k);
    }
    auto retained = descriptor.template read_native<10, 14>();
    const auto original_buffers = memory.alive;
    const auto* functions = descriptor.functions();
    bool completed = false;
    unsigned failures = 0;
    for (int failure = 0; failure < 32 && !completed; ++failure) {
        memory.remaining = failure;
        try {
            descriptor.template grow<10, 14>();
            completed = true;
        } catch (const denied&) {
            ++failures;
            ht::require(memory.alive == original_buffers && descriptor.functions() == functions,
                        "partial growth failure preserves descriptor and frees destination");
        }
        for (const auto& [k, v] : expected) {
            unsigned calls = 0;
            ht::require(descriptor.read().find(k,
                                               [&](const auto& got) {
                                                   ++calls;
                                                   ht::require(got == v, "growth failure value");
                                               }),
                        "growth failure membership");
            ht::require(calls == (V ? 1u : 0u), "growth callback count");
        }
    }
    ht::require(completed && failures > 1 && descriptor.functions() != functions,
                "growth reaches primary and Swiss allocation failures");
    ht::require(descriptor.writes() == ht::write_strategy::existing,
                "growth preserves write selection");
    memory.remaining = -1;
    descriptor.erase(expected.begin()->first);
    ht::require(retained.borrow().find(expected.begin()->first, [](const auto&) {}),
                "old source retained after growth and mutation");
    auto rhs = D::template explicit_encoding<11, 14>(713, &memory);
    ht::bytes<V> v{};
    const auto k = expected.rbegin()->first;
    rhs.insert(k, v);
    auto before = descriptor.read();
    auto* merge_functions = descriptor.functions();
    memory.remaining = 0;
    bool refused = false;
    try {
        auto copy = descriptor.clone();
        (void)copy;
    } catch (const denied&) {
        refused = true;
    }
    ht::require(refused && descriptor.functions() == merge_functions,
                "caller copy refusal preserves source descriptor");
    memory.remaining = -1;
    refused = false;
    try {
        auto disposable = descriptor.clone();
        disposable.template reserve_overflow<11, 14>(disposable.size() + rhs.size());
        (void)disposable.template accumulate<11, 14>(
            rhs, [](const auto&, const auto&) -> ht::bytes<V> { throw denied{}; });
    } catch (const denied&) {
        refused = true;
    }
    ht::require(refused && descriptor.functions() == merge_functions,
                "failed disposable accumulation preserves caller source");
    auto next = descriptor.clone();
    next.template reserve_overflow<11, 14>(next.size() + rhs.size());
    const auto allocations = memory.total;
    memory.remaining = 0;
    ht::require(next.template accumulate<11, 14>(rhs, [](auto, auto b) { return b; }) ==
                    ht::accumulation_status::complete,
                "prepared descriptor accumulation");
    ht::require(memory.total == allocations, "mutable leaf allocates nothing");
    descriptor = std::move(next);
    memory.remaining = -1;
    ht::require(descriptor.writes() == ht::write_strategy::existing,
                "caller clone and accumulation preserve write selection");
    descriptor.read().find(k, [&](const auto& got) { ht::require(got == v, "merged value"); });
    before.find(k, [&](const auto& got) {
        ht::require(got == expected.at(k), "caller publication retains previous backing");
    });
    descriptor.validate();
}
int main() {
    check<0>();
    check<8>();
    check<64>();
    std::cout << "storage-aware growth/merge refusal and retained publication passed\n";
}
