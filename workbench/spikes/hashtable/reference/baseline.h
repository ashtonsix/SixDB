#pragma once

#include <absl/container/flat_hash_map.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace hashtable_spike {

inline constexpr char abseil_revision[] = "2065f4ded0558c6f89fee67c8e5228feb4eb960e";

/// Requested allocator bytes, including Abseil's control/slot storage. Excludes
/// the map object, these counters, malloc bookkeeping, and allocator rounding.
struct allocation_counters {
    std::size_t current_bytes = 0;
    std::size_t peak_bytes = 0;
    std::size_t allocation_calls = 0;
    std::size_t deallocation_calls = 0;
};

/// All allocator rebinds share the owner's counters. No atomic accounting is
/// needed for this single-writer baseline; lookup performs no accounting work.
template <class T> class counting_allocator {
    template <class> friend class counting_allocator;
    allocation_counters* counters_ = nullptr;

  public:
    using value_type = T;
    using is_always_equal = std::false_type;

    counting_allocator() noexcept = default;
    explicit counting_allocator(allocation_counters& counters) noexcept : counters_(&counters) {}
    template <class U>
    counting_allocator(const counting_allocator<U>& other) noexcept : counters_(other.counters_) {}

    [[nodiscard]] T* allocate(std::size_t count) {
        auto* result = std::allocator<T>{}.allocate(count);
        if (counters_) {
            counters_->current_bytes += count * sizeof(T);
            counters_->peak_bytes = std::max(counters_->peak_bytes, counters_->current_bytes);
            ++counters_->allocation_calls;
        }
        return result;
    }

    void deallocate(T* address, std::size_t count) noexcept {
        if (counters_) {
            assert(counters_->current_bytes >= count * sizeof(T));
            counters_->current_bytes -= count * sizeof(T);
            ++counters_->deallocation_calls;
        }
        std::allocator<T>{}.deallocate(address, count);
    }

    template <class U> bool operator==(const counting_allocator<U>& other) const noexcept {
        return counters_ == other.counters_;
    }
};

template<class Key=std::uint64_t,class Value=std::uint64_t> class basic_abseil_map {
    using ordinary_map = absl::flat_hash_map<Key, Value>;
    using allocator = counting_allocator<std::pair<const Key, Value>>;
    using table = absl::flat_hash_map<Key, Value,
                                     typename ordinary_map::hasher, typename ordinary_map::key_equal, allocator>;

    // Declaration order keeps the counters alive through table destruction.
    allocation_counters counters_;
    table table_;

  public:
    static constexpr std::size_t ordinary_object_bytes = sizeof(ordinary_map);
    basic_abseil_map() : table_(0, typename ordinary_map::hasher{}, typename ordinary_map::key_equal{}, allocator(counters_)) {}
    basic_abseil_map(const basic_abseil_map&) = delete;
    basic_abseil_map& operator=(const basic_abseil_map&) = delete;
    basic_abseil_map(basic_abseil_map&&) = delete;
    basic_abseil_map& operator=(basic_abseil_map&&) = delete;

    /// Returns true for insertion, false for replacement.
    bool insert_or_assign(std::uint64_t key, std::uint64_t value) {
        return table_.insert_or_assign(static_cast<Key>(key), static_cast<Value>(value)).second;
    }

    /// A miss leaves output unchanged. Every 64-bit key/value pattern is legal.
    bool get(std::uint64_t key, std::uint64_t& output) const {
        const auto found = table_.find(static_cast<Key>(key));
        if (found == table_.end())
            return false;
        output = found->second;
        return true;
    }

    void prefetch(std::uint64_t key) const { table_.prefetch(static_cast<Key>(key)); }
    bool erase(std::uint64_t key) {
        return table_.erase(static_cast<Key>(key)) != 0;
    }
    void clear() {
        table_.clear();
    }
    void reserve(std::size_t entries) {
        table_.reserve(entries);
    }
    std::size_t size() const noexcept {
        return table_.size();
    }
    std::size_t capacity() const noexcept {
        return table_.capacity();
    }
    template<class F> void each(F&& f) const {
        for(const auto& [key,value]:table_) f(key,value);
    }
    const allocation_counters& allocation_stats() const noexcept {
        return counters_;
    }
};

using abseil_map=basic_abseil_map<>;
} // namespace hashtable_spike
