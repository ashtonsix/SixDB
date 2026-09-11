#include "region.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
namespace {
namespace sp = ikea::seriespack;
namespace trial = headed_region_experiment;
std::size_t checks = 0;
std::uint64_t rng = 0x659dac87972e3601ULL;
std::uint64_t random() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
void require(bool value) { if (!value) std::abort(); }
struct guarded {
    std::size_t size = sysconf(_SC_PAGESIZE);
    std::uint8_t* mapping = static_cast<std::uint8_t*>(mmap(nullptr, 3 * size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    std::uint8_t* bytes = mapping == MAP_FAILED ? nullptr : mapping + size;
    guarded() { require(mapping != MAP_FAILED); require(mprotect(bytes, size, PROT_READ | PROT_WRITE) == 0); }
    ~guarded() { munmap(mapping, 3 * size); }
};
template<unsigned W, unsigned H, class UInt>
void wire(std::uint8_t* payload, std::uint8_t* head0, std::uint8_t* head1,
    UInt* values, std::size_t count, std::array<std::size_t,3> strides) {
    constexpr std::uint64_t mask = (std::uint64_t{1} << (W + H)) - 1;
    for (std::size_t i = 0; i < count / 8; ++i) std::memset(payload + i * strides[0], 0, W);
    for (std::size_t i = 0; i < count; ++i) {
        const auto value = i % 19 == 0 ? 0 : i % 23 == 0 ? mask : random() & mask;
        values[i] = static_cast<UInt>(value);
        for (unsigned b = 0; b < W; ++b)
            payload[i / 8 * strides[0] + b] |= std::uint8_t((value >> b & 1) << (i % 8));
        // Public wire order: head0 is the highest byte; head1, when present,
        // is the byte immediately above the residual. They are independent.
        head0[i / 8 * strides[1] + i % 8] = static_cast<std::uint8_t>(value >> (W + H - 8));
        if constexpr (H == 16) head1[i / 8 * strides[2] + i % 8] = static_cast<std::uint8_t>(value >> W);
    }
}
template<unsigned W, unsigned H, class UInt, unsigned Mode>
[[gnu::noinline]] void run(const std::uint8_t* payload, const std::uint8_t* head0,
    const std::uint8_t* head1, std::uint8_t* out, const UInt* oracle,
    std::size_t count, std::array<std::size_t,3> strides) {
    trial::decode<W,H,UInt,Mode>(payload,head0,head1,reinterpret_cast<UInt*>(out),count,strides[0],strides[1],strides[2]);
    for (std::size_t i = 0; i < count; ++i) {
        UInt got; std::memcpy(&got, out + i * sizeof(UInt), sizeof got);
        if (got != oracle[i]) {
            std::fprintf(stderr,"W%u H%u UInt%zu mode%u count%zu index%zu: %llu != %llu\n",W,H,sizeof(UInt),Mode,count,i,
                static_cast<unsigned long long>(got),static_cast<unsigned long long>(oracle[i]));
            std::abort();
        }
    }
    ++checks;
}
template<unsigned W, unsigned H, class UInt>
void check() {
    guarded payload, head0, head1, output;
    std::array<UInt,136> oracle;
#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
    constexpr unsigned modes = 5;
#else
    constexpr unsigned modes = 2;
#endif
    const std::array<std::array<std::size_t,3>,4> layouts{{{W,8,8},{W,13,8},{W,8,19},{W+3,13,19}}};
    for (const auto strides : layouts)
        for (unsigned count : {0u,8u,16u,24u,32u,40u,56u,64u,72u,120u,128u,136u})
            for (bool end : {false,true}) {
                const auto extent = [&](unsigned field) { return count ? (count / 8 - 1) * strides[field] + (field == 0 ? W : 8) : 0; };
                auto* p = payload.bytes + (end ? payload.size - extent(0) : 0);
                auto* h0 = head0.bytes + (end ? head0.size - extent(1) : 0);
                auto* h1 = head1.bytes + (end ? head1.size - extent(2) : 0);
                auto* o = output.bytes + (end ? output.size - count * sizeof(UInt) : 0);
                std::memset(payload.bytes,0x59,payload.size);std::memset(head0.bytes,0x62,head0.size);std::memset(head1.bytes,0x8c,head1.size);
                wire<W,H>(p,h0,h1,oracle.data(),count,strides);
                sp::detail::static_for<modes>([&](auto mode) {
                    std::memset(output.bytes,0xa9,output.size);
                    run<W,H,UInt,mode>(count?p:payload.mapping,count?h0:head0.mapping,H==16&&count?h1:head1.mapping,
                        count?o:output.mapping,oracle.data(),count,strides);
                    for (std::size_t i=0;i<output.size;++i)
                        if (output.bytes+i<o||output.bytes+i>=o+count*sizeof(UInt)) require(output.bytes[i]==0xa9);
                });
            }
    for (const auto strides : layouts)
        for (unsigned count : {32u,64u,72u})
            for (unsigned offset=0;offset<128;++offset) {
                auto* p=payload.bytes+128+offset;
                auto* h0=head0.bytes+128+(127-offset);
                auto* h1=head1.bytes+128+((offset*17)%128);
                auto* o=output.bytes+256-offset;
                wire<W,H>(p,h0,h1,oracle.data(),count,strides);
                sp::detail::static_for<modes>([&](auto mode) {
                    std::memset(o-64,0xa9,count*sizeof(UInt)+128);
                    run<W,H,UInt,mode>(p,h0,H==16?h1:head1.mapping,o,oracle.data(),count,strides);
                    for (unsigned i=0;i<64;++i) require(o[-int(i)-1]==0xa9&&o[count*sizeof(UInt)+i]==0xa9);
                });
            }
}

}
int main() {
    sp::detail::static_for<7>([](auto w) {
        check<w+1,8,std::uint16_t>();check<w+1,8,std::uint32_t>();check<w+1,8,std::uint64_t>();
        check<w+1,16,std::uint32_t>();check<w+1,16,std::uint64_t>();
    });
    std::printf("headed local regions: %zu independent oracle/guard/unaligned cases passed\n", checks);
}
