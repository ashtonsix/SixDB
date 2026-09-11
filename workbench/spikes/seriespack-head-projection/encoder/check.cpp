#include "heads.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

namespace {
namespace trial = head_encode_experiment;
namespace sp = ikea::seriespack;
std::size_t checks = 0;
std::uint64_t state = 0x81b6c435e930fa72;
std::uint64_t random() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
void require(bool condition) { if (!condition) std::abort(); }
struct guarded {
    std::size_t size = sysconf(_SC_PAGESIZE);
    std::uint8_t* mapping = static_cast<std::uint8_t*>(mmap(nullptr, 3 * size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    std::uint8_t* bytes = mapping == MAP_FAILED ? nullptr : mapping + size;
    guarded() { require(mapping != MAP_FAILED); require(mprotect(bytes, size, PROT_READ | PROT_WRITE) == 0); }
    ~guarded() { munmap(mapping, 3 * size); }
};

template<class Ops, unsigned W, unsigned H, unsigned T, class U, unsigned Mode>
[[gnu::noinline]] void execute(const std::uint8_t* input, std::size_t count,
    std::uint8_t* h0, std::uint8_t* h1, std::size_t s0, std::size_t s1) {
    trial::encode<Ops,W,H,T,U,Mode>(reinterpret_cast<const U*>(input),count,h0,h1,s0,s1);
}

template<class Ops, unsigned W, unsigned H, unsigned T, class U>
void shape() {
    guarded input, head0, head1;
    constexpr std::uint8_t sentinel = 0xa7;
    const auto run = [&](unsigned count, std::size_t s0, std::size_t s1,
        unsigned input_offset, unsigned h0_offset, unsigned h1_offset) {
        auto* source = input.bytes + input_offset;
        auto* high = head0.bytes + h0_offset;
        auto* low = head1.bytes + h1_offset;
        std::array<std::uint8_t,1024> expected0, expected1;
        expected0.fill(sentinel); expected1.fill(sentinel);
        const auto tiles = (count + T - 1) / T;
        const auto extent0 = tiles ? (tiles - 1) * s0 + T : 0;
        const auto extent1 = tiles ? (tiles - 1) * s1 + T : 0;
        for (unsigned i = 0; i < count; ++i) {
            // Include all source bits, independently of K: these private head
            // projections must ignore bits above the selected high byte.
            const U value = i % 11 == 0 ? 0 : i % 13 == 0 ? ~U{0} : U(random());
            std::memcpy(source + i * sizeof(U), &value, sizeof(U));
            const auto wide = static_cast<std::uint64_t>(value);
            expected0[i / T * s0 + i % T] = (W + H - 8 < 64) ? std::uint8_t(wide >> (W + H - 8)) : 0;
            if constexpr (H == 16) expected1[i / T * s1 + i % T] = std::uint8_t(wide >> W);
        }
        if (count % T) {
            const auto last = tiles - 1;
            std::memset(expected0.data() + last * s0 + count % T, 0, T - count % T);
            std::memset(expected1.data() + last * s1 + count % T, 0, T - count % T);
        }
        sp::detail::static_for<4>([&](auto mode) {
            std::memset(head0.bytes,sentinel,head0.size);
            std::memset(head1.bytes,sentinel,head1.size);
            execute<Ops,W,H,T,U,mode>(count ? source : input.mapping,count,
                count ? high : head0.mapping,H == 16 && count ? low : head1.mapping,s0,s1);
            require(std::memcmp(high,expected0.data(),extent0) == 0);
            if constexpr (H == 16) require(std::memcmp(low,expected1.data(),extent1) == 0);
            for (unsigned i = 0; i < head0.size; ++i) {
                if (i < h0_offset || i >= h0_offset + extent0) require(head0.bytes[i] == sentinel);
                if constexpr (H == 16) {
                    if (i < h1_offset || i >= h1_offset + extent1) require(head1.bytes[i] == sentinel);
                } else require(head1.bytes[i] == sentinel);
            }
            ++checks;
        });
    };
    for (const auto strides : std::array<std::array<std::size_t,2>,4>{{{T,T},{T+11,T},{T,T+19},{T+11,T+19}}}) {
        for (unsigned count : {0u,1u,7u,8u,9u,15u,16u,17u,31u,32u,33u,63u,64u,65u,255u,256u,257u}) {
            const auto tiles = (count + T - 1) / T;
            const auto e0 = tiles ? (tiles - 1) * strides[0] + T : 0;
            const auto e1 = tiles ? (tiles - 1) * strides[1] + T : 0;
            run(count,strides[0],strides[1],0,0,0);
            run(count,strides[0],strides[1],input.size-count*sizeof(U),head0.size-e0,head1.size-e1);
        }
        for (unsigned offset = 0; offset < 128; ++offset)
            run(33,strides[0],strides[1],128+offset,128+(127-offset),128+(offset*17)%128);
    }
}

template<class Ops, unsigned W, unsigned H, class U>
void both_tiles() { shape<Ops,W,H,8,U>(); shape<Ops,W,H,256,U>(); }
template<class Ops, unsigned W, unsigned H>
void inputs() {
    both_tiles<Ops,W,H,std::uint8_t>(); both_tiles<Ops,W,H,std::uint16_t>();
    both_tiles<Ops,W,H,std::uint32_t>(); both_tiles<Ops,W,H,std::uint64_t>();
}
template<class Ops>
void target() {
    inputs<Ops,1,16>(); inputs<Ops,7,16>(); inputs<Ops,8,16>(); inputs<Ops,40,16>();
    inputs<Ops,0,16>(); inputs<Ops,8,8>(); inputs<Ops,1,8>();
}
}
int main() {
    target<trial::avx2>();
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    target<trial::avx512>();
#endif
    std::printf("head encode: %zu independent byte-oracle/guard/stride/partial cases passed\n",checks);
}
