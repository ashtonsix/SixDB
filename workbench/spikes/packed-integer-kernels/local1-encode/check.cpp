#include <ikea/seriespack.h>
#include <ikea/seriespack/native_avx512.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

#if !defined(__AVX512VBMI2__) || !defined(__GFNI__)
#error Full AVX512 profile required for this Local1 encode diagnostic.
#endif
namespace {
namespace sp=ikea::seriespack;
std::size_t cases=0;
std::uint64_t rng=0xa2d718c3495be60fULL;
std::uint64_t random() {rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
void require(bool ok) {if(!ok)std::abort();}
struct guarded {
    std::size_t page=sysconf(_SC_PAGESIZE),size;
    std::uint8_t* mapping;std::uint8_t* bytes;
    explicit guarded(std::size_t n):size(std::max(page,(n+page-1)/page*page)),
        mapping(static_cast<std::uint8_t*>(mmap(nullptr,size+2*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0))),
        bytes(mapping==MAP_FAILED?nullptr:mapping+page) {
        require(mapping!=MAP_FAILED);require(mprotect(bytes,size,PROT_READ|PROT_WRITE)==0);
    }
    ~guarded(){munmap(mapping,size+2*page);}
};
struct plane {
    struct window {std::uint8_t* bytes;std::vector<std::uint8_t> expected;};
    std::uint8_t* mapping=nullptr;std::uint8_t* data=nullptr;
    std::size_t page=sysconf(_SC_PAGESIZE),mapping_bytes=0,extent=0,stride=0,offset=0;
    unsigned tiles=0,tile_bytes=0;bool gaps=false;
    std::vector<window> windows;
    plane(unsigned n,unsigned tile_size,unsigned requested_stride,bool protect_gaps,bool end,unsigned prefix):
        stride(tile_size?requested_stride:0),tiles((n+7)/8),tile_bytes(tile_size),gaps(protect_gaps) {
        if(!tile_bytes||!tiles)return;
        if(gaps)stride=2*page;
        extent=(tiles-1)*stride+tile_bytes;
        const auto writable=gaps?page:((extent+prefix+page-1)/page)*page;
        mapping_bytes=gaps?tiles*2*page:writable+2*page;
        mapping=static_cast<std::uint8_t*>(mmap(nullptr,mapping_bytes,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
        require(mapping!=MAP_FAILED);
        if(gaps) {
            for(unsigned tile=0;tile<tiles;++tile) {
                auto* p=mapping+tile*2*page;require(mprotect(p,page,PROT_READ|PROT_WRITE)==0);
                windows.push_back({p,std::vector<std::uint8_t>(page,0xa7)});
            }
            data=mapping+page-tile_bytes;
        } else {
            auto* p=mapping+page;require(mprotect(p,writable,PROT_READ|PROT_WRITE)==0);
            windows.push_back({p,std::vector<std::uint8_t>(writable,0xa7)});
            offset=end?writable-extent:prefix;data=p+offset;
        }
        for(unsigned tile=0;tile<tiles;++tile)
            for(unsigned b=0;b<tile_bytes;++b) expected(tile,b)=0;
        reset();
    }
    ~plane(){if(mapping)munmap(mapping,mapping_bytes);}
    std::uint8_t& expected(unsigned tile,unsigned byte) {
        if(gaps)return windows[tile].expected[page-tile_bytes+byte];
        return windows[0].expected[offset+tile*stride+byte];
    }
    void reset(){for(auto& w:windows)std::memset(w.bytes,0xa7,w.expected.size());}
    void verify()const{for(const auto& w:windows)require(std::memcmp(w.bytes,w.expected.data(),w.expected.size())==0);}
    std::span<std::byte> span(){return{reinterpret_cast<std::byte*>(data),extent};}
};

template<class U>
[[gnu::noinline]] void raw(const U* in,std::uint8_t* out,std::size_t n,std::size_t stride) {
    const auto full=n/8;
    if(stride==1)sp::avx512::encode_low_tiles<1,sp::geometry::local8>(in,out,full);
    else for(std::size_t tile=0;tile<full;++tile)
        sp::avx512::encode_low_tile<1,sp::geometry::local8>(in+tile*8,out+tile*stride);
    if(const auto left=n%8;left!=0) {
        std::array<U,8> boundary{};std::memcpy(boundary.data(),in+full*8,left*sizeof(U));
        sp::avx512::encode_low_tile<1,sp::geometry::local8>(boundary.data(),out+full*stride);
    }
}
template<class U>
void private_case(unsigned n,unsigned stride,bool end,unsigned prefix=0) {
    guarded input(n*sizeof(U)+prefix);plane payload(n,1,stride,false,end,prefix);
    auto* p=n?input.bytes+(end?input.size-n*sizeof(U):prefix):input.mapping;
    for(unsigned i=0;i<n;++i) {
        const U value=static_cast<U>(random());std::memcpy(p+i*sizeof(U),&value,sizeof(U));
        payload.expected(i/8,0)|=std::uint8_t((value&1)<<(i%8));
    }
    const std::vector<std::uint8_t> original(input.bytes,input.bytes+input.size);
    raw(reinterpret_cast<const U*>(p),payload.data,n,payload.stride);
    payload.verify();require(std::memcmp(input.bytes,original.data(),input.size)==0);++cases;
}

template<class U>
void public_case(unsigned h,unsigned n,unsigned placement,bool end,unsigned prefix=0) {
    guarded input(n*sizeof(U)+prefix);
    plane payload(n,1,1,placement==2,end,prefix);
    plane head0(n,h?8:0,placement==1?13:8,placement==2,!end,17);
    plane head1(n,h==16?8:0,placement==1?23:8,placement==2,end,39);
    auto* p=n?input.bytes+(end?input.size-n*sizeof(U):prefix):input.mapping;
    const auto mask=(std::uint64_t{1}<<(1+h))-1;
    for(unsigned i=0;i<n;++i) {
        const U value=static_cast<U>(random()&mask);std::memcpy(p+i*sizeof(U),&value,sizeof(U));
        payload.expected(i/8,0)|=std::uint8_t((value&1)<<(i%8));
        if(h)head0.expected(i/8,i%8)=std::uint8_t(std::uint64_t(value)>>(h-7));
        if(h==16)head1.expected(i/8,i%8)=std::uint8_t(std::uint64_t(value)>>1);
    }
    const std::vector<std::uint8_t> original(input.bytes,input.bytes+input.size);
    auto destination=sp::mutable_view::attach({1+h,h,sp::geometry::local8},n,
        {{payload.span(),payload.stride},{{{head0.span(),head0.stride},{head1.span(),head1.stride}}}});
    require(destination.has_value());
    const sp::input_values values{std::span(reinterpret_cast<const U*>(p),n)};
    const auto encoder=sp::bind_encoder(*destination,sp::execution_target::avx512);require(encoder.has_value());
    for(unsigned checked=0;checked<2;++checked) {
        payload.reset();head0.reset();head1.reset();
        if(checked)require(sp::encode(*destination,values,nullptr,sp::execution_target::avx512).has_value());
        else encoder->encode(values);
        payload.verify();head0.verify();head1.verify();
        require(std::memcmp(input.bytes,original.data(),input.size)==0);++cases;
    }
}
void basis() {
    std::array<std::uint8_t,256> input{};std::array<std::uint8_t,32> output{},expected{};
    for(unsigned bit=0;bit<=2048;++bit) {
        input.fill(0);expected.fill(0);
        if(bit<2048) {input[bit/8]=1u<<(bit%8);if(bit%8==0)expected[bit/64]=1u<<((bit/8)%8);}
        sp::avx512::encode_low_tiles<1,sp::geometry::local8>(input.data(),output.data(),32);
        require(output==expected);++cases;
    }
}
template<class U>
void carrier() {
    for(unsigned n:{0u,1u,7u,8u,9u,15u,16u,17u,31u,32u,33u,63u,64u,65u,127u,128u,129u,255u,256u,257u,511u,512u,513u,8192u,65536u}) {
        for(bool end:{false,true}) {
            for(unsigned stride:{1u,2u,13u,64u})private_case<U>(n,stride,end);
            for(unsigned h:{0u,8u,16u})for(unsigned placement=0;placement<3;++placement)
                if(n<=513||placement!=2)public_case<U>(h,n,placement,end);
        }
    }
    for(unsigned prefix=0;prefix<128;++prefix)private_case<U>(257,1,false,prefix);
}
}
int main() {
    basis();carrier<std::uint8_t>();carrier<std::uint16_t>();carrier<std::uint32_t>();carrier<std::uint64_t>();
    for(unsigned prefix=0;prefix<128;++prefix)for(unsigned h:{0u,8u,16u})
        public_case<std::uint8_t>(h,257,1,false,prefix);
    std::printf("Local1 region4 encode: %zu basis/projection/guard/stride/partial/public cases passed\n",cases);
}
