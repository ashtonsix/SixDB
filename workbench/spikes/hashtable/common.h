#pragma once
#include <orbital/allocation.h>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <utility>
#include <vector>
namespace ht {
using byte=std::uint8_t;
using word=std::uint64_t;
namespace oa=orbital::allocation;
using clock_type=std::chrono::steady_clock;
inline void require(bool ok, const char* why="invariant") {
    if (!ok) { std::fprintf(stderr,"hashtable-design: %s\n",why); std::abort(); }
}
constexpr word mask(unsigned n) { return n==64?~word(0):(word(1)<<n)-1; }
template<unsigned N,class F> [[gnu::always_inline]] inline void lanes(F&& f) {
    // As in SeriesPack's detail::each, force both the expansion and callback.
    // An inline outer wrapper alone materializes entire request stages.
    [&]<std::size_t... I> [[gnu::always_inline]] (std::index_sequence<I...>) {
        auto invoke=[&]<std::size_t J> [[gnu::always_inline]] () {
            [[clang::always_inline]] f(std::integral_constant<unsigned,J>{});
        };
        (invoke.template operator()<I>(),...);
    }(std::make_index_sequence<N>{});
}
inline word mix(word x) {
    x^=x>>30; x*=0xbf58476d1ce4e5b9ULL;
    x^=x>>27; x*=0x94d049bb133111ebULL; return x^(x>>31);
}
struct pool_owner {
    orbital::Ref<oa::Arena> arena;
    orbital::worker::Lane lane;
    oa::Pool* pool;
    explicit pool_owner(word budget=4ULL<<30):arena([&] {
        auto a=oa::open_arena(budget); require(bool(a),"arena"); return std::move(*a);
    }()) {
        require(bool(orbital::worker::adopt(lane)),"adopt");
        auto p=oa::add_pool(*arena,lane); require(bool(p),"pool"); pool=*p;
    }
    ~pool_owner() {
        auto deadline=clock_type::now()+std::chrono::seconds(10);
        while(oa::close(*pool).kind()!=orbital::Progress::Kind::done) {
            require(clock_type::now()<deadline,"pool drain");
            orbital::worker::poll(lane,1024); std::this_thread::yield();
        }
        require(bool(oa::remove_pool(*pool)),"remove pool");
        orbital::worker::relinquish(lane);
    }
};
struct memory { word requested=0, backing=0, metadata=0; };
} // namespace ht
