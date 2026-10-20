#pragma once
#include <ikea/streampack.h>
#include <array>
#include <bit>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace sp=ikea::streampack;
using word=std::uint64_t;
inline word mix(word x) { x^=x>>30;x*=0xbf58476d1ce4e5b9ULL;x^=x>>27;x*=0x94d049bb133111ebULL;return x^(x>>31); }
struct release { void operator()(std::byte* p) const { std::free(p); } };
struct fixture {
    sp::layout format;
    unsigned nodes, stride, lengths_at,control_at;
    std::unique_ptr<std::byte,release> metadata;
    std::vector<std::byte> payload;
    std::vector<sp::view> views;
    fixture(unsigned n,sp::description d):format(*sp::layout::prepare(d)),nodes(n),payload(std::size_t(n)*8192) {
        lengths_at=(format.directory_bytes()+63)&~63u;
        control_at=lengths_at+format.lengths_bytes();stride=(control_at+format.control_bytes()+63)&~63u;
        metadata.reset(static_cast<std::byte*>(std::aligned_alloc(64,std::size_t(n)*stride)));
        assert(metadata);views.reserve(n);
        for(unsigned i=0;i<n;++i) views.push_back(*sp::view::bind(format,
            {std::span(payload).subspan(std::size_t(i)*8192,8192),
             {metadata.get()+std::size_t(i)*stride,format.directory_bytes()},
             {metadata.get()+std::size_t(i)*stride+lengths_at,format.lengths_bytes()},
             {metadata.get()+std::size_t(i)*stride+control_at,format.control_bytes()}}));
        reset();
    }
    void reset() {
        ikea::no_coverage none;
        for(unsigned i=0;i<nodes;++i) {
            assert(sp::initialize(views[i],none));
            for(unsigned r=0;r<256;++r) {
                std::array<std::byte,32> value{};
                const auto salt=mix(i*256+r+713);std::memcpy(value.data()+8,&salt,8);
                assert(sp::append(views[i],std::span(value).first(format.has_used()?16:32),32,none));
            }
        }
    }
};
