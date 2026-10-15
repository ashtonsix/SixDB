#pragma once

#include "baseline.h"

#include <absl/container/flat_hash_set.h>
#include <absl/hash/hash.h>

#include <array>
#include <bit>
#include <concepts>
#include <cstring>
#include <functional>
#include <limits>

namespace hashtable_spike {

/// Exactly Bytes bytes, with byte alignment. All lookup and insertion keys must
/// fit the selected domain, as they do for the candidate's width-bound API.
/// memcpy is deliberately bounded: hash/equality may read the last slot.
template <unsigned Bytes> struct exact_unsigned_key {
    static_assert(Bytes >= 1 && Bytes <= 8);
    static_assert(std::endian::native == std::endian::little);
    std::array<unsigned char, Bytes> bytes{};

    static constexpr std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max() >> (64 - Bytes * 8);

    exact_unsigned_key() = default;
    exact_unsigned_key(std::uint64_t value) noexcept {
        assert(value <= maximum);
        std::memcpy(bytes.data(), &value, Bytes);
    }

    std::uint64_t value() const noexcept {
        std::uint64_t result = 0;
        std::memcpy(&result, bytes.data(), Bytes);
        return result;
    }

    friend bool operator==(const exact_unsigned_key& a,
                           const exact_unsigned_key& b) noexcept {
        return a.value() == b.value();
    }

    template <class H> friend H AbslHashValue(H state, const exact_unsigned_key& key) {
        return H::combine(std::move(state), key.value());
    }
};

template <unsigned Bytes> struct exact_unsigned_hash {
    using is_transparent = void;

    std::size_t operator()(const exact_unsigned_key<Bytes>& key) const {
        return (*this)(key.value());
    }
    template <std::unsigned_integral Word> std::size_t operator()(Word key) const {
        return absl::Hash<std::uint64_t>{}(static_cast<std::uint64_t>(key));
    }
};

template <unsigned Bytes> struct exact_unsigned_equal {
    using is_transparent = void;

    bool operator()(const exact_unsigned_key<Bytes>& a,
                    const exact_unsigned_key<Bytes>& b) const noexcept {
        return a == b;
    }
    template <std::unsigned_integral Word>
    bool operator()(const exact_unsigned_key<Bytes>& a, Word b) const noexcept {
        return a.value() == static_cast<std::uint64_t>(b);
    }
    template <std::unsigned_integral Word>
    bool operator()(Word a, const exact_unsigned_key<Bytes>& b) const noexcept {
        return static_cast<std::uint64_t>(a) == b.value();
    }
};

/// The default preserves ordinary Abseil's fused per-table seed/hash path.
/// Transparent=true avoids query-key construction, but this pinned Abseil
/// treats its custom hasher as nondefault and adds the table seed separately.
/// Keep both controls: transparent lookup alone is not proof of a faster or
/// otherwise identical baseline. Neither mode weakens the hash function.
template <unsigned KeyBits, class Value = std::uint64_t, bool Transparent = false>
class packed_abseil_map {
    static_assert(KeyBits >= 8 && KeyBits <= 64 && KeyBits % 8 == 0);
    static_assert(std::is_same_v<Value, std::uint16_t> ||
                  std::is_same_v<Value, std::uint64_t>);

  public:
    using key_type = exact_unsigned_key<KeyBits / 8>;
    using mapped_type = Value;
    using hasher = std::conditional_t<Transparent, exact_unsigned_hash<KeyBits / 8>,
                                      absl::Hash<key_type>>;
    using key_equal = std::conditional_t<Transparent, exact_unsigned_equal<KeyBits / 8>,
                                         std::equal_to<key_type>>;
    using ordinary_table_type = absl::flat_hash_map<key_type, Value, hasher, key_equal>;
    using value_type = typename ordinary_table_type::value_type;
    using allocator_type = counting_allocator<value_type>;
    using counted_table_type =
        absl::flat_hash_map<key_type, Value, hasher, key_equal, allocator_type>;

    static_assert(sizeof(key_type) == KeyBits / 8 && alignof(key_type) == 1);
    static constexpr std::size_t value_type_bytes = sizeof(value_type);
    static constexpr std::size_t ordinary_object_bytes = sizeof(ordinary_table_type);
    static constexpr unsigned key_bits = KeyBits;
    static constexpr unsigned value_bits = sizeof(Value) * 8;
    static constexpr bool transparent = Transparent;

  private:
    allocation_counters counters_;
    counted_table_type table_;

  public:
    packed_abseil_map() : table_(0, hasher{}, key_equal{}, allocator_type(counters_)) {}
    packed_abseil_map(const packed_abseil_map&) = delete;
    packed_abseil_map& operator=(const packed_abseil_map&) = delete;
    packed_abseil_map(packed_abseil_map&&) = delete;
    packed_abseil_map& operator=(packed_abseil_map&&) = delete;

    [[gnu::always_inline]] bool insert_or_assign(std::uint64_t key, std::uint64_t value) {
        return table_.insert_or_assign(key, static_cast<Value>(value)).second;
    }
    /// A miss preserves output. V16 stores all 16 value bits; V64 stores all 64.
    [[gnu::always_inline]] bool get(std::uint64_t key, std::uint64_t& output) const {
        const auto found = table_.find(key);
        if (found == table_.end()) return false;
        output = found->second;
        return true;
    }
    [[gnu::always_inline]] bool erase(std::uint64_t key) {
        return table_.erase(key) != 0;
    }
    void prefetch(std::uint64_t key) const { table_.prefetch(key); }
    void reserve(std::size_t entries) { table_.reserve(entries); }
    void clear() { table_.clear(); }
    std::size_t size() const noexcept { return table_.size(); }
    std::size_t capacity() const noexcept { return table_.capacity(); }
    template <class F> void each(F&& f) const {
        for (const auto& [key, value] : table_) f(key.value(), value);
    }
    const allocation_counters& allocation_stats() const noexcept { return counters_; }
};

template <unsigned KeyBits, bool Transparent = false> class packed_abseil_set {
    static_assert(KeyBits >= 8 && KeyBits <= 64 && KeyBits % 8 == 0);

  public:
    using key_type = exact_unsigned_key<KeyBits / 8>;
    using hasher = std::conditional_t<Transparent, exact_unsigned_hash<KeyBits / 8>,
                                      absl::Hash<key_type>>;
    using key_equal = std::conditional_t<Transparent, exact_unsigned_equal<KeyBits / 8>,
                                         std::equal_to<key_type>>;
    using ordinary_table_type = absl::flat_hash_set<key_type, hasher, key_equal>;
    using value_type = typename ordinary_table_type::value_type;
    using allocator_type = counting_allocator<value_type>;
    using counted_table_type =
        absl::flat_hash_set<key_type, hasher, key_equal, allocator_type>;

    static_assert(sizeof(key_type) == KeyBits / 8 && alignof(key_type) == 1);
    static constexpr std::size_t value_type_bytes = sizeof(value_type);
    static constexpr std::size_t ordinary_object_bytes = sizeof(ordinary_table_type);
    static constexpr unsigned key_bits = KeyBits;
    static constexpr bool transparent = Transparent;

  private:
    allocation_counters counters_;
    counted_table_type table_;

  public:
    packed_abseil_set() : table_(0, hasher{}, key_equal{}, allocator_type(counters_)) {}
    packed_abseil_set(const packed_abseil_set&) = delete;
    packed_abseil_set& operator=(const packed_abseil_set&) = delete;
    packed_abseil_set(packed_abseil_set&&) = delete;
    packed_abseil_set& operator=(packed_abseil_set&&) = delete;

    [[gnu::always_inline]] bool contains(std::uint64_t key) const { return table_.contains(key); }
    [[gnu::always_inline]] bool insert(std::uint64_t key) { return table_.emplace(key).second; }
    [[gnu::always_inline]] bool erase(std::uint64_t key) { return table_.erase(key) != 0; }
    void prefetch(std::uint64_t key) const { table_.prefetch(key); }
    void reserve(std::size_t entries) { table_.reserve(entries); }
    void clear() { table_.clear(); }
    std::size_t size() const noexcept { return table_.size(); }
    std::size_t capacity() const noexcept { return table_.capacity(); }
    template <class F> void each(F&& f) const {
        for (const auto& key : table_) f(key.value());
    }
    const allocation_counters& allocation_stats() const noexcept { return counters_; }
};

} // namespace hashtable_spike
