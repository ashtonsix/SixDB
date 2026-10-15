#pragma once

#include <absl/container/inlined_vector.h>
#include <boost/unordered/concurrent_flat_map.hpp>
#include <boost/unordered/concurrent_flat_set.hpp>
#include <gtl/phmap.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>

namespace ht::concurrent_baselines {
using word = std::uint64_t;
enum class backend { boost, gtl };
enum class kind { set, map, multimap };

template<unsigned K> inline constexpr word domain_mask = ~word{0} >> (64-K);
template<unsigned K> using ordinary_key = std::conditional_t<(K<=8),std::uint8_t,
    std::conditional_t<(K<=16),std::uint16_t,std::conditional_t<(K<=32),std::uint32_t,word>>>;

// Byte-aligned exact-width storage; bounded copies also cover the last slot.
// No alias to a larger integer or read beyond the selected key domain.
template<unsigned K> struct exact_key {
    static_assert(K>=8 && K<=64 && K%8==0);
    static_assert(std::endian::native==std::endian::little);
    std::array<unsigned char,K/8> bytes{};
    exact_key() = default;
    explicit exact_key(word value) noexcept {
        assert(value<=domain_mask<K>);
        std::memcpy(bytes.data(),&value,bytes.size());
    }
    word value() const noexcept {
        word value=0; std::memcpy(&value,bytes.data(),bytes.size()); return value;
    }
    friend bool operator==(const exact_key& a,const exact_key& b) noexcept {
        return a.value()==b.value();
    }
};

template<backend Backend,unsigned K> struct exact_hash {
    std::size_t operator()(const exact_key<K>& key) const noexcept {
        // Preserve each library's ordinary integer hash and its internal mix.
        // No false is_avalanching annotation and no candidate permutation.
        if constexpr(Backend==backend::boost) return boost::hash<word>{}(key.value());
        else return gtl::Hash<word>{}(key.value());
    }
};

struct sum {
    word operator()(word a,word b) const noexcept { return a+b; }
};

/// Shared mutable baselines own ordinary flat containers and ordinary bags.
/// No allocation counters, hidden locks in the adapter, or escaped references.
/// The caller supplies synchronization between phases and before destruction.
/// Concurrent add means union / unsigned modular sum / unordered bag append.
/// Arbitrary overwrite/delete interleavings have no worker-local merge oracle.
template<backend Backend,unsigned K,kind Kind,bool Exact=false> class table {
    static_assert(K>=8 && K<=64 && K%8==0);
public:
    static constexpr backend implementation=Backend;
    static constexpr kind container_kind=Kind;
    static constexpr unsigned key_bits=K;
    static constexpr bool exact=Exact;
    using key_type=std::conditional_t<Exact,exact_key<K>,ordinary_key<K>>;
    using bag=absl::InlinedVector<word,1>;
    using mapped_type=std::conditional_t<Kind==kind::multimap,bag,word>;
    using native_hash=std::conditional_t<Backend==backend::boost,boost::hash<key_type>,gtl::Hash<key_type>>;
    using hasher=std::conditional_t<Exact,exact_hash<Backend,K>,native_hash>;
    using boost_map=boost::concurrent_flat_map<key_type,mapped_type,hasher>;
    using boost_set=boost::concurrent_flat_set<key_type,hasher>;
    // N is log2(shards), not the number of workers. These aliases supply a real
    // mutex; the unsuffixed GTL defaults intentionally do not synchronize.
    using gtl_map=gtl::parallel_flat_hash_map_m<key_type,mapped_type,hasher>;
    using gtl_set=gtl::parallel_flat_hash_set_m<key_type,hasher>;
    using storage_type=std::conditional_t<Backend==backend::boost,
        std::conditional_t<Kind==kind::set,boost_set,boost_map>,
        std::conditional_t<Kind==kind::set,gtl_set,gtl_map>>;

    table() = default;
    table(const table&)=delete;
    table& operator=(const table&)=delete;

    void reserve(std::size_t keys) { storage_.reserve(keys); }
    // These inventory/lifetime operations require caller-established quiescence.
    void clear() { storage_.clear(); }
    std::size_t size_quiescent() const { return storage_.size(); }
    static constexpr std::size_t owner_bytes() { return sizeof(storage_type); }
    static std::size_t bag_inline_capacity() { return bag{}.capacity(); }

    /// Map combine executes under the library's entry/shard protection. New
    /// entries start at incoming; combine is called only for existing entries.
    template<class Combine> [[gnu::always_inline]] bool upsert(word key,word incoming,Combine&& combine)
        requires(Kind==kind::map) {
        static_assert(noexcept(combine(word{},word{})));
        auto update=[&](auto& entry) { entry.second=combine(entry.second,incoming); };
        if constexpr(Backend==backend::boost)
            return storage_.try_emplace_or_visit(make_key(key),incoming,update);
        else return storage_.try_emplace_l(make_key(key),update,incoming);
    }
    [[gnu::always_inline]] bool add(word key,word value=0) {
        if constexpr(Kind==kind::set) {
            if constexpr(Backend==backend::boost) return storage_.insert(make_key(key));
            else return storage_.insert(make_key(key)).second;
        } else if constexpr(Kind==kind::map) return upsert(key,value,sum{});
        else {
            auto append=[&](auto& entry) { entry.second.push_back(value); };
            // Construct one element only on insertion. No empty bag can become
            // externally visible between insertion and the first append.
            if constexpr(Backend==backend::boost)
                return storage_.try_emplace_or_visit(make_key(key),std::size_t{1},value,append);
            else return storage_.try_emplace_l(make_key(key),append,std::size_t{1},value);
        }
    }
    bool assign(word key,word value) requires(Kind==kind::map) {
        return upsert(key,value,[](word,word b) noexcept {return b;});
    }

    /// consume is synchronous while protected. It may not reenter this table
    /// or retain element/bag references. Set membership consumes one zero value.
    /// Quiescent=true is permitted only after ALL possible mutators have stopped.
    /// It removes GTL's locks; Boost still uses cvisit. A native conversion to
    /// Boost unordered_flat_map/set is a separate, charged ownership operation,
    /// not silently performed by this lookup wrapper.
    template<bool Quiescent=false,class Consume>
    [[gnu::always_inline]] bool consume(word key,Consume&& consume) const {
        auto visit=[&](const auto& entry) {
            if constexpr(Kind==kind::set) consume(word{0});
            else if constexpr(Kind==kind::map) consume(entry.second);
            else for(word value:entry.second) consume(value);
        };
        if constexpr(Backend==backend::boost) return storage_.cvisit(make_key(key),visit)!=0;
        else if constexpr(Quiescent) return storage_.if_contains_unsafe(make_key(key),visit);
        else return storage_.if_contains(make_key(key),visit);
    }

    bool erase_key(word key) { return storage_.erase(make_key(key))!=0; }
    /// Remove one equal value, unordered swap erase; removing the final value
    /// erases the key under the same protection. Return a value count, not the
    /// library's erased-key result (which is zero for a surviving bag).
    bool erase_one(word key,word value) requires(Kind==kind::multimap) {
        bool removed=false;
        storage_.erase_if(make_key(key),[&](auto& entry) {
            auto& values=entry.second;
            auto it=std::find(values.begin(),values.end(),value);
            if(it==values.end()) return false;
            *it=values.back(); values.pop_back(); removed=true;
            return values.empty();
        });
        return removed;
    }

    template<class Visit> void each_quiescent(Visit&& visit) const {
        auto apply=[&](const auto& entry) {
            if constexpr(Kind==kind::set) visit(to_word(entry),word{0});
            else if constexpr(Kind==kind::map) visit(to_word(entry.first),entry.second);
            else for(word value:entry.second) visit(to_word(entry.first),value);
        };
        if constexpr(Backend==backend::boost) storage_.cvisit_all(apply);
        else storage_.for_each(apply);
    }

    /// Both tables must be quiescent and distinct; receiver retains ownership.
    /// Native merge moves absent entries/bags, leaving duplicates in source.
    /// Combine/appending handles those duplicates once, then source is cleared.
    /// A throwing bag allocation can leave a partial merge: no atomic rollback
    /// or retry-safe cursor is claimed. Benchmark OOM aborts the run.
    template<class Combine=sum> void merge_from_quiescent(table& source,Combine combine={}) {
        assert(this!=&source);
        storage_.merge(source.storage_);
        if constexpr(Kind!=kind::set) {
            source.each_quiescent([&](word key,word value) {
                if constexpr(Kind==kind::map) upsert(key,value,combine);
                else add(key,value);
            });
        }
        source.clear();
    }

private:
    static key_type make_key(word key) {
        assert(key<=domain_mask<K>); return key_type(key);
    }
    static word to_word(const key_type& key) {
        if constexpr(Exact) return key.value(); else return word(key);
    }
    storage_type storage_;
};
} // namespace ht::concurrent_baselines
