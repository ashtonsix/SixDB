#pragma once
#include <ikea/seriespack/author/expression.h>
#include <ikea/seriespack/read.h>
#include <ikea_predecessor/seriespack.h>
#include <ikea_predecessor/seriespack/composition_x86.h>
#include <ikea_predecessor/seriespack/detail/physical.h>
#include "probe_controls.h"
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace sp=ikea::seriespack;
namespace old=ikea_predecessor::seriespack;
struct free_bytes {void operator()(std::uint8_t* p)const{std::free(p);}};

template<unsigned K,sp::geometry G> struct comparison_fixture {
    using F=sp::format<K,G>;
    static constexpr auto OG=G==sp::geometry::local?old::geometry::local8:old::geometry::striped;
    std::size_t count,bytes;
    std::unique_ptr<std::uint8_t,free_bytes> storage;
    std::vector<std::uint64_t> values;
    std::vector<std::size_t> queries;
    explicit comparison_fixture(std::size_t n):count(n),bytes(n*K/8),storage(static_cast<std::uint8_t*>(std::aligned_alloc(64,(bytes+63)&~std::size_t{63}))),values(n),queries(8192) {
        if(!storage)std::abort();std::mt19937_64 random(42);
        for(auto& x:values){x=random();if constexpr(K<64)x&=(std::uint64_t{1}<<K)-1;}
        for(std::size_t i=0;i<n;i+=F::tile_rows)old::detail::encode_tile<K,OG>(values.data()+i,storage.get()+i*K/8);
        for(auto& q:queries)q=random()%n;
    }
    auto current()const {
        auto v=old::const_view::attach({K,0,OG},count,{{std::span<const std::byte>(reinterpret_cast<const std::byte*>(storage.get()),bytes),F::tile_bytes},{}});
        if(!v)std::abort();auto reader=old::bind_reader(*v);if(!reader)std::abort();return *reader;
    }
    template<class U> auto next()const {
        auto reader=sp::bind_decoder<U>(sp::dense_input{K,G,{storage.get(),bytes},count});
        if(!reader)std::abort();return *reader;
    }
};
