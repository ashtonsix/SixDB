#include <ikea/tuplepack.h>
#include <ikea/tuplepack/author/maintenance.h>
#include <array>
#include <cassert>
#include <cstdio>

namespace tp = ikea::tuplepack;
// A teaching owner: storage/input/effects/private maintenance live at one stable
// address. Real lease acquisition, scheduling and publication are owner code.
struct work {
    static tp::layout format() {
        const std::array<tp::code, 1> codes{{{0, 0, 3}}};
        return *tp::layout::make(1, codes);
    }
    tp::layout description = format();
    std::array<tp::byte, 4> bytes{};
    tp::view view = *tp::view::bind(description, bytes, 4, 1);
    std::array<tp::byte, 1> map{0};
    tp::writer<8> writer = *tp::writer<8>::make(description, map);
    tp::mutation_operation<8> operation = *tp::bind_writer(writer, view);
    std::array<std::uint64_t, 4> input{1, 2, 3, 4};
    std::array<ikea::owner_write, 4> records;
    ikea::source_write_journal effects{records};
    unsigned generation = 1, completed = 0;
    bool lease_ready = false, cancelled = false, published = false;
    std::uint64_t invalid_rows = 0; // retained private contribution, not a persistent summary
    work() = default;
    work(const work&) = delete;
    work(work&&) = delete; // named view/plan addresses are borrowed by operation
    bool covers(std::size_t first, std::size_t count) const noexcept {
        return first + count <= 4;
    }
    unsigned before(std::size_t) const noexcept {
        return 0;
    }
    void after(std::size_t row, unsigned) noexcept {
        invalid_rows |= 1ULL << row;
    }
    bool ready_reply(unsigned reply_generation) {
        if (reply_generation != generation || cancelled)
            return false;
        lease_ready = true;
        return true;
    }
    bool step() {
        if (!lease_ready || cancelled || completed == input.size())
            return false;
        const auto result = operation.replace(completed, std::span(input).subspan(completed, 1),
                                              effects, tp::selection::all(), *this);
        if (!result)
            return false;
        ++completed; // suspension frontier; no native registers need retaining
        return true;
    }
    void publish_completed_with_bypass() {
        // Real owner first makes every affected pruning level bypassable and
        // publishes the matching data edition. Queuing repair alone is unsafe.
        assert(invalid_rows == (1u << completed) - 1);
        published = true;
    }
};
int main() {
    work pending;
    assert(!pending.ready_reply(0));
    assert(pending.ready_reply(1));
    assert(pending.step());
    pending.lease_ready = false; // suspended: the work still owns all live state
    assert(!pending.step());
    assert(pending.ready_reply(1));
    assert(pending.step());
    pending.cancelled = true;
    assert(!pending.step() && pending.completed == 2 && pending.effects.used != 0);
    // Chosen owner policy: publish completed chunks with bypass, not rollback.
    pending.publish_completed_with_bypass();
    assert(pending.bytes[0] == 1 && pending.bytes[1] == 2 && pending.bytes[2] == 0);
    std::puts("TuplePack integration: suspension/cancellation retain dirty state and publication "
              "obligations");
}
