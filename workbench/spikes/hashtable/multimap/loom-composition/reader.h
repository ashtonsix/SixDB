#pragma once
#include "fixture.h"
#include <utility>
#ifndef WRITE_ARCH_MM_INLINE_ADVANCE
#define WRITE_ARCH_MM_INLINE_ADVANCE 0
#endif

namespace study {
// A borrowed native owner, exactly one pointer. The ordinary operation keeps
// that concrete owner and every input/sink/frame stable until returned drain or
// discard. No mutation, move, destruction, reentry or suspension through a
// consume/emitter is permitted. Mutation invalidates every outstanding probe;
// reborrow only after all prior work has physically returned. No ownership or
// validation, clocks, erased dispatch or reader-policy selection is added here.
template<class Owner,ikea::hashtable::staged_values Values=ikea::hashtable::staged_values::dependent> struct borrowed_reader {
    static_assert(!Multi||Values==ikea::hashtable::staged_values::dependent);
    const Owner* table;
    using staged_probe=typename Owner::staged_probe;
    template<class Consume> [[gnu::always_inline]] bool find(word k,Consume&& consume)const {
        return table->find(k,std::forward<Consume>(consume));
    }
    [[gnu::always_inline]] void prefetch(word k)const {
        const auto first=table->prepare(k);table->prefetch(first);
    }
    template<class Emit> [[gnu::always_inline]] void prepare_staged(staged_probe& state,word k,Emit&& emit)const {
        if constexpr(Multi)table->prepare_staged(state,k,std::forward<Emit>(emit));
        else table->template prepare_staged<Values>(state,k,std::forward<Emit>(emit));
    }
    template<class Consume,class Emit> [[gnu::always_inline]] bool advance(staged_probe& state,bool& found,Consume&& consume,Emit&& emit)const {
#if WRITE_ARCH_MM_INLINE_ADVANCE && defined(__aarch64__)
        if constexpr(Multi&&std::is_same_v<Owner,Packed>) {
            // Private caller experiment: expose phase dispatch without forcing
            // nested helpers inline or changing the container's public policy.
            [[clang::always_inline]] return table->advance(state,found,std::forward<Consume>(consume),std::forward<Emit>(emit));
        }else
#endif
        if constexpr(Multi)return table->advance(state,found,std::forward<Consume>(consume),std::forward<Emit>(emit));
        else return table->template advance<Values>(state,found,std::forward<Consume>(consume),std::forward<Emit>(emit));
    }
};
using PackedRead=borrowed_reader<Packed>;
using WholeRead=borrowed_reader<Whole>;
using MaintainedRead=borrowed_reader<MaintainedHeap>;
static_assert(sizeof(PackedRead)==sizeof(const Packed*)&&sizeof(WholeRead)==sizeof(const Whole*)&&
    sizeof(MaintainedRead)==sizeof(const MaintainedHeap*));
} // namespace study
