#include "microtask.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>
#include <time.h>

namespace loom_spike::microtask {

namespace {
std::size_t inventory_size(std::size_t slots, std::size_t overflow) {
    if (!slots || slots > std::numeric_limits<std::size_t>::max() - overflow)
        throw std::invalid_argument("invalid ring inventory geometry");
    return slots + overflow;
}
} // namespace

std::uint64_t monotonic_ns() {
    timespec stamp{};
    clock_gettime(CLOCK_MONOTONIC, &stamp);
    return std::uint64_t(stamp.tv_sec) * 1000000000 + stamp.tv_nsec;
}

void issue_prefetch(Dependency dependency, Prefetch policy, std::size_t page_bytes,
                    Counters& counters) {
    if (policy == Prefetch::none || !dependency.address || !dependency.bytes)
        return;
    const auto* bytes = static_cast<const char*>(dependency.address);
    __builtin_prefetch(bytes, 0, 3);
    ++counters.prefetches;
    if (policy != Prefetch::pages)
        return;
    // Experimental bounded bootstrap: the start plus up to two lines per page.
    // Count hints issued, not hits or hardware stream/MSHR occupancy.
    const auto first_page = page_bytes - reinterpret_cast<std::uintptr_t>(bytes) % page_bytes;
    for (std::size_t offset = first_page; offset < dependency.bytes; offset += page_bytes) {
        __builtin_prefetch(bytes + offset, 0, 2);
        ++counters.prefetches;
        if (dependency.bytes - offset > 64) {
            __builtin_prefetch(bytes + offset + 64, 0, 2);
            ++counters.prefetches;
        }
    }
}

template <std::size_t... I>
Ring<std::index_sequence<I...>>::Ring(std::size_t slots, std::size_t overflow_slots,
                                     Prefetch prefetch, std::size_t page_bytes)
    : inventory_(inventory_size(slots, overflow_slots)), ring_(slots, nullptr),
      overflow_(overflow_slots, nullptr), prefetch_(prefetch), page_bytes_(page_bytes) {
    if (!slots || !page_bytes)
        throw std::invalid_argument("ring and page sizes must be nonzero");
    free_.reserve(inventory_.size());
    vacant_.reserve(slots);
    for (std::size_t i = slots; i; --i)
        vacant_.push_back(i - 1);
    for (std::size_t i = inventory_.size(); i; --i) {
        inventory_[i - 1].inventory = i - 1;
        free_.push_back(i - 1);
    }
}

template <std::size_t... I>
Ring<std::index_sequence<I...>>::Ring(std::size_t slots, std::size_t overflow_slots,
                                     prefetch::Binding binding)
    : Ring(slots, overflow_slots, Prefetch::none, binding.page_bytes) {
    hint_binding_ = binding;
    admitted_hint_ = prefetch::entries()[unsigned(binding.kind)];
}

template <std::size_t... I>
void Ring<std::index_sequence<I...>>::prefetch_admitted(const Slot& slot) {
    if (admitted_hint_) {
        if (slot.admission_class == prefetch::Class::none)
            return;
        const auto region = prefetch::prepare({slot.admission_class, hint_binding_.page_bytes},
                                              slot.dependency.address);
        prefetch::entries()[unsigned(slot.admission_class)](
            region.base, region.first_following_page, region.page_bytes);
    } else
        issue_prefetch(slot.dependency, prefetch_, page_bytes_, counters_);
}

template <std::size_t... I>
bool Ring<std::index_sequence<I...>>::push(Function function, Arguments args,
                                          Dependency dependency) {
    return push(function, args, dependency, hint_binding_.kind);
}

template <std::size_t... I>
bool Ring<std::index_sequence<I...>>::push(Function function, Arguments args,
                                          Dependency dependency, prefetch::Class admission_class) {
    if (!function || free_.empty()) {
        ++counters_.refused;
        return false;
    }
    auto& slot = inventory_[free_.back()];
    free_.pop_back();
    slot.function = function;
    slot.arguments = args;
    slot.dependency = dependency;
    slot.admission_class = admission_class;
    slot.completed = false;
    ++counters_.accepted;
    if (live_ < ring_.size()) {
        slot.home = vacant_.back();
        vacant_.pop_back();
        ring_[slot.home] = &slot;
        ++live_;
        prefetch_admitted(slot);
        counters_.high_ring = std::max<std::uint64_t>(counters_.high_ring, live_);
    } else {
        assert(overflow_size_ < overflow_.size());
        overflow_[overflow_tail_] = &slot;
        if (++overflow_tail_ == overflow_.size())
            overflow_tail_ = 0;
        ++overflow_size_;
        counters_.high_overflow = std::max<std::uint64_t>(counters_.high_overflow, overflow_size_);
    }
    return true;
}

template <std::size_t... I> void Ring<std::index_sequence<I...>>::retire(Slot& slot) {
    const auto home = slot.home;
    assert(ring_[home] == &slot);
    ring_[home] = nullptr;
    --live_;
    ++counters_.completed;
    free_.push_back(slot.inventory);
    if (overflow_size_) {
        auto* promoted = overflow_[overflow_head_];
        if (++overflow_head_ == overflow_.size())
            overflow_head_ = 0;
        --overflow_size_;
        promoted->home = home;
        ring_[home] = promoted;
        ++live_;
        ++counters_.promoted;
        prefetch_admitted(*promoted);
    } else
        vacant_.push_back(home);
}

template <std::size_t... I>
std::uint64_t Ring<std::index_sequence<I...>>::spin(SpinBudget budget) {
    if (spinning_)
        throw std::logic_error("a microtask spin cannot reenter its active ring");
    budget_ = budget;
    budget_.clock_interval = std::max(1u, budget.clock_interval);
    clock_countdown_ = budget_.clock_interval;
    spin_visits_ = 0;
    if (!budget.visits || !live_)
        return 0;
    spinning_ = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{spinning_};
    const auto now = budget.elapsed_ns ? monotonic_ns() : 0;
    deadline_ = budget.elapsed_ns > std::numeric_limits<std::uint64_t>::max() - now ?
                    std::numeric_limits<std::uint64_t>::max() : now + budget.elapsed_ns;
    Arguments empty;
#if defined(__aarch64__) && !defined(LOOM_MICROTASK_ORDINARY_ABI)
    enter(yield, this, nullptr, 0, 0, 0, 0, empty.vectors[I]...);
#else
    yield(this, nullptr, 0, 0, 0, 0, empty.vectors[I]...);
#endif
    counters_.visits += spin_visits_;
    return spin_visits_;
}

template <std::size_t... I>
std::size_t Ring<std::index_sequence<I...>>::discard() {
    if (spinning_)
        throw std::logic_error("discard requires an owner spin-return boundary");
    const auto abandoned = pending();
    counters_.abandoned += abandoned;
    std::fill(ring_.begin(), ring_.end(), nullptr);
    std::fill(overflow_.begin(), overflow_.end(), nullptr);
    cursor_ = live_ = overflow_head_ = overflow_tail_ = overflow_size_ = 0;
    vacant_.clear();
    for (std::size_t i = ring_.size(); i; --i)
        vacant_.push_back(i - 1);
    free_.clear();
    for (std::size_t i = inventory_.size(); i; --i) {
        auto& slot = inventory_[i - 1];
        slot.function = nullptr;
        slot.arguments = {};
        slot.dependency = {};
        slot.completed = false;
        free_.push_back(i - 1);
    }
    return abandoned;
}

template class Ring<std::make_index_sequence<0>>;
template class Ring<std::make_index_sequence<2>>;

} // namespace loom_spike::microtask
