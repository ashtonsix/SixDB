#include "regions.h"
#include "local.h"
#include <ikea/seriespack.h>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace {
namespace sp=ikea::seriespack;
std::size_t checks=0;
std::uint64_t random_state=0xa4b917d2653ce80fULL;
std::uint8_t random() {random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;return random_state;}
void require(bool value) {if (!value) std::abort();}
struct guarded {
    std::size_t page=sysconf(_SC_PAGESIZE),size;
    std::uint8_t* mapping;std::uint8_t* bytes;
    explicit guarded(std::size_t n):size(std::max(page,(n+page-1)/page*page)),
        mapping(static_cast<std::uint8_t*>(mmap(nullptr,size+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0))),
        bytes(mapping==MAP_FAILED?nullptr:mapping+page) {
        require(mapping!=MAP_FAILED);require(mprotect(bytes,size,PROT_READ|PROT_WRITE)==0);
    }
    ~guarded() {munmap(mapping,size+2*page);}
};
template<unsigned Grain>
[[gnu::noinline]] void operation(const std::uint8_t* in,std::uint8_t* out,std::size_t n,std::size_t stride) {
    local6_experiment::decode<Grain>(in,out,n,stride);
}
void run(unsigned n,unsigned stride,int where,unsigned input_offset=0,unsigned output_offset=0) {
    const unsigned tiles=(n+7)/8,extent=tiles?(tiles-1)*stride+6:0;
    guarded input(extent+256),output(n+256);
    if (where==1) {input_offset=input.size-extent;output_offset=output.size-n;}
    auto* in=input.bytes+input_offset;auto* out=output.bytes+output_offset;
    std::memset(input.bytes,0xa7,input.size);
    std::vector<std::uint8_t> expected(output.size,0xa7);
    for (unsigned t=0;t<tiles;++t) for (unsigned b=0;b<6;++b) in[t*stride+b]=random();
    for (unsigned i=0;i<n;++i) {
        std::uint8_t value=0;
        for (unsigned b=0;b<6;++b) value|=std::uint8_t((in[i/8*stride+b]>>(i%8)&1)<<b);
        expected[output_offset+i]=value;
    }
    const auto check=[&](auto function) {
        std::memset(output.bytes,0xa7,output.size);
        function(n?in:input.mapping,n?out:output.mapping,n,stride);
        require(std::memcmp(output.bytes,expected.data(),output.size)==0);++checks;
    };
    check(operation<0>);check(operation<64>);check(operation<256>);check(operation<512>);
    const sp::description description{6,0,sp::geometry::local8};
    const auto attached=sp::const_view::attach(description,n,
        {{std::span(reinterpret_cast<const std::byte*>(n?in:input.mapping),extent),stride},{}});
    require(attached.has_value());
    const auto reader=sp::bind_reader(*attached,sp::execution_target::avx512);require(reader.has_value());
    std::memset(output.bytes,0xa7,output.size);
    reader->decode({0,n},sp::output_values{std::span(n?out:output.mapping,n)});
    require(std::memcmp(output.bytes,expected.data(),output.size)==0);++checks;
    if (n!=0&&n%256==0&&stride==6) {
        std::memset(output.bytes,0xa7,output.size);
        for (unsigned i=0;i<n;i+=256) ikea::integers::local_decode<6>(in+i/8*6,out+i);
        require(std::memcmp(output.bytes,expected.data(),output.size)==0);++checks;
    }
}
void basis() {
    std::array<std::uint8_t,48> wire{};
    std::array<std::uint8_t,64> expected{},output{};
    for (unsigned bit=0;bit<=384;++bit) {
        wire.fill(0);expected.fill(0);
        if (bit<384) {wire[bit/8]=1u<<(bit%8);expected[bit/48*8+bit%8]=1u<<((bit/8)%6);}
        _mm512_storeu_si512(output.data(),sp::avx512::read_local_region64<6>(wire.data()));
        require(output==expected);++checks;
    }
}
}
int main() {
    basis();
    for (unsigned stride:{6u,7u,13u,64u}) {
        for (unsigned n:{0u,1u,7u,8u,9u,15u,16u,17u,31u,32u,33u,63u,64u,65u,127u,128u,129u,255u,256u,257u,511u,512u,513u,8192u}) {
            run(n,stride,0);run(n,stride,1);
        }
        for (unsigned offset=0;offset<128;++offset) {
            run(73,stride,0,128+offset,256-offset);
            run(513,stride,0,128+(offset*17)%128,128+offset);
        }
    }
    std::printf("Local6 grain: %zu independent basis/oracle/guard/stride/partial/public cases passed\n",checks);
}
