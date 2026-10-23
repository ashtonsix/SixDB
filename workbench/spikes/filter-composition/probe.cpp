#include <ikea/seriespack.h>
#include <ikea/seriespack/author/chain.h>
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

// Isolated architectural control: unchanged native kernels, only NEON here.
// MODE0 fully fused; MODE1 shared decode/compare; MODE2 selective hot fusion.
#ifndef MODE
#define MODE 0
#endif
namespace sp = ikea::seriespack;
using C = ikea::comparison;
using word = std::uint64_t;
template<unsigned B> using Packet = sp::native::values<B * 8>;
template<unsigned B> using Chain = sp::author::chain<B * 8, 4>;
template<unsigned K> using Source = sp::dense_source<sp::layout<K>>;
struct Binding { const void* source; void* chain; word query{}; std::uint16_t result{}; };
using Call = std::uint16_t(*)(Binding&, std::size_t, word, std::uint16_t);

template<unsigned K>
[[gnu::always_inline]] auto decode_body(void* opaque, std::size_t row,
    std::uint16_t active, Packet<sizeof(sp::uint_for<K>)>, unsigned)
    -> typename Chain<sizeof(sp::uint_for<K>)>::result {
    const auto& source = *static_cast<const Source<K>*>(static_cast<Binding*>(opaque)->source);
    sp::author::native_ops ops;
    auto decoded = sp::author::read(ops, sp::author::expression(source), row, active);
    return {Packet<sizeof(sp::uint_for<K>)>{decoded.v}, active};
}
template<unsigned B, C Op>
[[gnu::always_inline]] auto compare_body(void* opaque, std::size_t,
    std::uint16_t active, Packet<B> value, unsigned) -> typename Chain<B>::result {
    auto bits = sp::author::filter_values<Op>(value, static_cast<Binding*>(opaque)->query, active);
    return {value, bits};
}
template<unsigned B>
[[gnu::always_inline]] void finish(void* opaque, std::size_t,
    std::uint16_t active, Packet<B>) { static_cast<Binding*>(opaque)->result = active; }

template<unsigned B>
[[gnu::noinline]] std::uint16_t shared_entry(Binding& state, std::size_t first,
    word rhs, std::uint16_t active) {
    if (!active) return 0;
    state.query = rhs;
    static_cast<const Chain<B>*>(state.chain)->run(&state, first, active, Packet<B>{});
    return state.result;
}
template<unsigned K, C Op>
[[gnu::noinline]] std::uint16_t fused(Binding& state, std::size_t first,
    word rhs, std::uint16_t active) {
    return sp::author::filter16_unchecked<Op>(
        sp::author::expression(*static_cast<const Source<K>*>(state.source)),
        first, rhs, active);
}
template<unsigned K, C Op> constexpr bool hot =
    (K < 8 && (Op == C::equal || K <= 5)) || K == 8 || K == 16 || K == 32 || K == 64;
template<unsigned K, C Op> constexpr Call selected() {
    if constexpr (MODE == 0 || hot<K, Op>) return &fused<K, Op>;
    else return nullptr;
}
template<unsigned K> constexpr auto fused_calls() {
    return std::array<Call, 5>{selected<K,C::equal>(), selected<K,C::less>(),
        selected<K,C::less_equal>(), selected<K,C::greater>(), selected<K,C::greater_equal>()};
}
template<unsigned B> constexpr auto comparisons() {
    return std::array<typename Chain<B>::function, 5>{
        &Chain<B>::template stage<compare_body<B,C::equal>>,
        &Chain<B>::template stage<compare_body<B,C::less>>,
        &Chain<B>::template stage<compare_body<B,C::less_equal>>,
        &Chain<B>::template stage<compare_body<B,C::greater>>,
        &Chain<B>::template stage<compare_body<B,C::greater_equal>>};
}

[[gnu::noinline]] bool expected(C op, word a, word b) {
    switch(op) {
    case C::equal:return a==b;case C::less:return a<b;case C::less_equal:return a<=b;
    case C::greater:return a>b;case C::greater_equal:return a>=b;
    }
    __builtin_unreachable();
}
// Independent Local encoder and scalar oracle, identical for all profiles.
[[gnu::noinline]] void encode(unsigned k, std::uint8_t* out, word* truth) {
    word random = 0x1234567812345678ULL + k;
    const word limit = ~word{0} >> (64-k);
    for (unsigned i=0;i<64;++i) {
        random ^= random << 13;random ^= random >> 7;random ^= random << 17;
        const word value=i%5==0?0:i%5==1?limit:random&limit;truth[i]=value;
        auto* tile=out+(i/8)*k;
        for(unsigned b=0;b<k/8;++b)tile[(i%8)*(k/8)+b]=value>>(k%8+b*8);
        for(unsigned b=0;b<k%8;++b)tile[8*(k/8)+b]|=((value>>b)&1)<<(i%8);
    }
}
[[gnu::noinline]] word verify(Call call, Binding& state, unsigned k, C op,
    const word* truth) {
    word checksum=0;
    const word limit=~word{0}>>(64-k);
    for(word query:{word(0),word(1),limit/2,limit,limit+1,~word{0}})
        for(unsigned first:{0u,16u,32u,48u})
            for(std::uint16_t active:{std::uint16_t(0),std::uint16_t(1),std::uint16_t(0xa53c),std::uint16_t(0xffff)}) {
                unsigned want=0;
                for(unsigned i=0;i<16;++i)
                    if((active>>i&1)&&expected(op,truth[first+i],query))want|=1u<<i;
                auto got=call(state,first,query,active);assert(got==want);checksum+=got;
            }
    return checksum;
}
template<unsigned K> word run() {
    std::array<std::uint8_t,K*8> bytes{};
    std::array<word,64> truth{};encode(K,bytes.data(),truth.data());
    Source<K> source{std::as_bytes(std::span{bytes}).data(),64};Binding state{&source,nullptr};
    word checksum=0;
    constexpr bool all_hot = K<=5 || K==8 || K==16 || K==32 || K==64;
    if constexpr (MODE == 0 || (MODE==2 && all_hot)) {
        auto callbacks=fused_calls<K>();
        for(unsigned op=0;op<5;++op)checksum+=verify(callbacks[op],state,K,C(op),truth.data());
    } else {
        constexpr unsigned B=sizeof(sp::uint_for<K>);
        auto compare=comparisons<B>();
        for(unsigned op=0;op<5;++op) {
            std::array<typename Chain<B>::function,2> stages{
                &Chain<B>::template stage<decode_body<K>>,compare[op]};
            auto chain=*Chain<B>::make(stages,&Chain<B>::template completion<finish<B>>);
            state.chain=&chain;Call call=&shared_entry<B>;
            if constexpr(MODE==2) {auto selected=fused_calls<K>();if(selected[op])call=selected[op];}
            checksum+=verify(call,state,K,C(op),truth.data());
        }
    }
    return checksum;
}
template<std::size_t... I> word run_all(std::index_sequence<I...>) {return (run<I+1>()+...);}
int main() {
    word sum=run_all(std::make_index_sequence<64>{});
    std::printf("%llu\n",(unsigned long long)sum);
}
