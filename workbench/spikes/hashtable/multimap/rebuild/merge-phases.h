#pragma once
#include <array>
#include <chrono>

namespace write_arch::merge_diagnostic {
// Coarse diagnostic only; there is no clock or counter in an entry loop.
// The ordinary build does not include this header or create this object.
inline thread_local std::array<double,6> elapsed{};
struct phases {
    using clock=std::chrono::steady_clock;
    clock::time_point previous=clock::now();
    ~phases(){end(5);} // last local destroyed: plan/pool/index cleanup included
    void end(unsigned phase)noexcept {
        const auto now=clock::now();
        elapsed[phase]=std::chrono::duration<double,std::nano>(now-previous).count();
        previous=now;
    }
};
}
