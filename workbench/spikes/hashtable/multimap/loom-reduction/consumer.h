#pragma once
#include <type_traits>
#include <utility>

#ifndef WRITE_ARCH_VALUEWISE_REDUCTION
#define WRITE_ARCH_VALUEWISE_REDUCTION 0
#endif

namespace write_arch::reduction_study {
// Private caller experiment: preserve vectorization within a value while
// inhibiting cross-value loop vectorization. No stored format or Ring change.
template<class Consume> struct valuewise_consumer {
    using valuewise_tag=void;
    Consume consume;
    template<class Value> [[gnu::always_inline]] void operator()(const Value& value)const {
        consume(value);
    }
};
template<bool Independent,class Owner,class Read,class Consume>
[[gnu::always_inline]] decltype(auto) select(Consume&& consume) {
    if constexpr(WRITE_ARCH_VALUEWISE_REDUCTION&&Independent) {
        if constexpr(requires(Read read){read.table;}) {
            if constexpr(std::is_same_v<decltype(std::declval<Read>().table),const Owner*>)
                return valuewise_consumer<std::remove_cvref_t<Consume>>{std::forward<Consume>(consume)};
            else return std::forward<Consume>(consume);
        }else return std::forward<Consume>(consume);
    }else return std::forward<Consume>(consume);
}
}
