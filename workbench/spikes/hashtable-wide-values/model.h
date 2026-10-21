#pragma once
#include "borrow.h"
#include <ikea/hashtable/encoding-selection.h>
#include <ikea/hashtable/rebuild.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <map>
#include <vector>
#if WIDE_PEERS
#include <absl/container/flat_hash_map.h>
#include <boost/unordered/unordered_flat_map.hpp>
#include <folly/container/F14Map.h>
static_assert(FOLLY_F14_VECTOR_INTRINSICS_AVAILABLE, "control must use actual SIMD F14");
#endif

namespace wide {
constexpr unsigned W = WIDE_W, b = WIDE_B, V = WIDE_V;
using Key = std::conditional_t<W <= 32, std::uint32_t, std::uint64_t>;
using Value = ht::bytes<V>;
inline std::size_t allocated{}, peak{}, allocations{};
inline std::map<const void*, std::size_t> backing;
inline void added(std::size_t n, const void* p) {
    allocated += n;
    peak = std::max(peak, allocated);
    ++allocations;
    if (n) backing[p] = n;
}
inline void removed(std::size_t n, const void* p) { allocated -= n; if (n) backing.erase(p); }
template <class T> struct Allocator {
    using value_type = T;
    Allocator() = default;
    template <class U> Allocator(const Allocator<U>&) {}
    T* allocate(std::size_t n) {
        auto* p = std::allocator<T>{}.allocate(n);
        added(n * sizeof(T), p);
        return p;
    }
    void deallocate(T* p, std::size_t n) {
        removed(n * sizeof(T), p);
        std::allocator<T>{}.deallocate(p, n);
    }
    template <class U> bool operator==(const Allocator<U>&) const { return true; }
};
// Swap-style base moves transfer capacities. Account the same transfer explicitly,
// including assignment, overflow reallocation and exception unwinding.
struct Storage : ht::heap_storage_policy {
    struct buffer : ht::heap_storage_policy::buffer {
        using base = ht::heap_storage_policy::buffer;
        buffer() = default;
        explicit buffer(std::size_t n, context* p = nullptr, bool zero = true,
                        std::size_t reserve = 0) : base(n, p, zero, reserve) { added(capacity(), data); }
        buffer(buffer&&) noexcept = default;
        buffer& operator=(buffer&&) noexcept = default;
        ~buffer() { removed(capacity(), data); }
        void grow(std::size_t n, std::size_t reserve = 0, bool zero = true) {
            const auto old = capacity();
            const auto* old_data = data;
            base::grow(n, reserve, zero);
            if (data != old_data) {
                // Reconstruct the overlap: base allocates next before freeing
                // old. Its internal temporary bypasses derived accounting.
                peak = std::max(peak, allocated + capacity());
                removed(old, old_data);
                added(capacity(), data);
            }
        }
    };
};
static_assert(sizeof(Storage::buffer) == sizeof(ht::heap_storage_policy::buffer));

#if defined(__x86_64__) && defined(__AVX512BW__)
constexpr auto cpu = "zen5", isa = "avx512";
#elif defined(__x86_64__)
constexpr auto cpu = "zen5", isa = "avx2";
#else
constexpr auto cpu = "neoverse-v2", isa = "neon";
#endif
template <unsigned B, unsigned Bytes> constexpr auto profile = [] {
    constexpr auto k = W - B;
    if constexpr (k <= 9)
        return ht::encoding_choice{k, ht::search_kernel::generic};
    else
        return *ht::measured_encoding(k, Bytes <= 8 ? 8 : 64, cpu, isa);
}();
template <unsigned B, unsigned Bytes>
using Table = ht::table<W, B, Bytes, profile<B, Bytes>.width,
                        profile<B, Bytes>.kernel, Storage>;

// Dense allocation with reusable stable slot IDs; ID zero is an initialized
// miss row. No per-entry allocation. Payload capacity is measured, including
// holes and geometric reserve. Table growth can leave this store untouched.
class Slotted {
    byte* data_{};
    std::size_t size_{1}, capacity_{};
    std::vector<word, Allocator<word>> free_;
public:
    explicit Slotted(std::size_t reserve) { grow(reserve + 1); std::memset(data_, 0, V); }
    Slotted(const Slotted&) = delete;
    ~Slotted() { if (data_) { removed(capacity_ * V, data_); std::free(data_); } }
    void grow(std::size_t n) {
        if (n <= capacity_) return;
        const auto next = (std::max(n, capacity_ * 2) * V + 63) & ~std::size_t{63};
        auto* p = static_cast<byte*>(std::aligned_alloc(64, next));
        if (!p) throw std::bad_alloc();
        added(next, p);
        if (data_) { std::memcpy(p, data_, size_ * V); removed(capacity_ * V, data_); std::free(data_); }
        data_ = p;
        capacity_ = next / V;
    }
    word insert(const Value& value) {
        word id;
        if (!free_.empty()) { id = free_.back(); free_.pop_back(); }
        else { grow(size_ + 1); id = size_++; }
        assign(id, value);
        return id;
    }
    void erase(word id) { ht::require(id && id < size_, "valid slot erase"); free_.push_back(id); }
    void assign(word id, const Value& value) { std::memcpy(data_ + id * V, value.data(), V); }
    const byte* at(word id) const { return data_ + id * V; }
    std::size_t capacity() const { return capacity_; }
};

enum class Kind { materialized, borrowed, slots, slots32, abseil, boost, f14, f14value,
                  abseil_slots, boost_slots, f14_slots };
template <Kind K> constexpr bool indirect = K == Kind::slots || K == Kind::slots32 || K == Kind::abseil_slots || K == Kind::boost_slots || K == Kind::f14_slots;
template <Kind K> constexpr unsigned cell_bytes = K == Kind::slots32 ? 4 : 8;
template <Kind K> constexpr bool packed = K == Kind::materialized || K == Kind::borrowed || K == Kind::slots || K == Kind::slots32;

#if WIDE_PEERS
template <Kind K> using Mapped = std::conditional_t<indirect<K>, ht::bytes<8>, Value>;
template <Kind K> using Pair = std::pair<const Key, Mapped<K>>;
template <Kind K> using Peer = std::conditional_t<K == Kind::abseil || K == Kind::abseil_slots,
    absl::flat_hash_map<Key, Mapped<K>, absl::Hash<Key>, std::equal_to<Key>, Allocator<Pair<K>>>,
    std::conditional_t<K == Kind::boost || K == Kind::boost_slots,
    boost::unordered_flat_map<Key, Mapped<K>, boost::hash<Key>, std::equal_to<Key>, Allocator<Pair<K>>>,
    std::conditional_t<K == Kind::f14value,
    folly::F14ValueMap<Key, Mapped<K>, folly::f14::DefaultHasher<Key>, folly::f14::DefaultKeyEqual<Key>, Allocator<Pair<K>>>,
    folly::F14FastMap<Key, Mapped<K>, folly::f14::DefaultHasher<Key>, folly::f14::DefaultKeyEqual<Key>, Allocator<Pair<K>>>>>>;
#endif
template <Kind K, unsigned B> struct Model {
    static constexpr auto kind = K;
    using Native = Table<B, indirect<K> ? cell_bytes<K> : V>;
#if WIDE_PEERS
    using Owner = Peer<K>;
#else
    using Owner = Native;
#endif
    std::unique_ptr<Owner> table;
    std::conditional_t<indirect<K>, std::unique_ptr<Slotted>, std::nullptr_t> payloads;
    explicit Model([[maybe_unused]] word seed, std::size_t population) {
#if WIDE_PEERS
        table = std::make_unique<Owner>();
        table->reserve(population);
#else
        table = std::make_unique<Owner>(seed);
#endif
        if constexpr (indirect<K>) payloads = std::make_unique<Slotted>(population);
    }
    bool put(Key key, const Value& value, bool existing = false) {
        if constexpr (indirect<K>) {
            word id{};
            bool present;
#if WIDE_PEERS
            auto it = table->find(key);
            present = it != table->end();
            if (present) id = ht::load<cell_bytes<K>>(it->second.data());
#else
            ht::bytes<cell_bytes<K>> previous;
            const auto position = Access::prepare_write(*table, key, previous);
            present = position.present;
            if (present) id = ht::load<cell_bytes<K>>(previous.data());
#endif
            if (present) { payloads->assign(id, value); return false; }
            ht::require(!existing, "existing slot write");
            id = payloads->insert(value);
            ht::require(id <= ht::mask<cell_bytes<K> * 8>, "slot ID width");
            ht::bytes<cell_bytes<K>> cell; ht::store<cell_bytes<K>>(cell.data(), id);
#if WIDE_PEERS
            table->emplace(key, cell);
#else
            Access::insert_prepared(*table, key, position, cell);
#endif
            return true;
        } else {
#if WIDE_PEERS
            if (existing) { table->find(key)->second = value; return false; }
            return table->insert_or_assign(key, value).second;
#else
            if (existing) return table->template insert<ht::write_strategy::existing>(key, value);
            return table->insert(key, value);
#endif
        }
    }
    bool erase(Key key) {
        if constexpr (indirect<K>) {
#if WIDE_PEERS
            auto it = table->find(key);
            if (it == table->end()) return false;
            payloads->erase(ht::load<cell_bytes<K>>(it->second.data()));
            table->erase(it);
#else
            const auto old = table->get(key);
            if (!old.found) return false;
            payloads->erase(ht::load<cell_bytes<K>>(old.value.data()));
            table->erase(key);
#endif
            return true;
        } else return table->erase(key);
    }
    bool increment(Key key) {
        const auto update = [](byte* p) { ht::store<8>(p, ht::load<8>(p) + 1); };
#if WIDE_PEERS
        auto it = table->find(key);
        if (it == table->end()) return false;
        if constexpr (indirect<K>) update(const_cast<byte*>(payloads->at(ht::load<cell_bytes<K>>(it->second.data()))));
        else update(it->second.data());
        return true;
#else
        if constexpr (K == Kind::materialized)
            return table->modify(key, [&](auto& value) { update(value.data()); });
        else if constexpr (K == Kind::borrowed) {
            const auto value = borrow<ht::lookup_hint::predictable>(*table, key);
            if (value.found) update(const_cast<byte*>(value.address));
            return value.found;
        } else {
            const auto value = table->get(key);
            if (value.found) update(const_cast<byte*>(payloads->at(ht::load<cell_bytes<K>>(value.value.data()))));
            return value.found;
        }
#endif
    }
    std::size_t size() const { return table->size(); }
    void prefetch(Key key) const {
#if WIDE_PEERS
        if constexpr (K == Kind::abseil || K == Kind::abseil_slots) table->prefetch(key);
        else if constexpr (K == Kind::f14 || K == Kind::f14value || K == Kind::f14_slots) table->prefetch(table->prehash(key));
#else
        table->prefetch(table->prepare(key));
#endif
    }
    // Expose the ID cell without loading it: a cold primary ID is a separate
    // dependency from matching its key. The owner stays immutable throughout
    // all stages, so both this cell and its eventual payload remain borrowed.
    template <ht::lookup_hint Hint>
    [[gnu::always_inline]] auto locate_id(Key key) const {
        static_assert(indirect<K>);
#if WIDE_PEERS
        const auto it = table->find(key);
        const bool found = it != table->end();
        return borrowed_value<cell_bytes<K>>{found ? it->second.data() : zero<cell_bytes<K>>.data(), found};
#else
        return borrow<Hint>(*table, key);
#endif
    }
    [[gnu::always_inline]] const byte* resolve_id(const byte* cell) const {
        static_assert(indirect<K>);
        return payloads->at(ht::load<cell_bytes<K>>(cell));
    }
    template <ht::lookup_hint Hint, class Consume>
    [[gnu::always_inline]] bool read(Key key, Consume&& consume) const {
#if WIDE_PEERS
        auto it = table->find(key);
        if (it == table->end()) return false;
        if constexpr (indirect<K>) consume(payloads->at(ht::load<cell_bytes<K>>(it->second.data())));
        else consume(it->second.data());
        return true;
#else
        if constexpr (K == Kind::materialized || (K == Kind::borrowed && V <= 8)) {
            const auto result = table->template get<Hint>(key);
            if constexpr (Hint == ht::lookup_hint::predictable) {
                if (result.found) consume(result.value.data());
            } else consume(result.value.data());
            return result.found;
        } else if constexpr (K == Kind::borrowed) {
            const auto result = borrow<Hint>(*table, key);
            if constexpr (Hint == ht::lookup_hint::predictable) {
                if (result.found) consume(result.address);
            } else consume(result.address);
            return result.found;
        } else if constexpr (K == Kind::slots32) {
            const auto result = borrow<Hint>(*table, key);
            if constexpr (Hint == ht::lookup_hint::predictable) {
                if (result.found) consume(payloads->at(ht::load<4>(result.address)));
            } else consume(payloads->at(ht::load<4>(result.address)));
            return result.found;
        } else {
            const auto result = table->template get<Hint>(key);
            if constexpr (Hint == ht::lookup_hint::predictable) {
                if (result.found) consume(payloads->at(ht::load<cell_bytes<K>>(result.value.data())));
            } else consume(payloads->at(ht::load<cell_bytes<K>>(result.value.data())));
            return result.found;
        }
#endif
    }
};
} // namespace wide
