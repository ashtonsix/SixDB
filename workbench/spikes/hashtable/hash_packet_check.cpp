#include "hash_packet.h"
#include <sys/mman.h>
#include <unistd.h>
#include <random>
#ifndef HT_HASH_CHECK_KEY
#define HT_HASH_CHECK_KEY 0
#endif
#ifndef HT_CHECK_COMPACT
#define HT_CHECK_COMPACT 0
#endif

namespace {
using namespace ht;
constexpr word canary=0xa9751820e634fbcdULL;
std::uint64_t cases=0,keys_checked=0,products_checked=0,low_products_checked=0;

// Independent arithmetic oracle. No vector partial-product formula or scalar
// permutation helper is reused when constructing expected packet fields.
word reference_fold(word value) {
    const __uint128_t product=__uint128_t(value)*UINT64_C(0x79d5f9e0de1e8cf5);
    return word(product)^word(product>>64);
}
constexpr word reference_mask(unsigned bits) {
    return bits==64?~word{0}:(word{1}<<bits)-1;
}
template<unsigned K,unsigned A> auto reference_split(const permutation<K,A>& p,word key) {
    constexpr unsigned remainder=K-A,fingerprint=std::min(8u,remainder);
    const word x=(key^p.input_seed)&reference_mask(K);
    const word r=x>>A,s=r>>fingerprint;
    const word selector=[&] {
        if constexpr(HT_SELECTOR_HASH && remainder>8) {
            constexpr unsigned width=remainder<=33?32:64;
            const __uint128_t full=__uint128_t(s)*p.selector_seed+p.selector_bias;
            return (word(full)&reference_mask(width))>>(width-fingerprint);
        }else return reference_fold(s^p.selector_seed)&reference_mask(fingerprint);
    }();
    return typename permutation<K,A>::parts{
        (x&reference_mask(A))^(reference_fold(r^p.bucket_seed)&reference_mask(A)),s,
        byte((r&reference_mask(fingerprint))^selector)};
}

template<unsigned N,unsigned K,unsigned A> void check_packet(const permutation<K,A>& hash,const word* keys) {
    const auto result=split_packet<N>(hash,std::span<const word,N>{keys,N});
#if HT_CHECK_COMPACT
    const auto compact=split_packet<N,true>(hash,std::span<const word,N>{keys,N});
    static_assert(sizeof(compact)==N*sizeof(word));
#endif
    for(unsigned i=0;i<N;++i) {
        const auto expected=reference_split(hash,keys[i]);
#if HT_CHECK_COMPACT
        const auto decoded=packet_parts<K,A>(compact,i);
        require(decoded.bucket==expected.bucket&&decoded.suffix==expected.suffix&&
                decoded.fingerprint==expected.fingerprint,"compact packet differs from uint128 oracle");
        require((compact.encoded[i]&~reference_mask(K))==0,"compact hash facts fit key domain");
#endif
        const auto scalar=hash.split(keys[i]);
        require(result.bucket[i]==expected.bucket&&result.suffix[i]==expected.suffix&&
                result.fingerprint[i]==expected.fingerprint,"SIMD packet differs from uint128 oracle");
        require(scalar.bucket==expected.bucket&&scalar.suffix==expected.suffix&&
                scalar.fingerprint==expected.fingerprint,"scalar differs from independent oracle");
        require(hash.invert(scalar)==(keys[i]&reference_mask(K)),"masked-domain inverse");
    }
    keys_checked+=N;
}

template<unsigned N,unsigned K,unsigned A> void geometry() {
    ++cases;
    alignas(64) std::array<word,N+2> input{};
    input.front()=input.back()=canary;
    auto* keys=input.data()+1; // Naturally word-aligned, deliberately not vector-aligned.
    std::mt19937_64 random(0x725631a98279ULL+K*97+A*337+N*981);
    const std::array<word,10> seeds{0,1,~word{0},word{1}<<63,0xffffffffULL,
        0xffffffff00000000ULL,0xaaaaaaaaaaaaaaaaULL,0x5555555555555555ULL,random(),random()};
    for(word seed:seeds) {
        const permutation<K,A> hash(seed);
        const word expected_zero=(HT_SELECTOR_HASH && permutation<K,A>::C!=0)?
            ((hash.selector_bias&reference_mask(permutation<K,A>::selector_word_bits))>>(permutation<K,A>::selector_word_bits-permutation<K,A>::F)):
            reference_fold(hash.selector_seed)&reference_mask(permutation<K,A>::F);
        require(hash.selector_zero()==expected_zero,
            "independent exact selector construction");
        // Every bit individually, its complement, and low/high 32-bit carry boundaries.
        for(unsigned bit=0;bit<64;++bit) {
            const word one=word{1}<<bit;
            for(unsigned i=0;i<N;++i) {
                switch(i%4) {case 0:keys[i]=one;break;case 1:keys[i]=~one;break;
                    case 2:keys[i]=one-1;break;default:keys[i]=one+1;break;}
            }
            check_packet<N>(hash,keys);
        }
        for(unsigned sample=0;sample<128;++sample) {
            for(unsigned i=0;i<N;++i)keys[i]=random();
            check_packet<N>(hash,keys);
        }
        // Equal-key packets must preserve every lane, including zero/all-ones.
        for(word key:{word{0},~word{0},word{0xffffffff},word{0x100000000},word{0xffffffff00000000}}) {
            for(unsigned i=0;i<N;++i)keys[i]=key;
            check_packet<N>(hash,keys);
        }
    }
    require(input.front()==canary&&input.back()==canary,"read-only input canaries");
}
template<unsigned K,std::size_t... A> void address_widths(std::index_sequence<A...>) {
    // Every address width, not just the boundaries of Kb/Kc; both packet widths.
    (geometry<4,K,A>(),...);(geometry<8,K,A>(),...);
}

void product_cases() {
    alignas(64) std::array<word,8> input{},output{};
    const std::array<word,10> halves{0,1,2,0x7ffffffe,0x7fffffff,0x80000000,0x80000001,
        0xfffffffd,0xfffffffe,0xffffffff};
    auto verify=[&] {
#if defined(__aarch64__)
        for(unsigned i=0;i<8;i+=2)vst1q_u64(output.data()+i,hash_packet_detail::fold(vld1q_u64(input.data()+i)));
#else
        for(unsigned i=0;i<8;i+=4)_mm256_storeu_si256(reinterpret_cast<__m256i*>(output.data()+i),
            hash_packet_detail::fold(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(input.data()+i))));
#endif
        for(unsigned i=0;i<8;++i){require(output[i]==reference_fold(input[i]),"SIMD folded product carry");++products_checked;}
#if defined(__AVX512F__)
        _mm512_storeu_si512(output.data(),hash_packet_detail::fold(_mm512_loadu_si512(input.data())));
        for(unsigned i=0;i<8;++i){require(output[i]==reference_fold(input[i]),"AVX512 folded product carry");++products_checked;}
#endif
    };
    for(word hi:halves)for(word lo:halves) {
        for(unsigned i=0;i<8;++i)input[i]=((hi<<32)|lo)+i-4;
        verify();
    }
    std::mt19937_64 random(0x491ae6207ULL);
    for(unsigned n=0;n<16384;++n) {
        for(auto& value:input)value=random();verify();
    }
}

// The coefficient is unrestricted: zero and even values are valid arithmetic
// inputs. Test the carry truncation independently of the seeded constructor.
void low_product_cases() {
    alignas(64) std::array<word,8> input{},output{};
    const std::array<word,12> coefficients{0,1,2,3,0x7fffffff,0x80000000,0xffffffff,
        0x100000000,0xffffffff00000000ULL,0x8000000000000000ULL,~word{1},~word{0}};
    std::mt19937_64 random(0x39124af779ULL);
    for(word a:coefficients)for(unsigned sample=0;sample<1024;++sample) {
        for(unsigned i=0;i<8;++i)input[i]=sample<64?(word(1)<<sample)+i-4:random();
#if defined(__aarch64__)
        for(unsigned i=0;i<8;i+=2)vst1q_u64(output.data()+i,
            hash_packet_detail::multiply_low(vld1q_u64(input.data()+i),a));
#else
        for(unsigned i=0;i<8;i+=4)_mm256_storeu_si256(reinterpret_cast<__m256i*>(output.data()+i),
            hash_packet_detail::multiply_low(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(input.data()+i)),a));
#endif
        for(unsigned i=0;i<8;++i){require(output[i]==word(__uint128_t(input[i])*a),"SIMD low product carry");++low_products_checked;}
#if defined(__AVX512F__)
        _mm512_storeu_si512(output.data(),hash_packet_detail::multiply_low(_mm512_loadu_si512(input.data()),a));
        for(unsigned i=0;i<8;++i){require(output[i]==word(__uint128_t(input[i])*a),"AVX512 low product carry");++low_products_checked;}
#endif
    }
}

template<unsigned K,unsigned A> void selector_parameters() {
    static_assert(permutation<K,A>::C>0);
    if constexpr(HT_SELECTOR_HASH) {
        const std::array<word,10> parameters{0,1,2,0xffffffff,0x100000000,
            0x7fffffff00000000ULL,0x8000000000000000ULL,0xffffffff00000000ULL,~word{1},~word{0}};
        std::mt19937_64 random(0x739514e271ULL+K*397+A*811);
        std::array<word,8> keys;
        permutation<K,A> hash(0xe239750da912ULL);
        for(word a:parameters)for(word b:parameters) {
            hash.selector_seed=a;hash.selector_bias=b;
            for(unsigned sample=0;sample<16;++sample) {
                for(auto& key:keys)key=random();
                check_packet<4>(hash,keys.data());check_packet<8>(hash,keys.data());
            }
        }
    }
}

template<unsigned N> void guarded_input() {
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));require(page>=N*sizeof(word),"page size");
    auto* memory=static_cast<byte*>(mmap(nullptr,3*page,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    require(memory!=MAP_FAILED,"guard mmap");require(mprotect(memory+page,page,PROT_READ|PROT_WRITE)==0,"guard mprotect");
    const permutation<64,20> hash(0x88121ae7713ULL);
    for(bool end:{false,true}) {
        auto* keys=reinterpret_cast<word*>(memory+page+(end?page-N*sizeof(word):0));
        for(unsigned i=0;i<N;++i)keys[i]=~word{0}-i;
        check_packet<N>(hash,keys);
    }
    require(munmap(memory,3*page)==0,"guard munmap");
}
template<unsigned N> concept supported_packet=requires(const permutation<64,20>& p,std::span<const word,N> keys) {
    split_packet<N>(p,keys);
};
static_assert(supported_packet<4>&&supported_packet<8>);
static_assert(!supported_packet<1>&&!supported_packet<2>&&!supported_packet<3>&&!supported_packet<16>);
} // namespace

// Stable code-generation witnesses: only the call boundary is out of line. A
// consumer can inline split_packet into complete getN/setN stages. These are not
// timed hash microbenchmarks and establish no complete-container speedup.
extern "C" [[gnu::noinline,gnu::used,gnu::retain]] void ht_hash_packet4_64a20(const ht::permutation<64,20>* p,
    const ht::word* keys,ht::hash_packet<4>* result) {
    *result=ht::split_packet<4>(*p,std::span<const ht::word,4>{keys,4});
}
extern "C" [[gnu::noinline,gnu::used,gnu::retain]] void ht_hash_packet8_64a20(const ht::permutation<64,20>* p,
    const ht::word* keys,ht::hash_packet<8>* result) {
    *result=ht::split_packet<8>(*p,std::span<const ht::word,8>{keys,8});
}
extern "C" [[gnu::noinline,gnu::used,gnu::retain]] void ht_hash_packet8_64a0(const ht::permutation<64,0>* p,
    const ht::word* keys,ht::hash_packet<8>* result) {
    *result=ht::split_packet<8>(*p,std::span<const ht::word,8>{keys,8});
}
extern "C" [[gnu::noinline,gnu::used,gnu::retain]] void ht_hash_packet8_16a9(const ht::permutation<16,9>* p,
    const ht::word* keys,ht::hash_packet<8>* result) {
    *result=ht::split_packet<8>(*p,std::span<const ht::word,8>{keys,8});
}

int main() {
    product_cases();low_product_cases();guarded_input<4>();guarded_input<8>();
    selector_parameters<16,0>();selector_parameters<40,7>();
    selector_parameters<40,6>();selector_parameters<64,0>();
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==8
    address_widths<8>(std::make_index_sequence<8>{});
#endif
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==16
    address_widths<16>(std::make_index_sequence<16>{});
#endif
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==24
    address_widths<24>(std::make_index_sequence<24>{});
#endif
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==32
    address_widths<32>(std::make_index_sequence<32>{});
#endif
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==40
    address_widths<40>(std::make_index_sequence<40>{});
#endif
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==48
    address_widths<48>(std::make_index_sequence<48>{});
#endif
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==56
    address_widths<56>(std::make_index_sequence<56>{});
#endif
#if HT_HASH_CHECK_KEY==0 || HT_HASH_CHECK_KEY==64
    address_widths<64>(std::make_index_sequence<64>{});
#endif
    std::printf("hash packet checks passed: %llu geometries, %llu keys, %llu folded products, %llu low products; "
        "N4/N8, every A0..K-1 for selected K, uint128 oracle, scalar parity, inverse, "
        "extremes/random/carries, duplicate lanes, non-vector-aligned and guarded inputs\n",
        (unsigned long long)cases,(unsigned long long)keys_checked,(unsigned long long)products_checked,(unsigned long long)low_products_checked);
    std::printf("selected key domain=%u (0 means all); compact packet checked=%u\n",HT_HASH_CHECK_KEY,HT_CHECK_COMPACT);
}
