#pragma once
#include "model.h"
#include <loom/ring.h>
#include <array>

namespace wide {
// Actual native Loom rotation, with the same immutable model and complete byte
// consumer as the direct loops. Setup/close are outside the read interval;
// admission, every native yield, drain and result reduction are charged.
template <unsigned Bytes, class M, class R, bool StagedIDs = false> class LoomReader {
    struct Frame {
        std::size_t next{}, end{}, limit{};
        Key key{};
        bool pending{}, id_pending{}, found{};
        const byte* address{};
        ht::bytes<Bytes> copy;
        R result;
    };
    using Ring = typename loom::Ring<Frame*>::template WithShared<const M*, const Key*>;
    loom::Worker worker_;
    decltype(orbital::allocation::open_arena(0)) arena_;
    Ring ring_;
    std::array<Frame, 16> frames_;
    std::size_t limit_;

    template <ht::lookup_hint Hint, unsigned Distance>
    static LOOM_NATIVE_CC void visit(typename Ring::Cursor ring, typename Ring::Slot* slot,
                                    const M* model, const Key* queries, Frame* frame) {
        if constexpr (StagedIDs) {
            if (frame->id_pending) {
                frame->address = model->resolve_id(frame->address);
                loom::prefetch<loom::Hint::l1_1>(frame->address);
                if constexpr (Bytes > 64) loom::prefetch<loom::Hint::l1_1>(frame->address + 64);
                if constexpr (Bytes > 1) loom::prefetch<loom::Hint::l1_1>(frame->address + Bytes - 1);
                frame->id_pending = false;
                frame->pending = true;
                [[clang::musttail]] return Ring::yield_unchanged(ring, slot, model, queries, frame);
            }
        }
        if (frame->pending) {
            word sum{};
            for (unsigned at = 0; at < Bytes; at += 8) sum += ht::load<8>(frame->address + at);
            frame->result.digest += sum;
            frame->result.hits += frame->found;
            frame->result.keys += frame->found ? frame->key : 0;
        }
        if (frame->next >= frame->end)
            [[clang::musttail]] return Ring::finish(ring, slot, model, queries, frame);
        const auto position = frame->next;
        frame->next += 16;
        if constexpr (Distance)
            if (position + Distance < frame->limit) model->prefetch(queries[position + Distance]);
        frame->key = queries[position];
        if constexpr (StagedIDs) {
            const auto cell = model->template locate_id<Hint>(frame->key);
            frame->address = cell.address;
            frame->found = cell.found;
            loom::prefetch<loom::Hint::l1_1>(cell.address);
            // Four/eight-byte ID cells can straddle a line in peer storage.
            loom::prefetch<loom::Hint::l1_1>(cell.address + cell_bytes<M::kind> - 1);
            frame->pending = false;
            frame->id_pending = true;
            [[clang::musttail]] return Ring::yield_unchanged(ring, slot, model, queries, frame);
        }
        frame->address = zero<V>.data();
        frame->found = model->template read<Hint>(frame->key, [&](const byte* value) {
            if constexpr (M::kind == Kind::materialized || (M::kind == Kind::borrowed && V <= 8)) {
                std::memcpy(frame->copy.data(), value, Bytes);
                frame->address = frame->copy.data();
            } else {
                frame->address = value;
                // Fixed-L1 mechanism control. Each target is inside the actual
                // consumed byte window, including an unaligned final line.
                loom::prefetch<loom::Hint::l1_1>(value);
                if constexpr (Bytes > 64) loom::prefetch<loom::Hint::l1_1>(value + 64);
                if constexpr (Bytes > 1) loom::prefetch<loom::Hint::l1_1>(value + Bytes - 1);
            }
        });
        frame->pending = true;
        [[clang::musttail]] return Ring::yield_unchanged(ring, slot, model, queries, frame);
    }
public:
    LoomReader(const M& model, const Key* queries, std::size_t limit)
        : arena_(orbital::allocation::open_arena(8 * 1024 * 1024)), limit_(limit) {
        ht::require(bool(arena_) && bool(loom::adopt(worker_)), "Loom owner setup");
        ht::require(bool(loom::configure(worker_, *arena_, 0)), "Loom pool setup");
        ht::require(bool(ring_.configure(worker_, {16, 0}, {&model, queries})), "Loom bank setup");
    }
    ~LoomReader() {
        ht::require(bool(ring_.close()), "Loom bank closed after physical drain");
        for (;;) {
            const auto progress = loom::shutdown(worker_);
            ht::require(progress.kind() != orbital::Progress::Kind::failed, "Loom shutdown");
            if (progress.kind() == orbital::Progress::Kind::done) break;
            loom::poll(worker_, {64, 0});
        }
        loom::relinquish(worker_);
    }
    template <ht::lookup_hint Hint, unsigned Distance>
    R run(std::size_t first, std::size_t count) {
        const auto lanes = std::min(count, frames_.size());
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            auto& frame = frames_[lane];
            frame.next = first + lane; frame.end = first + count; frame.limit = limit_;
            frame.pending = false; frame.id_pending = false; frame.result = {};
            ht::require(ring_.push(visit<Hint, Distance>, {&frame}), "bounded Loom admission");
        }
        while (ring_.pending()) ring_.spin({4096});
        R result;
        for (std::size_t lane = 0; lane < lanes; ++lane) {
            result.hits += frames_[lane].result.hits;
            result.keys += frames_[lane].result.keys;
            result.digest += frames_[lane].result.digest;
        }
        return result;
    }
};
} // namespace wide
