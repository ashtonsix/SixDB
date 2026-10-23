#include <ikea/hashtable/table.h>
#include "damage.h"
#include <iostream>
#include <map>

using namespace ikea::hashtable;
struct refused {};
struct counted_storage {
    struct context {
        int remaining = -1;
        unsigned alive = 0;
    };
    using retention = heap_storage_policy::retention;
    using trim_result = heap_storage_policy::trim_result;
    struct buffer : heap_storage_policy::buffer {
        context* owner = nullptr;
        static std::size_t admit(std::size_t n, context* c) {
            if (c) {
                if (c->remaining == 0)
                    throw refused{};
                if (c->remaining > 0)
                    --c->remaining;
            }
            return n;
        }
        buffer() = default;
        buffer(std::size_t n, context* c = nullptr, bool zero = true, std::size_t reserve = 0)
            : heap_storage_policy::buffer(admit(n, c), nullptr, zero, reserve), owner(c) {
            if (owner)
                ++owner->alive;
        }
        buffer(buffer&& other) noexcept
            : heap_storage_policy::buffer(std::move(other)),
              owner(std::exchange(other.owner, nullptr)) {}
        buffer& operator=(buffer&& other) noexcept {
            swap(other);
            return *this;
        }
        void swap(buffer& other) noexcept {
            heap_storage_policy::buffer::swap(other);
            std::swap(owner, other.owner);
        }
        ~buffer() {
            if (owner)
                --owner->alive;
        }
    };
};
template <unsigned V> bytes<V> payload(word i, unsigned j) {
    bytes<V> result{};
    for (unsigned n = 0; n < V; ++n)
        result[n] = std::byte(i * 29 + j * 71 + n * 13);
    return result;
}
template <unsigned V, class Owner>
void inspect(const Owner& owner, const std::map<word, std::vector<bytes<V>>>& expected,
             const std::vector<word>& keys) {
    require(owner.size() == expected.size(), "copy population");
    for (auto key : keys) {
        std::vector<bytes<V>> actual;
        const bool found = owner.find(key, [&](const auto& value) { actual.push_back(value); });
        const auto at = expected.find(key);
        require(found == (at != expected.end()), "copy membership including misses");
        if constexpr (V)
            require(actual == (found ? at->second : std::vector<bytes<V>>{}),
                    "copy complete ordered payload");
        else
            require(actual.empty(), "set has no value callbacks");
    }
    owner.validate();
}
template <unsigned W, unsigned B, unsigned V, bool Multi> void history() {
    constexpr auto K = W - B;
    using Owner = table<W, B, V, K, search_kernel::generic, counted_storage>;
    counted_storage::context original_memory, new_memory;
    const permutation<W, K> mix{311};
    std::vector<word> keys;
    for (unsigned i = 0; i < 1536; ++i)
        keys.push_back(mix.recover(K <= 9 ? i / 512 : 0, K <= 9 ? i % 512 : i));
    std::map<word, std::vector<bytes<V>>> original;
    {
        Owner source(311, &original_memory);
        for (unsigned i = 0; i < 512; ++i) {
            auto& values = original[keys[i]];
            const unsigned count = Multi ? (i == 510 ? 600 : 1 + i % 5) : 1;
            for (unsigned j = 0; j < count; ++j)
                values.push_back(payload<V>(i, j));
            if constexpr (Multi)
                source.insert_bag(keys[i], values);
            else
                source.insert(keys[i], values.front());
        }
        // Saturated overflow count stays unknown after deletion. Dead arena
        // blocks and abandoned initialized heads remain part of the copy.
        for (unsigned i = 0; i < 510; i += 2) {
            require(source.erase(keys[i]), "source erase");
            original.erase(keys[i]);
        }
        if constexpr (Multi) {
            auto abandoned = source.prepare_bag(keys[800], 600);
            (void)abandoned;
        }
        inspect<V>(source, original, keys);
        unsigned allocations = 0;
        source.allocations([&](auto, const auto*, auto) { ++allocations; });
        for (unsigned failure = 0; failure < allocations; ++failure) {
            new_memory.remaining = int(failure);
            bool denied = false;
            try {
                auto copy = source.clone(&new_memory);
            } catch (const refused&) {
                denied = true;
            }
            require(denied && new_memory.alive == 0,
                    "partial copy failure releases every new buffer");
            inspect<V>(source, original, keys);
        }
        new_memory.remaining = -1;

        auto copy = source.clone(&new_memory);
        require(new_memory.alive == allocations, "copy owns separate buffer identities");
        std::vector<std::pair<const std::byte*, std::size_t>> old_bytes;
        source.allocations(
            [&](auto, const std::byte* p, std::size_t n) { old_bytes.emplace_back(p, n); });
        unsigned next = 0;
        copy.allocations([&](auto, const std::byte* p, std::size_t n) {
            const auto [old, size] = old_bytes.at(next++);
            require(p != old && n == size && std::memcmp(p, old, n) == 0,
                    "exact assigned representation in independent storage");
        });
        require(next == old_bytes.size(), "all buffers copied");
        inspect<V>(copy, original, keys);
        auto changed = original;
        for (unsigned i = 1; i < 512; i += 2) {
            copy.erase(keys[i]);
            changed.erase(keys[i]);
        }
        for (unsigned i = 512; i < 1536; ++i) {
            auto& values = changed[keys[i]];
            for (unsigned j = 0; j < (Multi ? 1 + i % 7 : 1); ++j)
                values.push_back(payload<V>(i, j));
            if constexpr (Multi)
                copy.insert_bag(keys[i], values);
            else
                copy.insert(keys[i], values.front());
        }
        inspect<V>(copy, changed, keys);
        inspect<V>(source, original, keys);
        // A copied copy keeps source provenance independent too.
        auto second = copy.clone(&new_memory);
        inspect<V>(second, changed, keys);
    }
    require(original_memory.alive == 0 && new_memory.alive == 0, "complete owner cleanup");
}

int main() {
    try {
        history<24, 5, 0, false>();
        history<24, 5, 8, false>();
        history<24, 5, 64, false>();

        history<14, 5, 0, false>();
        history<14, 5, 8, false>();
        history<64, 5, 8, false>();
        std::cout
            << "physical copy: sets/maps, bitmap/full-width, saturated overflow, tombstones, source provenance and partial refusal passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
