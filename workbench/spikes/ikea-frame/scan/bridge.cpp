#include "material.h"
#include <algorithm>
#include <limits>
#include <variant>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <cpuid.h>
#include <immintrin.h>
#endif

namespace {
namespace fe=frame_encoding;
using fe::u64;using fe::u128;
using Native=std::variant<std::vector<std::uint8_t>,std::vector<std::uint16_t>,
                          std::vector<std::uint32_t>,std::vector<u64>>;
struct Image {
    fe::column encoded;
    fe::block_mask mask;
    Native native;
    u64 minimum{},maximum{};
    std::size_t rows{};
    bool all=true;
    template<class T> void make_native(std::span<const u64> input) {
        std::vector<T> out;out.reserve(input.size());
        for(auto value:input)out.push_back(static_cast<T>(value-minimum));
        native=std::move(out);
    }
    explicit Image(std::span<const u64> input):mask(input.size()/256),rows(input.size()) {
        const auto bounds=std::minmax_element(input.begin(),input.end());
        minimum=*bounds.first;maximum=*bounds.second;
        const auto width=fe::width_of(maximum-minimum);
        if(width<=8)make_native<std::uint8_t>(input);
        else if(width<=16)make_native<std::uint16_t>(input);
        else if(width<=32)make_native<std::uint32_t>(input);
        else make_native<u64>(input);
        encoded=fe::encode(input,{.bind_decoders=false});
        for(std::size_t b=0;b<mask.blocks;++b)mask.set(b);
    }
};
struct Result {u64 low,high;};
struct Stats {u64 rows,frame_payload,frame_metadata,frame_capacity,native_capacity,native_lane,minimum,maximum,mask_bytes;};
struct Range {const void* address;u64 bytes,role;};
template<class T> u128 sum(const T* values,std::size_t count) {
    // Same exact law as the native owner control: narrow sums fit u64;
    // split wide values into two vectorisable 32-bit-half accumulators.
    u64 low=0,high=0;
    for(std::size_t i=0;i<count;++i) {
        if constexpr(sizeof(T)<8)low+=values[i];
        else {low+=static_cast<std::uint32_t>(values[i]);high+=u64(values[i])>>32;}
    }
    return (u128(high)<<32)+low;
}
template<class T> u64 count_ge(const T* values,std::size_t count,u64 rhs) {
    if(rhs>std::numeric_limits<T>::max())return 0;
    const auto threshold=static_cast<T>(rhs);
    u64 answer=0;
    for(std::size_t i=0;i<count;++i)answer+=values[i]>=threshold;
    return answer;
}
u128 native_query(const Image& image,bool count,u64 threshold) {
    const auto rows=image.mask.popcount()*256;
    if(count && (threshold<=image.minimum || threshold>image.maximum))
        return threshold<=image.minimum?rows:0;
    return std::visit([&](const auto& v)->u128 {
        u128 answer=0;
        const auto apply=[&](std::size_t first,std::size_t n) {
            if(count)answer+=count_ge(v.data()+first,n,threshold-image.minimum);
            else answer+=sum(v.data()+first,n)+u128(image.minimum)*n;
        };
        if(image.all)apply(0,image.rows);
        else fe::for_each_block(image.mask,[&](std::size_t b){apply(b*256,256);});
        return answer;
    },image.native);
}
void flush_line(std::uintptr_t address) {
#if defined(__x86_64__)
    _mm_clflush(reinterpret_cast<const void*>(address));
#elif defined(__aarch64__)
    asm volatile("dc civac, %0"::"r"(address):"memory");
#endif
}
void flush_done() {
#if defined(__x86_64__)
    _mm_mfence();_mm_lfence();
#elif defined(__aarch64__)
    asm volatile("dsb ish\n\tisb":::"memory");
#endif
}
}

#define SCAN_API extern "C" __attribute__((visibility("default")))
SCAN_API void* frame_scan_build(const u64* values,std::size_t rows) noexcept {
    if(!values || !rows || rows%256 || rows>UINT32_MAX)return nullptr;
    try {return new Image({values,rows});}catch(...){return nullptr;}
}
SCAN_API void frame_scan_destroy(void* opaque) noexcept {delete static_cast<Image*>(opaque);}
SCAN_API int frame_scan_mask(void* opaque,const u64* words,std::size_t count) noexcept {
    auto& image=*static_cast<Image*>(opaque);
    if(!words || count!=image.mask.words.size())return 0;
    if(image.mask.blocks%64 && (words[count-1]>>(image.mask.blocks%64)))return 0;
    std::copy_n(words,count,image.mask.words.data());
    image.all=image.mask.popcount()==image.mask.blocks;return 1;
}
SCAN_API Result frame_scan_query(void* opaque,unsigned implementation,unsigned operation,u64 threshold) noexcept {
    const auto& image=*static_cast<Image*>(opaque);
    assert(implementation<=2 && operation<=1);
    const auto result=implementation==1?native_query(image,operation==1,threshold):
        implementation==2?(operation==1?u128(fe::catalogue_count_ge(image.encoded,image.mask,threshold)):
                            fe::catalogue_sum(image.encoded,image.mask)):
        operation==1?u128(fe::encoded_count_ge(image.encoded,image.mask,threshold)):
                     fe::encoded_sum(image.encoded,image.mask);
    return {u64(result),u64(result>>64)};
}
SCAN_API Stats frame_scan_stats(void* opaque) noexcept {
    const auto& image=*static_cast<Image*>(opaque);const auto& c=image.encoded;
    const auto capacity=sizeof(c)+c.payload.capacity()+c.blocks.capacity()*sizeof(c.blocks[0])+
        c.parts.capacity()*sizeof(c.parts[0])+c.coded.capacity()*sizeof(c.coded[0])+c.binding_bytes();
    const auto native=std::visit([](const auto& v){return std::pair{v.capacity()*sizeof(v[0]),sizeof(v[0])};},image.native);
    const auto native_capacity=native.first+sizeof(image.native)+sizeof(image.minimum)+
        sizeof(image.maximum)+sizeof(image.rows);
    return {image.rows,c.payload.size(),c.total_bytes()-c.payload.size(),capacity,native_capacity,
            native.second,image.minimum,image.maximum,image.mask.words.capacity()*sizeof(u64)};
}
SCAN_API std::size_t frame_scan_ranges(void* opaque,unsigned implementation,Range* output,std::size_t capacity) noexcept {
    const auto& image=*static_cast<Image*>(opaque);std::size_t count=0;
    const auto add=[&](const auto& v,unsigned role) {
        const auto bytes=v.size()*sizeof(v[0]);if(!bytes)return;
        if(count<capacity)output[count]={v.data(),bytes,role};++count;
    };
    if(implementation==1)std::visit([&](const auto& v){add(v,0);},image.native);
    else {add(image.encoded.payload,0);add(image.encoded.blocks,1);
          add(image.encoded.parts,1);add(image.encoded.coded,1);}
    return count;
}
SCAN_API int frame_scan_flush_supported() noexcept {
    // Existing memory-characterisation method: capability-test privileged ARM
    // maintenance in a child; no global perf or cache policy is changed.
#if defined(__x86_64__)
    unsigned a,b,c,d;return __get_cpuid(1,&a,&b,&c,&d) && (d&(1u<<19));
#elif defined(__aarch64__)
    const auto child=fork();
    if(child==0){rlimit limit{0,0};setrlimit(RLIMIT_CORE,&limit);
        alignas(256) u64 value{};flush_line(reinterpret_cast<std::uintptr_t>(&value));flush_done();_exit(0);}
    if(child<0)return 0;int status{};
    return waitpid(child,&status,0)>0 && WIFEXITED(status) && WEXITSTATUS(status)==0;
#else
    return 0;
#endif
}
SCAN_API std::size_t frame_scan_flush(const void* pointer,std::size_t bytes,std::size_t line) noexcept {
    assert(line && std::has_single_bit(line));if(!bytes)return 0;
    const auto first=reinterpret_cast<std::uintptr_t>(pointer);
    assert(first && bytes<=std::numeric_limits<std::uintptr_t>::max()-first);
    const auto last=first+bytes-1;std::size_t count=1;flush_line(first);
    auto next=(first&~(line-1));
    while(last-next>=line){next+=line;flush_line(next);++count;}
    flush_done();return count;
}
