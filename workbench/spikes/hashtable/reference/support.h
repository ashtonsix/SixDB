#pragma once
#include "map.h"
#include <chrono>
#include <thread>
namespace hashtable_spike {
using clock_type=std::chrono::steady_clock;
struct pool_owner {
    orbital::Ref<oa::Arena> arena;
    orbital::worker::Lane lane;
    oa::Pool* pool;
    explicit pool_owner(std::uint64_t budget=4ULL<<30) : arena([budget] { auto p=oa::open_arena(budget); require(bool(p)); return std::move(*p); }()) {
        require(bool(orbital::worker::adopt(lane)));
        auto p=oa::add_pool(*arena,lane); require(bool(p)); pool=*p;
    }
    ~pool_owner() {
        const auto deadline=clock_type::now()+std::chrono::seconds(10);
        while (oa::close(*pool).kind()!=orbital::Progress::Kind::done) {
            require(clock_type::now()<deadline); orbital::worker::poll(lane,1024); std::this_thread::yield();
        }
        require(bool(oa::remove_pool(*pool))); orbital::worker::relinquish(lane);
    }
};

} // namespace hashtable_spike
