#include "array.h"
#include "local.h"
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>

namespace {
namespace sp = ikea::seriespack;
std::size_t checks = 0;
std::uint64_t state = 0xb0d189a74c2365f8;
std::uint64_t random() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
void require(bool value) { if (!value) std::abort(); }
struct guarded {
    std::size_t size = sysconf(_SC_PAGESIZE);
    std::uint8_t* mapping = static_cast<std::uint8_t*>(mmap(nullptr,3*size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    std::uint8_t* bytes = mapping == MAP_FAILED ? nullptr : mapping + size;
    guarded() { require(mapping != MAP_FAILED); require(mprotect(bytes,size,PROT_READ|PROT_WRITE)==0); }
    ~guarded() { munmap(mapping,3*size); }
};
template<unsigned Mode,class U>
[[gnu::noinline]] void operation(const std::uint8_t* input,std::uint8_t* output,std::size_t n,std::size_t stride) {
    local4_experiment::encode<Mode>(reinterpret_cast<const U*>(input),output,n,stride);
}
template<class U>
void carrier() {
    guarded input,output;
    std::array<std::uint8_t,4096> expected;
    const auto run = [&](unsigned n,unsigned stride,unsigned input_offset,unsigned output_offset) {
        require(output.size == expected.size());
        auto* source = input.bytes + input_offset;
        auto* destination = output.bytes + output_offset;
        expected.fill(0xa7);
        const auto tiles = (n+7)/8;
        for (unsigned tile=0;tile<tiles;++tile) std::memset(expected.data()+output_offset+tile*stride,0,4);
        for (unsigned i=0;i<n;++i) {
            const U value = i%17==0 ? U{0} : i%19==0 ? U(~U{0}) : U(random());
            std::memcpy(source+i*sizeof(U),&value,sizeof(U));
            for (unsigned b=0;b<4;++b)
                expected[output_offset+i/8*stride+b] |= std::uint8_t((std::uint64_t(value)>>b&1)<<(i%8));
        }
        sp::detail::static_for<4>([&](auto mode) {
            std::memset(output.bytes,0xa7,output.size);
            operation<mode,U>(n?source:input.mapping,n?destination:output.mapping,n,stride);
            require(std::memcmp(output.bytes,expected.data(),output.size)==0);
            ++checks;
        });
    };
    for (unsigned stride:{4u,5u,13u,64u}) {
        for (unsigned n:{0u,1u,7u,8u,9u,15u,16u,17u,23u,24u,31u,32u,33u,39u,40u,63u,64u,65u,71u,72u,73u,255u,256u,257u,289u}) {
            const auto tiles=(n+7)/8;
            const auto extent=tiles?(tiles-1)*stride+4:0;
            run(n,stride,0,0);
            run(n,stride,input.size-n*sizeof(U),output.size-extent);
        }
        for (unsigned offset=0;offset<128;++offset) {
            run(35,stride,128+offset,128+(127-offset));
            run(73,stride,128+(offset*17)%128,256-offset);
        }
    }
}
// The unchanged predecessor only exposes complete dense cells; check those
// exact extents independently without fabricating partial/strided support.
void predecessor_bounds() {
    guarded input,output;
    std::array<std::uint8_t,4096> expected;
    const auto run = [&](unsigned input_offset,unsigned output_offset) {
        require(output.size==expected.size());
        expected.fill(0xa7);
        std::memset(expected.data()+output_offset,0,128);
        for (unsigned i=0;i<256;++i) {
            const auto value=std::uint8_t(random());
            input.bytes[input_offset+i]=value;
            for (unsigned b=0;b<4;++b)
                expected[output_offset+i/8*4+b] |= std::uint8_t((value>>b&1)<<(i%8));
        }
        std::memset(output.bytes,0xa7,output.size);
        ikea::integers::local_encode<4>(input.bytes+input_offset,output.bytes+output_offset);
        require(std::memcmp(output.bytes,expected.data(),output.size)==0);
        ++checks;
    };
    run(0,0); run(input.size-256,output.size-128);
    for (unsigned offset=0;offset<128;++offset) run(128+offset,256-offset);
}
void basis() {
    std::array<std::uint8_t,32> input{};
    std::array<std::uint8_t,16> output{},expected{};
    for (unsigned bit=0;bit<=256;++bit) {
        input.fill(0); expected.fill(0);
        if (bit<256) {
            input[bit/8]=std::uint8_t(1u<<(bit%8));
            if (bit%8<4) expected[(bit/8)/8*4+bit%8]=std::uint8_t(1u<<((bit/8)%8));
        }
        local4_experiment::write32<true>(output.data(),_mm256_loadu_si256(reinterpret_cast<const __m256i*>(input.data())));
        require(output==expected); ++checks;
    }
}
}
int main() {
    predecessor_bounds(); basis(); carrier<std::uint8_t>(); carrier<std::uint16_t>(); carrier<std::uint32_t>(); carrier<std::uint64_t>();
    std::printf("Local4 projection: %zu independent bit-oracle/basis/guard/stride/partial cases passed\n",checks);
}
