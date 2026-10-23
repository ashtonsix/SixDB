#pragma once
#include "phase_material.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <thread>

namespace frame_lifecycle::phase {
inline void require(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); std::abort(); }
}
template<class T> T take(orbital::Result<T> result,
                        std::source_location where = std::source_location::current()) {
    if (!result) {
        std::fprintf(stderr, "%s:%u: native operation refused (%u)\n", where.file_name(), where.line(),
                     unsigned(result.error())); std::abort();
    }
    if constexpr (!std::is_void_v<T>) return std::move(*result);
}
struct Memory final : std::pmr::memory_resource {
    std::size_t live{}, peak{};
    void* do_allocate(std::size_t n, std::size_t alignment) override {
        auto* out = std::pmr::new_delete_resource()->allocate(n, alignment);
        live += n; peak = std::max(peak, live); return out;
    }
    void do_deallocate(void* ptr, std::size_t n, std::size_t alignment) override {
        require(live >= n, "metadata double release"); live -= n;
        std::pmr::new_delete_resource()->deallocate(ptr, n, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& r) const noexcept override { return &r == this; }
};
struct Runtime {
    orbital::worker::Lane lane;
    orbital::Ref<orbital::allocation::Arena> arena;
    orbital::allocation::Pool* pool;
    Runtime() {
        take(orbital::worker::adopt(lane));
        arena = take(orbital::allocation::open_arena(64 * 1024 * 1024));
        pool = take(orbital::allocation::add_pool(*arena, lane));
    }
    auto allocate(std::size_t n) {
        return take(orbital::allocation::allocate(*pool, {.bytes=n, .alignment=64,
            .readable_padding=0, .initialization=orbital::allocation::Initialization::zero}));
    }
    ~Runtime() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        for (;;) {
            const auto progress = orbital::allocation::close(*pool);
            require(progress.kind() != orbital::Progress::Kind::failed, "native Pool closure failed");
            if (progress.kind() == orbital::Progress::Kind::done) break;
            require(std::chrono::steady_clock::now() < deadline, "native Pool closure timed out");
            orbital::worker::poll(lane, 32); std::this_thread::yield();
        }
        require(!arena->charged_bytes.load(), "native backing stranded after release");
        take(orbital::allocation::remove_pool(*pool)); arena.reset(); orbital::worker::relinquish(lane);
    }
};
template<class Group> auto& base(Group& g) {
    if constexpr (std::is_same_v<std::remove_cvref_t<Group>, OverlayBase>) return g.frozen;
    else return g;
}
} // namespace frame_lifecycle::phase
